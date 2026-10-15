/* checker.c - the TeXAspell engine. See checker.h. */
#include "checker.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cmddb.h"
#include "scan.h"
#include "speller.h"
#include "strset.h"

#define MAX_RECENT_ENVS 2000

struct texaspell {
    char home[1024];
    char dictionary[128];
    char kpsewhich[1024];
    char default_classes[512];
    int check_commands;
    int skip_allcaps;
    int min_word_length;
    char warning[1200];

    speller *sp;
    cmddb db;
    strset words;        /* words.txt                                   */
    strset user_cmds;    /* commands.txt and auto-commands.txt          */
    strset user_envs;
    strset user_keys;
    strset packages;     /* names listed in packages.txt                */
    strset recent_envs;  /* environment names flagged recently          */
    strset recent_keys;  /* option keys flagged recently                */
};

static const char default_config[] =
    "# TeXAspell settings. Lines starting with # are comments.\n"
    "# After editing this file, restart the spell service (see README).\n"
    "\n"
    "# aspell dictionary to use:\n"
    "#   en_GB-ise  British English, -ise spellings (organise)\n"
    "#   en_GB-ize  British English, Oxford -ize spellings (organize)\n"
    "#   en_GB      British English, accepts both\n"
    "dictionary = en_GB-ise\n"
    "\n"
    "# Check \\command names and \\begin{environment} names against the\n"
    "# commands your TeX installation defines.\n"
    "check_commands = yes\n"
    "\n"
    "# Ignore words written entirely in capitals (acronyms such as NHS, MPs).\n"
    "skip_allcaps = yes\n"
    "\n"
    "# Ignore words shorter than this many letters.\n"
    "min_word_length = 2\n"
    "\n"
    "# Full path to kpsewhich, or auto to search the usual places.\n"
    "kpsewhich = auto\n"
    "\n"
    "# Document classes whose commands are always accepted (the text being\n"
    "# checked often does not include the \\documentclass line).\n"
    "default_classes = article report book\n";

static const char default_packages[] =
    "# Packages whose commands are always accepted, one per line.\n"
    "# TeXAspell adds a package here automatically the first time it sees\n"
    "# \\usepackage{...} for it. A name ending in .cls is a document class.\n"
    "amsmath\n"
    "amssymb\n"
    "amsthm\n"
    "graphicx\n"
    "hyperref\n"
    "xcolor\n"
    "booktabs\n"
    "geometry\n";

static const char default_commands[] =
    "# Your own LaTeX commands and environments that TeXAspell should accept.\n"
    "# One per line: \\command for a command, a bare name for an environment,\n"
    "# name= for an option key (as in \\hypersetup{name=...}).\n"
    "# \"Learn Spelling\" on a flagged command, environment or key adds it here.\n";

static const char default_auto_commands[] =
    "# Commands and environments TeXAspell has seen defined in your documents\n"
    "# (\\newcommand, \\newenvironment, ...). Safe to edit or delete.\n";

static const char default_words[] =
    "# Your own words. \"Learn Spelling\" adds words here.\n";

/* ------------------------------------------------------------------------
 * Files
 * ---------------------------------------------------------------------- */

static void path_join(char *out, size_t outlen, const char *dir, const char *name)
{
    snprintf(out, outlen, "%s/%s", dir, name);
}

static int mkdirs(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static void ensure_file(const char *home, const char *name, const char *content)
{
    char path[1100];
    path_join(path, sizeof path, home, name);
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        return;
    }
    f = fopen(path, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

static void append_line(const texaspell *ta, const char *name, const char *line,
                        size_t len)
{
    char path[1100];
    path_join(path, sizeof path, ta->home, name);
    FILE *f = fopen(path, "a");
    if (!f)
        return;
    fwrite(line, 1, len, f);
    fputc('\n', f);
    fclose(f);
}

/* Remove every line equal to line from the file. */
static void remove_line(const texaspell *ta, const char *name, const char *line,
                        size_t len)
{
    char path[1100], tmp[1110];
    path_join(path, sizeof path, ta->home, name);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *in = fopen(path, "r");
    if (!in)
        return;
    FILE *out = fopen(tmp, "w");
    if (!out) {
        fclose(in);
        return;
    }
    char buf[4096];
    while (fgets(buf, sizeof buf, in)) {
        size_t l = strlen(buf);
        size_t k = l;
        while (k && (buf[k - 1] == '\n' || buf[k - 1] == '\r' || buf[k - 1] == ' '))
            k--;
        if (k == len && memcmp(buf, line, len) == 0)
            continue;
        fputs(buf, out);
    }
    fclose(in);
    fclose(out);
    rename(tmp, path);
}

typedef void (*line_fn)(texaspell *ta, char *line);

/* Call fn for each non-blank, non-comment line (trimmed) of a file. */
static void each_line(texaspell *ta, const char *name, line_fn fn)
{
    char path[1100];
    path_join(path, sizeof path, ta->home, name);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char buf[4096];
    while (fgets(buf, sizeof buf, f)) {
        char *s = buf;
        while (*s && isspace((unsigned char)*s))
            s++;
        size_t l = strlen(s);
        while (l && isspace((unsigned char)s[l - 1]))
            s[--l] = '\0';
        if (!l || *s == '#')
            continue;
        fn(ta, s);
    }
    fclose(f);
}

static int yes(const char *v)
{
    return strcmp(v, "yes") == 0 || strcmp(v, "true") == 0 ||
           strcmp(v, "on") == 0 || strcmp(v, "1") == 0;
}

static void config_line(texaspell *ta, char *line)
{
    char *eq = strchr(line, '=');
    if (!eq)
        return;
    *eq = '\0';
    char *key = line, *val = eq + 1;
    size_t kl = strlen(key);
    while (kl && isspace((unsigned char)key[kl - 1]))
        key[--kl] = '\0';
    while (*val && isspace((unsigned char)*val))
        val++;
    if (strcmp(key, "dictionary") == 0)
        snprintf(ta->dictionary, sizeof ta->dictionary, "%s", val);
    else if (strcmp(key, "check_commands") == 0)
        ta->check_commands = yes(val);
    else if (strcmp(key, "skip_allcaps") == 0)
        ta->skip_allcaps = yes(val);
    else if (strcmp(key, "min_word_length") == 0)
        ta->min_word_length = atoi(val);
    else if (strcmp(key, "kpsewhich") == 0)
        snprintf(ta->kpsewhich, sizeof ta->kpsewhich, "%s", val);
    else if (strcmp(key, "default_classes") == 0)
        snprintf(ta->default_classes, sizeof ta->default_classes, "%s", val);
}

static void word_line(texaspell *ta, char *line)
{
    strset_add(&ta->words, line);
}

static void command_line(texaspell *ta, char *line)
{
    size_t l = strlen(line);
    if (line[0] == '\\') {
        if (line[1])
            strset_add(&ta->user_cmds, line + 1);
    } else if (l > 1 && line[l - 1] == '=') {
        strset_addn(&ta->user_keys, line, l - 1);
    } else {
        strset_add(&ta->user_envs, line);
    }
}

static void package_line(texaspell *ta, char *line)
{
    strset_add(&ta->packages, line);
}

static void load_user_lists(texaspell *ta)
{
    strset_free(&ta->words);
    strset_free(&ta->user_cmds);
    strset_free(&ta->user_envs);
    strset_free(&ta->user_keys);
    each_line(ta, "words.txt", word_line);
    each_line(ta, "commands.txt", command_line);
    each_line(ta, "auto-commands.txt", command_line);
}

static void default_home(char *out, size_t outlen)
{
    const char *env = getenv("TEXASPELL_HOME");
    if (env && *env) {
        snprintf(out, outlen, "%s", env);
        return;
    }
    const char *home = getenv("HOME");
    if (!home)
        home = "/tmp";
#ifdef __APPLE__
    snprintf(out, outlen, "%s/Library/Application Support/TeXAspell", home);
#else
    snprintf(out, outlen, "%s/.config/texaspell", home);
#endif
}

/* ------------------------------------------------------------------------
 * Open / close
 * ---------------------------------------------------------------------- */

/* Turn a packages.txt entry into a file name: "amsmath" -> "amsmath.sty". */
static void package_file(const char *entry, char *out, size_t outlen)
{
    size_t l = strlen(entry);
    if (l > 4 && (strcmp(entry + l - 4, ".cls") == 0 || strcmp(entry + l - 4, ".sty") == 0))
        snprintf(out, outlen, "%s", entry);
    else
        snprintf(out, outlen, "%s.sty", entry);
}

static void load_base(texaspell *ta)
{
    size_t cap = ta->packages.count + 64;
    char **names = calloc(cap, sizeof *names);
    if (!names)
        return;
    size_t n = 0;
    names[n++] = strdup("latex.ltx");

    char classes[512];
    snprintf(classes, sizeof classes, "%s", ta->default_classes);
    for (char *tok = strtok(classes, " ,\t"); tok && n < cap; tok = strtok(NULL, " ,\t")) {
        char buf[256];
        snprintf(buf, sizeof buf, "%s.cls", tok);
        names[n++] = strdup(buf);
    }
    for (size_t i = 0; i < ta->packages.cap && n < cap; i++) {
        if (!ta->packages.slots[i])
            continue;
        char buf[256];
        package_file(ta->packages.slots[i], buf, sizeof buf);
        names[n++] = strdup(buf);
    }
    cmddb_load(&ta->db, (const char *const *)names, n, NULL);
    for (size_t i = 0; i < n; i++)
        free(names[i]);
    free(names);
}

texaspell *ta_open(const char *home, char *err, size_t errlen)
{
    texaspell *ta = calloc(1, sizeof *ta);
    if (!ta) {
        if (err) snprintf(err, errlen, "out of memory");
        return NULL;
    }
    if (home)
        snprintf(ta->home, sizeof ta->home, "%s", home);
    else
        default_home(ta->home, sizeof ta->home);

    snprintf(ta->dictionary, sizeof ta->dictionary, "en_GB-ise");
    snprintf(ta->kpsewhich, sizeof ta->kpsewhich, "auto");
    snprintf(ta->default_classes, sizeof ta->default_classes, "article report book");
    ta->check_commands = 1;
    ta->skip_allcaps = 1;
    ta->min_word_length = 2;

    strset_init(&ta->words);
    strset_init(&ta->user_cmds);
    strset_init(&ta->user_envs);
    strset_init(&ta->user_keys);
    strset_init(&ta->packages);
    strset_init(&ta->recent_envs);
    strset_init(&ta->recent_keys);

    if (mkdirs(ta->home) == 0) {
        ensure_file(ta->home, "config.txt", default_config);
        ensure_file(ta->home, "packages.txt", default_packages);
        ensure_file(ta->home, "commands.txt", default_commands);
        ensure_file(ta->home, "auto-commands.txt", default_auto_commands);
        ensure_file(ta->home, "words.txt", default_words);
    }
    each_line(ta, "config.txt", config_line);
    const char *dict_override = getenv("TEXASPELL_DICTIONARY");
    if (dict_override && *dict_override)
        snprintf(ta->dictionary, sizeof ta->dictionary, "%s", dict_override);
    each_line(ta, "packages.txt", package_line);
    load_user_lists(ta);

    char aerr[512] = "";
    ta->sp = speller_open(ta->dictionary, aerr, sizeof aerr);
    if (!ta->sp) {
        if (err)
            snprintf(err, errlen, "cannot open aspell dictionary \"%s\" (%s). "
                     "Is it installed? Try: aspell dicts", ta->dictionary, aerr);
        ta_close(ta);
        return NULL;
    }

    cmddb_init(&ta->db, ta->kpsewhich);
    if (ta->check_commands) {
        load_base(ta);
        if (!ta->db.have_kernel) {
            ta->check_commands = 0;
            snprintf(ta->warning, sizeof ta->warning,
                     "could not find latex.ltx with %s; command checking is off",
                     ta->db.kpsewhich);
        }
    }
    return ta;
}

void ta_close(texaspell *ta)
{
    if (!ta)
        return;
    speller_close(ta->sp);
    cmddb_free(&ta->db);
    strset_free(&ta->words);
    strset_free(&ta->user_cmds);
    strset_free(&ta->user_envs);
    strset_free(&ta->user_keys);
    strset_free(&ta->packages);
    strset_free(&ta->recent_envs);
    strset_free(&ta->recent_keys);
    free(ta);
}

const char *ta_home(const texaspell *ta) { return ta->home; }
const char *ta_dictionary(const texaspell *ta) { return ta->dictionary; }
int ta_commands_enabled(const texaspell *ta) { return ta->check_commands; }
const char *ta_warning(const texaspell *ta) { return ta->warning[0] ? ta->warning : NULL; }

/* ------------------------------------------------------------------------
 * Checking
 * ---------------------------------------------------------------------- */

#define TOK_OPTION 100 /* alongside the ts_kind values */

typedef struct tok {
    int kind;
    size_t start, end;
    size_t toff, tlen;
    size_t coff, clen; /* TOK_OPTION: the command it was given to */
} tok;

typedef struct collect {
    texaspell *ta;
    tok *v;
    size_t n, cap;
    char *text;
    size_t tl, tcap;
    char **files;   /* packages/classes to load */
    size_t nfiles, fcap;
} collect;

static void add_token(collect *c, int kind, const char *t, size_t len,
                      const char *cmd, size_t clen, size_t start, size_t end)
{
    if (c->n == c->cap) {
        size_t ncap = c->cap ? c->cap * 2 : 256;
        tok *nv = realloc(c->v, ncap * sizeof *nv);
        if (!nv)
            return;
        c->v = nv;
        c->cap = ncap;
    }
    if (c->tl + len + clen > c->tcap) {
        size_t ncap = c->tcap ? c->tcap * 2 : 4096;
        while (ncap < c->tl + len + clen)
            ncap *= 2;
        char *nt = realloc(c->text, ncap);
        if (!nt)
            return;
        c->text = nt;
        c->tcap = ncap;
    }
    memcpy(c->text + c->tl, t, len);
    if (clen)
        memcpy(c->text + c->tl + len, cmd, clen);
    c->v[c->n++] = (tok){kind, start, end, c->tl, len, c->tl + len, clen};
    c->tl += len + clen;
}

static void on_token(void *ctx, ts_kind kind, const char *t, size_t len,
                     size_t start, size_t end)
{
    add_token(ctx, (int)kind, t, len, NULL, 0, start, end);
}

static void on_option(void *ctx, const char *cmd, size_t cmdlen,
                      const char *key, size_t keylen, size_t start, size_t end)
{
    add_token(ctx, TOK_OPTION, key, keylen, cmd, cmdlen, start, end);
}

static void add_user_name(texaspell *ta, int env, const char *name, size_t len)
{
    char line[300];
    if (len == 0 || len > 250)
        return;
    if (env) {
        if (cmddb_has_environment(&ta->db, name, len) ||
            strset_hasn(&ta->user_envs, name, len))
            return;
        strset_addn(&ta->user_envs, name, len);
        append_line(ta, "auto-commands.txt", name, len);
    } else {
        if (cmddb_has_command(&ta->db, name, len) ||
            strset_hasn(&ta->user_cmds, name, len))
            return;
        strset_addn(&ta->user_cmds, name, len);
        line[0] = '\\';
        memcpy(line + 1, name, len);
        append_line(ta, "auto-commands.txt", line, len + 1);
    }
}

static void on_doc_define(void *ctx, ts_def_kind kind, const char *name, size_t len)
{
    collect *c = ctx;
    texaspell *ta = c->ta;
    switch (kind) {
    case TS_DEF_COMMAND:
        add_user_name(ta, 0, name, len);
        break;
    case TS_DEF_ENVIRONMENT:
        add_user_name(ta, 1, name, len);
        break;
    case TS_DEF_COUNTER: {
        char buf[256];
        if (len + 4 < sizeof buf) {
            memcpy(buf, "the", 3);
            memcpy(buf + 3, name, len);
            add_user_name(ta, 0, buf, len + 3);
        }
        break;
    }
    case TS_DEF_KEY:
        break;
    case TS_DEF_PACKAGE:
    case TS_DEF_CLASS: {
        if (len > 200)
            return;
        if (c->nfiles == c->fcap) {
            size_t ncap = c->fcap ? c->fcap * 2 : 16;
            char **nf = realloc(c->files, ncap * sizeof *nf);
            if (!nf)
                return;
            c->files = nf;
            c->fcap = ncap;
        }
        char *f = malloc(len + 5);
        if (!f)
            return;
        memcpy(f, name, len);
        memcpy(f + len, kind == TS_DEF_CLASS ? ".cls" : ".sty", 5);
        c->files[c->nfiles++] = f;
        break;
    }
    }
}

/* Remember packages the document loads, so their commands are accepted
 * even when a later check sees only part of the document. */
static void load_document_packages(texaspell *ta, collect *c)
{
    if (!c->nfiles || !ta->check_commands)
        return;
    int *found = calloc(c->nfiles, sizeof *found);
    if (!found)
        return;
    cmddb_load(&ta->db, (const char *const *)c->files, c->nfiles, found);
    for (size_t i = 0; i < c->nfiles; i++) {
        if (!found[i])
            continue;
        char entry[256];
        size_t l = strlen(c->files[i]);
        snprintf(entry, sizeof entry, "%s", c->files[i]);
        if (l > 4 && strcmp(entry + l - 4, ".sty") == 0)
            entry[l - 4] = '\0';
        if (strset_add(&ta->packages, entry) == 1)
            append_line(ta, "packages.txt", entry, strlen(entry));
    }
    free(found);
}

static size_t utf8_chars(const char *s, size_t len)
{
    size_t n = 0;
    for (size_t i = 0; i < len; i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80)
            n++;
    return n;
}

static int is_allcaps(const char *w, size_t len)
{
    size_t upper = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)w[i];
        if (c >= 'A' && c <= 'Z') {
            upper++;
            continue;
        }
        /* plural acronyms: MPs, NGOs */
        if (c == 's' && i == len - 1 && upper >= 2)
            continue;
        return 0;
    }
    return upper >= 2;
}

static int word_ok(texaspell *ta, const char *w, size_t len)
{
    if ((int)utf8_chars(w, len) < ta->min_word_length)
        return 1;
    if (ta->skip_allcaps && is_allcaps(w, len))
        return 1;
    if (strset_hasn(&ta->words, w, len))
        return 1;
    if (len < 256 && w[0] >= 'A' && w[0] <= 'Z') {
        char buf[256];
        memcpy(buf, w, len);
        buf[0] = (char)(buf[0] - 'A' + 'a');
        if (strset_hasn(&ta->words, buf, len))
            return 1;
    }
    return speller_check(ta->sp, w, len);
}

static int command_ok(texaspell *ta, const char *name, size_t len)
{
    return cmddb_has_command(&ta->db, name, len) ||
           strset_hasn(&ta->user_cmds, name, len);
}

/* The package whose keys a setup command takes. Keys are only checked when
 * that package has been read, so a document that never loads it is not
 * buried in complaints. */
static const char *key_package(const char *cmd, size_t len)
{
    static const struct { const char *cmd, *pkg; } map[] = {
        {"hypersetup", "hyperref.sty"}, {"geometry", "geometry.sty"},
        {"newgeometry", "geometry.sty"}, {"captionsetup", "caption.sty"},
        {"lstset", "listings.sty"}, {"sisetup", "siunitx.sty"},
        {"setlist", "enumitem.sty"},
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (strlen(map[i].cmd) == len && memcmp(map[i].cmd, cmd, len) == 0)
            return map[i].pkg;
    return NULL;
}

static int option_ok(texaspell *ta, const char *cmd, size_t clen,
                     const char *key, size_t len)
{
    const char *pkg = key_package(cmd, clen);
    if (!pkg || !cmddb_loaded(&ta->db, pkg))
        return 1;
    return cmddb_has_key(&ta->db, key, len) || strset_hasn(&ta->user_keys, key, len);
}

static int environment_ok(texaspell *ta, const char *name, size_t len)
{
    if (cmddb_has_environment(&ta->db, name, len) ||
        strset_hasn(&ta->user_envs, name, len))
        return 1;
    if (len > 1 && name[len - 1] == '*' && strset_hasn(&ta->user_envs, name, len - 1))
        return 1;
    return 0;
}

size_t ta_check(texaspell *ta, const char *text, size_t len,
                ta_issue **out, size_t *nwords)
{
    collect c = {0};
    c.ta = ta;
    ts_callbacks cb = {&c, on_token, on_doc_define, on_option};
    ts_scan(text, len, &cb);

    load_document_packages(ta, &c);

    ta_issue *issues = NULL;
    size_t ni = 0, icap = 0;
    for (size_t i = 0; i < c.n; i++) {
        const tok *t = &c.v[i];
        const char *w = c.text + t->toff;
        int ok = 1;
        ta_kind kind = TA_WORD;
        switch (t->kind) {
        case TS_WORD:
            ok = word_ok(ta, w, t->tlen);
            kind = TA_WORD;
            break;
        case TS_COMMAND:
            ok = !ta->check_commands || command_ok(ta, w, t->tlen);
            kind = TA_COMMAND;
            break;
        case TS_ENVIRONMENT:
            ok = !ta->check_commands || environment_ok(ta, w, t->tlen);
            kind = TA_ENVIRONMENT;
            if (!ok) {
                if (ta->recent_envs.count > MAX_RECENT_ENVS)
                    strset_free(&ta->recent_envs);
                strset_addn(&ta->recent_envs, w, t->tlen);
            }
            break;
        case TOK_OPTION:
            ok = !ta->check_commands ||
                 option_ok(ta, c.text + t->coff, t->clen, w, t->tlen);
            kind = TA_OPTION;
            if (!ok) {
                if (ta->recent_keys.count > MAX_RECENT_ENVS)
                    strset_free(&ta->recent_keys);
                strset_addn(&ta->recent_keys, w, t->tlen);
            }
            break;
        }
        if (ok)
            continue;
        if (ni == icap) {
            size_t ncap = icap ? icap * 2 : 16;
            ta_issue *n2 = realloc(issues, ncap * sizeof *n2);
            if (!n2)
                break;
            issues = n2;
            icap = ncap;
        }
        issues[ni++] = (ta_issue){t->start, t->end, kind};
    }

    if (nwords)
        *nwords = c.n;
    free(c.v);
    free(c.text);
    for (size_t i = 0; i < c.nfiles; i++)
        free(c.files[i]);
    free(c.files);
    *out = issues;
    return ni;
}

/* ------------------------------------------------------------------------
 * Suggestions and learning
 * ---------------------------------------------------------------------- */

size_t ta_suggest(texaspell *ta, const char *word, size_t len, char **out, size_t max)
{
    if (len > 1 && word[0] == '\\') {
        size_t n = cmddb_suggest(&ta->db, &ta->user_cmds, CMDDB_COMMANDS,
                                 word + 1, len - 1, out, max);
        for (size_t i = 0; i < n; i++) {
            size_t l = strlen(out[i]);
            char *s = malloc(l + 2);
            if (!s)
                continue;
            s[0] = '\\';
            memcpy(s + 1, out[i], l + 1);
            free(out[i]);
            out[i] = s;
        }
        return n;
    }
    if (strset_hasn(&ta->recent_envs, word, len))
        return cmddb_suggest(&ta->db, &ta->user_envs, CMDDB_ENVIRONMENTS,
                             word, len, out, max);
    if (strset_hasn(&ta->recent_keys, word, len))
        return cmddb_suggest(&ta->db, &ta->user_keys, CMDDB_KEYS,
                             word, len, out, max);
    return speller_suggest(ta->sp, word, len, out, max);
}

void ta_learn(texaspell *ta, const char *word, size_t len)
{
    if (len == 0 || len > 250 || memchr(word, '\n', len))
        return;
    if (len > 1 && word[0] == '\\') {
        strset_addn(&ta->user_cmds, word + 1, len - 1);
        append_line(ta, "commands.txt", word, len);
    } else if (strset_hasn(&ta->recent_envs, word, len)) {
        strset_addn(&ta->user_envs, word, len);
        append_line(ta, "commands.txt", word, len);
    } else if (strset_hasn(&ta->recent_keys, word, len)) {
        char line[260];
        strset_addn(&ta->user_keys, word, len);
        memcpy(line, word, len);
        line[len] = '=';
        append_line(ta, "commands.txt", line, len + 1);
    } else {
        strset_addn(&ta->words, word, len);
        append_line(ta, "words.txt", word, len);
    }
}

void ta_forget(texaspell *ta, const char *word, size_t len)
{
    if (len == 0 || len > 250)
        return;
    remove_line(ta, "words.txt", word, len);
    remove_line(ta, "commands.txt", word, len);
    remove_line(ta, "auto-commands.txt", word, len);
    char line[260];
    memcpy(line, word, len);
    line[len] = '=';
    remove_line(ta, "commands.txt", line, len + 1);
    load_user_lists(ta);
}

void ta_utf16_map(const char *s, size_t len, size_t *map)
{
    size_t u = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        map[i] = u;
        if ((c & 0xC0) == 0x80)
            continue;            /* continuation byte: same character */
        u += (c >= 0xF0) ? 2 : 1; /* 4-byte sequences are surrogate pairs */
    }
    map[len] = u;
}
