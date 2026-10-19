/* checker.c - the TeXAspell engine. See checker.h. */
#include "checker.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cmddb.h"
#include "languages.h"
#include "scan.h"
#include "speller.h"
#include "strset.h"

#define OPTION_PACKAGE_COUNT 6
#define PACKAGE_CACHE 256    /* packages whose command lists are kept     */
#define MAX_RECENT_DOCS 32   /* upper bound for remember_documents        */
#define KIND_HISTORY 8       /* checks whose token categories are kept    */
#define MAX_LANGS 32
#define DEFAULT_LANGUAGE "en_GB"

/* A language other than the default, and its aspell dictionary. */
typedef struct langdict {
    char lang[32];
    char dict[128];
    speller *sp;    /* opened on first use */
    int failed;     /* the dictionary could not be opened */
} langdict;

/* Flagged words and the dictionary that flagged them, so suggestions come
 * from the right language. */
#define RECENT_WORDS 256
typedef struct recent_word {
    char word[64];
    speller *sp;
} recent_word;

/* One package (or class) read on its own, with everything it loads. */
typedef struct package_entry {
    char *name;
    cmddb *db;     /* the package and what it loads, alone: for its keys */
    cmddb *added;  /* what it adds to the base vocabulary: for documents */
} package_entry;

/* What a document checked in full loads and defines. Text checked without
 * its preamble (as while typing) also accepts the names of recent
 * documents. */
typedef struct doc_context {
    strset packages, cmds, envs;
} doc_context;

struct texaspell {
    char home[1024];
    char dictionary[128];
    char kpsewhich[1024];
    char default_classes[512];
    int check_commands;
    int skip_allcaps;
    int min_word_length;
    char warning[1200];

    speller *sp;          /* dictionary in use       */
    speller *default_sp;  /* dictionary for en_GB    */
    langdict langs[MAX_LANGS];
    int nlangs;
    cmddb db;
    cmddb base_db;       /* only explicitly configured packages/classes */
    package_entry pkgcache[PACKAGE_CACHE];
    int npkgcache;
    doc_context recent_docs[MAX_RECENT_DOCS]; /* newest first */
    int nrecent_docs;
    int remember_documents;  /* config: how many documents to remember */
    cmddb fragment_db;       /* base + recent documents' packages        */
    int fragment_db_valid;
    strset fragment_cmds, fragment_envs; /* recent documents' definitions */
    int is_fragment;         /* the current request has no \documentclass */
    strset inferred_cmds, inferred_envs;
    /* token categories of recent checks (kind+1, option owner in high
     * bits), newest at kinds_next */
    strset kinds[KIND_HISTORY];
    int kinds_next;
    strset words;        /* words.txt                                   */
    strset user_cmds;    /* explicitly learned commands from commands.txt */
    strset user_envs;
    strset user_keys;
    strset packages;     /* names listed in packages.txt                */
    strset inputs_done;  /* cycle guard for this request only            */
    char doc_dir[1024];  /* folder of the document being checked, if known */
    char input_dirs[2048]; /* config: more folders for \input files       */
    char doc_lang[32];   /* main language in the current request          */
    recent_word recent_words[RECENT_WORDS];
    int recent_next;
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
    "# Other languages offered in the Spelling panel use the aspell dictionary\n"
    "# of the same name (en_US, de_DE, fr_FR, ...). To use a different one:\n"
    "#   dictionary.en_US = en_US-variant_1\n"
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
    "default_classes = article report book\n"
    "\n"
    "# Folders to search for files your documents \\input (such as a shared\n"
    "# preamble), separated by colons. The spell service is not told where\n"
    "# your document is, so list the folders here. Files in your personal TeX\n"
    "# tree (~/Library/texmf/tex/latex) are found without this.\n"
    "#   input_dirs = ~/Documents/LaTeX:~/Work/Preambles\n"
    "input_dirs =\n"
    "\n"
    "# Text checked without its preamble (as while typing) also accepts the\n"
    "# packages and \\newcommand definitions of this many documents most\n"
    "# recently checked in full. 0 turns this off.\n"
    "remember_documents = 8\n";

static const char default_packages[] =
    "# Packages whose commands are always accepted, one per line.\n"
    "# Add packages you want accepted even in partial text.\n"
    "# A name ending in .cls is a document class.\n"
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
    "# Legacy file, no longer read or populated automatically.\n"
    "# Move deliberately trusted entries to commands.txt.\n";

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
    if (strcmp(key, "dictionary") == 0) {
        snprintf(ta->dictionary, sizeof ta->dictionary, "%s", val);
    } else if (strncmp(key, "dictionary.", 11) == 0 && key[11] &&
               ta->nlangs < MAX_LANGS) {
        langdict *l = &ta->langs[ta->nlangs++];
        snprintf(l->lang, sizeof l->lang, "%s", key + 11);
        snprintf(l->dict, sizeof l->dict, "%s", val);
        l->sp = NULL;
    }
    else if (strcmp(key, "check_commands") == 0)
        ta->check_commands = yes(val);
    else if (strcmp(key, "skip_allcaps") == 0)
        ta->skip_allcaps = yes(val);
    else if (strcmp(key, "min_word_length") == 0)
        ta->min_word_length = atoi(val);
    else if (strcmp(key, "kpsewhich") == 0)
        snprintf(ta->kpsewhich, sizeof ta->kpsewhich, "%s", val);
    else if (strcmp(key, "remember_documents") == 0) {
        ta->remember_documents = atoi(val);
        if (ta->remember_documents < 0) ta->remember_documents = 0;
        if (ta->remember_documents > MAX_RECENT_DOCS) ta->remember_documents = MAX_RECENT_DOCS;
    }
    else if (strcmp(key, "input_dirs") == 0)
        snprintf(ta->input_dirs, sizeof ta->input_dirs, "%s", val);
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
    /* Legacy auto-commands.txt is deliberately not an implicit allowlist. */
}

/* ------------------------------------------------------------------------
 * Recent documents (recent-documents.txt)
 *
 *   document
 *   package biblatex.sty
 *   command dept
 *   environment keypoint
 *   document
 *   ...
 * ---------------------------------------------------------------------- */

static const char recent_docs_header[] =
    "# Packages and definitions of the documents TeXAspell most recently\n"
    "# checked in full, newest first. Text checked without its preamble (as\n"
    "# while typing) also accepts these. Written automatically; safe to delete.\n";

static void free_doc_context(doc_context *d)
{
    strset_free(&d->packages);
    strset_free(&d->cmds);
    strset_free(&d->envs);
}

static void load_recent_docs(texaspell *ta)
{
    char path[1100];
    path_join(path, sizeof path, ta->home, "recent-documents.txt");
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char buf[1024];
    doc_context *d = NULL;
    while (fgets(buf, sizeof buf, f)) {
        size_t l = strlen(buf);
        while (l && (buf[l - 1] == '\n' || buf[l - 1] == '\r' || buf[l - 1] == ' '))
            buf[--l] = '\0';
        if (!l || buf[0] == '#')
            continue;
        if (strcmp(buf, "document") == 0) {
            if (ta->nrecent_docs >= ta->remember_documents) {
                d = NULL;
                continue;
            }
            d = &ta->recent_docs[ta->nrecent_docs++];
            memset(d, 0, sizeof *d);
        } else if (d && strncmp(buf, "package ", 8) == 0) {
            strset_add(&d->packages, buf + 8);
        } else if (d && strncmp(buf, "command ", 8) == 0) {
            strset_add(&d->cmds, buf + 8);
        } else if (d && strncmp(buf, "environment ", 12) == 0) {
            strset_add(&d->envs, buf + 12);
        }
    }
    fclose(f);
}

static void write_set(FILE *f, const char *label, const strset *s)
{
    for (size_t i = 0; i < s->cap; i++)
        if (s->slots[i])
            fprintf(f, "%s %s\n", label, s->slots[i]);
}

static void save_recent_docs(const texaspell *ta)
{
    char path[1100], tmp[1110];
    path_join(path, sizeof path, ta->home, "recent-documents.txt");
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f)
        return;
    fputs(recent_docs_header, f);
    for (int i = 0; i < ta->nrecent_docs; i++) {
        const doc_context *d = &ta->recent_docs[i];
        fputs("document\n", f);
        write_set(f, "package", &d->packages);
        write_set(f, "command", &d->cmds);
        write_set(f, "environment", &d->envs);
    }
    if (fclose(f) == 0)
        rename(tmp, path);
    else
        remove(tmp);
}

static int sets_equal(const strset *a, const strset *b)
{
    if (a->count != b->count)
        return 0;
    for (size_t i = 0; i < a->cap; i++)
        if (a->slots[i] && !strset_has(b, a->slots[i]))
            return 0;
    return 1;
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
    ta->remember_documents = 8;

    strset_init(&ta->words);
    strset_init(&ta->user_cmds);
    strset_init(&ta->user_envs);
    strset_init(&ta->user_keys);
    strset_init(&ta->packages);
    strset_init(&ta->inputs_done);

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
    load_recent_docs(ta);

    char aerr[512] = "";
    ta->default_sp = ta->sp = speller_open(ta->dictionary, aerr, sizeof aerr);
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
    if (!cmddb_copy(&ta->base_db, &ta->db)) {
        if (err) snprintf(err, errlen, "out of memory copying command database");
        ta_close(ta);
        return NULL;
    }
    return ta;
}

void ta_close(texaspell *ta)
{
    if (!ta)
        return;
    speller_close(ta->default_sp);
    for (int i = 0; i < ta->nlangs; i++)
        speller_close(ta->langs[i].sp);
    cmddb_free(&ta->db);
    cmddb_free(&ta->base_db);
    for (int i = 0; i < ta->npkgcache; i++) {
        free(ta->pkgcache[i].name);
        for (int k = 0; k < 2; k++) {
            cmddb *db = k ? ta->pkgcache[i].added : ta->pkgcache[i].db;
            if (db) cmddb_free(db);
            free(db);
        }
    }
    for (int i = 0; i < ta->nrecent_docs; i++) {
        strset_free(&ta->recent_docs[i].packages);
        strset_free(&ta->recent_docs[i].cmds);
        strset_free(&ta->recent_docs[i].envs);
    }
    if (ta->fragment_db_valid)
        cmddb_free(&ta->fragment_db);
    strset_free(&ta->fragment_cmds);
    strset_free(&ta->fragment_envs);
    strset_free(&ta->inferred_cmds);
    strset_free(&ta->inferred_envs);
    for (int i = 0; i < KIND_HISTORY; i++)
        strset_free(&ta->kinds[i]);
    strset_free(&ta->words);
    strset_free(&ta->user_cmds);
    strset_free(&ta->user_envs);
    strset_free(&ta->user_keys);
    strset_free(&ta->packages);
    strset_free(&ta->inputs_done);
    free(ta);
}

const char *ta_home(const texaspell *ta) { return ta->home; }

void ta_set_document_dir(texaspell *ta, const char *dir)
{
    snprintf(ta->doc_dir, sizeof ta->doc_dir, "%s", dir ? dir : "");
}

void ta_describe(const texaspell *ta, char *out, size_t outlen)
{
    snprintf(out, outlen,
             "kpsewhich:       %s\n"
             "LaTeX kernel:    %s\n"
             "files read:      %zu\n"
             "commands known:  %zu\n"
             "input_dirs:      %s\n",
             ta->db.kpsewhich, ta->db.have_kernel ? "found" : "NOT FOUND",
             ta->db.files_read, cmddb_known_commands(&ta->db),
             ta->input_dirs[0] ? ta->input_dirs : "(none)");
}
const char *ta_dictionary(const texaspell *ta) { return ta->dictionary; }

/* The speller for an aspell language code; NULL if its dictionary cannot be
 * opened (the failure is remembered, and described in err). */
static speller *speller_for_code(texaspell *ta, const char *lang, char *err, size_t errlen)
{
    if (!lang || !*lang || strcmp(lang, DEFAULT_LANGUAGE) == 0)
        return ta->default_sp;
    langdict *l = NULL;
    for (int i = 0; i < ta->nlangs; i++)
        if (strcmp(ta->langs[i].lang, lang) == 0)
            l = &ta->langs[i];
    if (!l) {
        if (ta->nlangs >= MAX_LANGS) {
            if (err) snprintf(err, errlen, "too many languages");
            return NULL;
        }
        l = &ta->langs[ta->nlangs++];
        snprintf(l->lang, sizeof l->lang, "%s", lang);
        snprintf(l->dict, sizeof l->dict, "%s", lang);
        l->sp = NULL;
        l->failed = 0;
    }
    if (!l->sp && !l->failed) {
        char aerr[512] = "";
        l->sp = speller_open(l->dict, aerr, sizeof aerr);
        if (!l->sp) {
            l->failed = 1;
            if (err)
                snprintf(err, errlen, "cannot open aspell dictionary \"%s\" for %s (%s)",
                         l->dict, lang, aerr);
        }
    } else if (!l->sp && err) {
        snprintf(err, errlen, "no aspell dictionary \"%s\" for %s", l->dict, lang);
    }
    return l->sp;
}

int ta_use_language(texaspell *ta, const char *lang, char *err, size_t errlen)
{
    speller *sp = speller_for_code(ta, lang, err, errlen);
    if (!sp)
        return 0;
    ta->sp = sp;
    return 1;
}

/* The speller for words in a babel/polyglossia language ("" = the
 * document's main language, or failing that the chosen default). NULL means
 * do not check: an unknown language, or no dictionary installed. */
static speller *speller_for_text(texaspell *ta, const char *babel_name)
{
    if (!*babel_name)
        babel_name = ta->doc_lang;
    if (!*babel_name)
        return ta->sp;
    const char *code = lang_code(babel_name, strlen(babel_name));
    if (!code)
        return NULL;
    if (!*code)
        return ta->sp;
    return speller_for_code(ta, code, NULL, 0);
}
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
    char main_lang[32];
    int have_main_lang;
    int saw_class;
    size_t nfiles, fcap;
    char **inputs;  /* \input / \include file names */
    size_t ninputs, incap;
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
    if (len)
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

static void add_inferred_name(texaspell *ta, int env, const char *name, size_t len)
{
    if (len == 0 || len > 250) return;
    strset_addn(env ? &ta->inferred_envs : &ta->inferred_cmds, name, len);
}

static void on_doc_define(void *ctx, ts_def_kind kind, const char *name, size_t len)
{
    collect *c = ctx;
    texaspell *ta = c->ta;
    switch (kind) {
    case TS_DEF_MENTION:
        break;
    case TS_DEF_COMMAND:
        add_inferred_name(ta, 0, name, len);
        break;
    case TS_DEF_ENVIRONMENT:
        add_inferred_name(ta, 1, name, len);
        break;
    case TS_DEF_COUNTER: {
        char buf[256];
        if (len + 4 < sizeof buf) {
            memcpy(buf, "the", 3);
            memcpy(buf + 3, name, len);
            add_inferred_name(ta, 0, buf, len + 3);
        }
        break;
    }
    case TS_DEF_KEY:
        break;
    case TS_DEF_INPUT: {
        if (len > 500 || c->ninputs >= 200)
            return;
        if (c->ninputs == c->incap) {
            size_t ncap = c->incap ? c->incap * 2 : 8;
            char **ni = realloc(c->inputs, ncap * sizeof *ni);
            if (!ni)
                return;
            c->inputs = ni;
            c->incap = ncap;
        }
        char *f = malloc(len + 1);
        if (!f)
            return;
        memcpy(f, name, len);
        f[len] = '\0';
        c->inputs[c->ninputs++] = f;
        break;
    }
    case TS_DEF_MAIN_LANGUAGE:
        if (len < sizeof c->main_lang) {
            memcpy(c->main_lang, name, len);
            c->main_lang[len] = '\0';
            c->have_main_lang = 1;
        }
        break;
    case TS_DEF_PACKAGE:
    case TS_DEF_CLASS: {
        if (kind == TS_DEF_CLASS)
            c->saw_class = 1;
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

static char *try_path(const char *dir, const char *name, const char *ext)
{
    char path[3200];
    if (dir)
        snprintf(path, sizeof path, "%s/%s%s", dir, name, ext);
    else
        snprintf(path, sizeof path, "%s%s", name, ext);
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fclose(f);
    return strdup(path);
}

/* Find an \input file: in the document's folder, the input_dirs from
 * config.txt, then the TeX installation (e.g. ~/Library/texmf). */
static char *find_input(texaspell *ta, const char *name)
{
    static const char *const exts[] = {"", ".tex"};
    for (int e = 0; e < 2; e++) {
        char *p = NULL;
        if (name[0] == '/')
            p = try_path(NULL, name, exts[e]);
        if (!p && ta->doc_dir[0])
            p = try_path(ta->doc_dir, name, exts[e]);
        if (!p && ta->input_dirs[0]) {
            char dirs[2048];
            snprintf(dirs, sizeof dirs, "%s", ta->input_dirs);
            for (char *d = strtok(dirs, ":"); d && !p; d = strtok(NULL, ":")) {
                char full[2048];
                while (*d == ' ')
                    d++;
                if (d[0] == '~' && getenv("HOME"))
                    snprintf(full, sizeof full, "%s%s", getenv("HOME"), d + 1);
                else
                    snprintf(full, sizeof full, "%s", d);
                p = try_path(full, name, exts[e]);
            }
        }
        if (p)
            return p;
    }
    if (!strchr(name, '/') && ta->check_commands) {
        char buf[600];
        for (int e = 0; e < 2; e++) {
            snprintf(buf, sizeof buf, "%s%s", name, exts[e]);
            char *p = cmddb_find_file(&ta->db, buf);
            if (p)
                return p;
        }
    }
    return NULL;
}

/* Re-read included definitions on each check. Canonical paths bound cycles
 * within a request; there is no stale cross-document or negative cache. */
static void follow_inputs(texaspell *ta, collect *c)
{
    ts_callbacks cb = {c, NULL, on_doc_define, NULL, NULL};
    int files = 0;
    for (size_t i = 0; i < c->ninputs && files < 50; i++) {
        const char *name = c->inputs[i];
        char *path = find_input(ta, name);
        if (!path) continue; /* missing files are retried next check */
        char *canonical = realpath(path, NULL);
        if (strset_add(&ta->inputs_done, canonical ? canonical : path) != 1) {
            free(canonical);
            free(path);
            continue;
        }
        free(canonical);
        size_t len = 0;
        char *text = cmddb_read_file(path, &len);
        free(path);
        if (!text)
            continue;
        ts_scan(text, len, &cb);
        free(text);
        files++;
    }
}

/* The cache entry for a package, created if need be. NULL if the cache
 * is full. */
static package_entry *package_entry_for(texaspell *ta, const char *name)
{
    for (int i = 0; i < ta->npkgcache; i++)
        if (strcmp(ta->pkgcache[i].name, name) == 0)
            return &ta->pkgcache[i];
    if (ta->npkgcache >= PACKAGE_CACHE)
        return NULL;
    char *copy = strdup(name);
    if (!copy)
        return NULL;
    package_entry *e = &ta->pkgcache[ta->npkgcache++];
    memset(e, 0, sizeof *e);
    e->name = copy;
    return e;
}

/* The command list of one package (or class) and everything it loads,
 * read on its own, once per session. NULL if the cache is full. */
static const cmddb *package_db(texaspell *ta, const char *name)
{
    package_entry *e = package_entry_for(ta, name);
    if (!e)
        return NULL;
    if (!e->db) {
        cmddb *db = calloc(1, sizeof *db);
        if (!db)
            return NULL;
        cmddb_init(db, ta->kpsewhich);
        cmddb_load(db, (const char *const *)&e->name, 1, NULL);
        e->db = db;
    }
    return e->db;
}

/* What loading a package adds to the base vocabulary, read once per
 * session: the files it reads that the base does not, and the commands
 * they mention, counted as a fresh load would count them. */
static const cmddb *package_additions(texaspell *ta, const char *name)
{
    package_entry *e = package_entry_for(ta, name);
    if (!e)
        return NULL;
    if (!e->added) {
        cmddb full, *added = calloc(1, sizeof *added);
        if (!added)
            return NULL;
        if (!cmddb_copy(&full, &ta->base_db)) {
            free(added);
            return NULL;
        }
        cmddb_load(&full, (const char *const *)&e->name, 1, NULL);
        int ok = cmddb_difference(added, &full, &ta->base_db);
        cmddb_free(&full);
        if (!ok) {
            free(added);
            return NULL;
        }
        e->added = added;
    }
    return e->added;
}

/* Add a package's commands to this request's working copy. */
static void add_package(texaspell *ta, const char *name)
{
    if (cmddb_loaded(&ta->db, name))
        return;
    const cmddb *pdb = package_additions(ta, name);
    if (!pdb || !cmddb_merge(&ta->db, pdb))
        cmddb_load(&ta->db, &name, 1, NULL);
}

/* Load packages into this request's working copy of the base vocabulary. */
static void load_document_packages(texaspell *ta, collect *c)
{
    if (!ta->check_commands)
        return;
    for (size_t i = 0; i < c->nfiles; i++)
        add_package(ta, c->files[i]);
}

/* The vocabulary for text without a preamble: the base packages plus the
 * packages of recent full documents. Built when first needed after the
 * recent documents change. */
static const cmddb *fragment_base(texaspell *ta)
{
    if (ta->fragment_db_valid)
        return &ta->fragment_db;
    if (!cmddb_copy(&ta->fragment_db, &ta->base_db))
        return &ta->base_db;
    ta->fragment_db_valid = 1;
    cmddb saved = ta->db;   /* add_package works on ta->db */
    ta->db = ta->fragment_db;
    strset_free(&ta->fragment_cmds);
    strset_free(&ta->fragment_envs);
    for (int d = 0; d < ta->nrecent_docs; d++) {
        const doc_context *dc = &ta->recent_docs[d];
        for (size_t i = 0; i < dc->packages.cap; i++)
            if (dc->packages.slots[i] && ta->check_commands)
                add_package(ta, dc->packages.slots[i]);
        for (size_t i = 0; i < dc->cmds.cap; i++)
            if (dc->cmds.slots[i])
                strset_add(&ta->fragment_cmds, dc->cmds.slots[i]);
        for (size_t i = 0; i < dc->envs.cap; i++)
            if (dc->envs.slots[i])
                strset_add(&ta->fragment_envs, dc->envs.slots[i]);
    }
    ta->fragment_db = ta->db;
    ta->db = saved;
    return &ta->fragment_db;
}

/* After a full-document check, remember what it loaded and defined. */
static void remember_document(texaspell *ta, const collect *c)
{
    if (ta->remember_documents <= 0)
        return;
    doc_context d;
    memset(&d, 0, sizeof d);
    for (size_t i = 0; i < c->nfiles; i++)
        if (cmddb_loaded(&ta->db, c->files[i]))
            strset_add(&d.packages, c->files[i]);
    for (size_t i = 0; i < ta->inferred_cmds.cap; i++)
        if (ta->inferred_cmds.slots[i])
            strset_add(&d.cmds, ta->inferred_cmds.slots[i]);
    for (size_t i = 0; i < ta->inferred_envs.cap; i++)
        if (ta->inferred_envs.slots[i])
            strset_add(&d.envs, ta->inferred_envs.slots[i]);

    /* Already the newest? Nothing to do. Elsewhere in the list? Move it. */
    int found = -1;
    for (int i = 0; i < ta->nrecent_docs; i++) {
        const doc_context *o = &ta->recent_docs[i];
        if (sets_equal(&o->packages, &d.packages) && sets_equal(&o->cmds, &d.cmds) &&
            sets_equal(&o->envs, &d.envs)) {
            found = i;
            break;
        }
    }
    if (found == 0) {
        free_doc_context(&d);
        return;
    }
    if (found > 0) {
        free_doc_context(&d);
        d = ta->recent_docs[found];
        memmove(&ta->recent_docs[1], &ta->recent_docs[0], found * sizeof d);
    } else {
        if (ta->nrecent_docs >= ta->remember_documents)
            free_doc_context(&ta->recent_docs[--ta->nrecent_docs]);
        memmove(&ta->recent_docs[1], &ta->recent_docs[0], ta->nrecent_docs * sizeof d);
        ta->nrecent_docs++;
        /* the set of remembered names changed */
        if (ta->fragment_db_valid) {
            cmddb_free(&ta->fragment_db);
            ta->fragment_db_valid = 0;
        }
    }
    ta->recent_docs[0] = d;
    save_recent_docs(ta);
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
        /* possessives: NDA's (the scanner turns ’ into ') */
        if (c == '\'' && i == len - 2 && w[len - 1] == 's' && upper >= 2)
            return 1;
        return 0;
    }
    return upper >= 2;
}

static int word_ok(texaspell *ta, speller *sp, const char *w, size_t len)
{
    if (!sp)
        return 1;
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
    return speller_check(sp, w, len);
}

static void remember_word(texaspell *ta, speller *sp, const char *w, size_t len)
{
    if (len >= sizeof ta->recent_words[0].word)
        return;
    recent_word *r = &ta->recent_words[ta->recent_next];
    memcpy(r->word, w, len);
    r->word[len] = '\0';
    r->sp = sp;
    ta->recent_next = (ta->recent_next + 1) % RECENT_WORDS;
}

static int command_ok(texaspell *ta, const char *name, size_t len)
{
    return cmddb_has_command(&ta->db, name, len) ||
           strset_hasn(&ta->user_cmds, name, len) ||
           strset_hasn(&ta->inferred_cmds, name, len) ||
           (ta->is_fragment && strset_hasn(&ta->fragment_cmds, name, len));
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

static const char *const option_packages[] = {
    "hyperref.sty", "geometry.sty", "caption.sty", "listings.sty",
    "siunitx.sty", "enumitem.sty", NULL
};

static int option_index(const char *pkg)
{
    for (int i = 0; option_packages[i]; i++)
        if (pkg && strcmp(pkg, option_packages[i]) == 0) return i;
    return -1;
}

static const cmddb *option_db(texaspell *ta, int index)
{
    if (index < 0 || index >= OPTION_PACKAGE_COUNT) return &ta->db;
    const cmddb *db = package_db(ta, option_packages[index]);
    return db ? db : &ta->db;
}

static int option_ok(texaspell *ta, const char *cmd, size_t clen,
                     const char *key, size_t len)
{
    const char *pkg = key_package(cmd, clen);
    if (!pkg || !cmddb_loaded(&ta->db, pkg))
        return 1;
    const cmddb *db = option_db(ta, option_index(pkg));
    return cmddb_has_key(db, key, len) || strset_hasn(&ta->user_keys, key, len);
}

static int environment_ok(texaspell *ta, const char *name, size_t len)
{
    if (cmddb_has_environment(&ta->db, name, len) ||
        strset_hasn(&ta->user_envs, name, len) ||
        strset_hasn(&ta->inferred_envs, name, len) ||
        (ta->is_fragment && strset_hasn(&ta->fragment_envs, name, len)))
        return 1;
    if (len > 1 && name[len - 1] == '*' &&
        (strset_hasn(&ta->user_envs, name, len - 1) ||
         strset_hasn(&ta->inferred_envs, name, len - 1) ||
         (ta->is_fragment && strset_hasn(&ta->fragment_envs, name, len - 1))))
        return 1;
    return 0;
}

/* A lone line such as "colorlinks=true, linkcolor=blue," while typing:
 * is the name an option of a setup package this context loads? */
static int on_is_option_key(void *ctx, const char *key, size_t len)
{
    texaspell *ta = ((collect *)ctx)->ta;
    if (!ta->check_commands)
        return 0;
    const cmddb *ctxdb = fragment_base(ta);
    for (int i = 0; option_packages[i]; i++) {
        if (!cmddb_loaded(ctxdb, option_packages[i]))
            continue;
        const cmddb *db = package_db(ta, option_packages[i]);
        if (db && cmddb_has_key(db, key, len))
            return 1;
    }
    return 0;
}

size_t ta_check(texaspell *ta, const char *text, size_t len,
                ta_issue **out, size_t *nwords)
{
    strset_free(&ta->inferred_cmds);
    strset_free(&ta->inferred_envs);
    strset_free(&ta->inputs_done);
    ta->kinds_next = (ta->kinds_next + 1) % KIND_HISTORY;
    strset *kinds = &ta->kinds[ta->kinds_next];
    strset_free(kinds);
    ta->doc_lang[0] = '\0';
    collect c = {0};
    c.ta = ta;
    ts_callbacks cb = {&c, on_token, on_doc_define, on_option, on_is_option_key};
    ts_scan(text, len, &cb);
    follow_inputs(ta, &c);

    /* A full document (it has \documentclass) is checked against its own
     * packages and definitions only. Text without a preamble, as sent
     * while typing, also accepts those of recent full documents. */
    ta->is_fragment = !c.saw_class;
    cmddb fresh;
    if (!cmddb_copy(&fresh, ta->is_fragment ? fragment_base(ta) : &ta->base_db)) {
        snprintf(ta->warning, sizeof ta->warning, "out of memory preparing check");
        *out = NULL;
        if (nwords) *nwords = 0;
        free(c.v);
        free(c.text);
        for (size_t i = 0; i < c.nfiles; i++)
            free(c.files[i]);
        free(c.files);
        for (size_t i = 0; i < c.ninputs; i++)
            free(c.inputs[i]);
        free(c.inputs);
        return 0;
    }
    cmddb_free(&ta->db);
    ta->db = fresh;
    load_document_packages(ta, &c);
    if (!ta->is_fragment)
        remember_document(ta, &c);

    /* A declaration applies to this request, not to unrelated later text. */
    if (c.have_main_lang)
        snprintf(ta->doc_lang, sizeof ta->doc_lang, "%s", c.main_lang);
    else if (c.saw_class)
        ta->doc_lang[0] = '\0';
    char cur_lang[32] = "";
    speller *sp = speller_for_text(ta, cur_lang);

    ta_issue *issues = NULL;
    size_t ni = 0, icap = 0;
    for (size_t i = 0; i < c.n; i++) {
        const tok *t = &c.v[i];
        const char *w = c.text + t->toff;
        int ok = 1;
        ta_kind kind = TA_WORD;
        switch (t->kind) {
        case TS_LANGUAGE:
            if (t->tlen < sizeof cur_lang) {
                memcpy(cur_lang, w, t->tlen);
                cur_lang[t->tlen] = '\0';
            } else {
                snprintf(cur_lang, sizeof cur_lang, "?");
            }
            sp = speller_for_text(ta, cur_lang);
            continue;
        case TS_WORD:
            ok = word_ok(ta, sp, w, t->tlen);
            kind = TA_WORD;
            if (!ok)
                remember_word(ta, sp, w, t->tlen);
            break;
        case TS_COMMAND:
            ok = !ta->check_commands || command_ok(ta, w, t->tlen);
            kind = TA_COMMAND;
            break;
        case TS_ENVIRONMENT:
            ok = !ta->check_commands || environment_ok(ta, w, t->tlen);
            kind = TA_ENVIRONMENT;
            break;
        case TOK_OPTION:
            ok = !ta->check_commands ||
                 option_ok(ta, c.text + t->coff, t->clen, w, t->tlen);
            kind = TA_OPTION;
            break;
        }
        strset_addn(kinds, w, t->tlen);
        unsigned *category = strset_valn(kinds, w, t->tlen);
        if (category) {
            unsigned next = (unsigned)kind + 1;
            if (kind == TA_OPTION) {
                int index = option_index(key_package(c.text + t->coff, t->clen));
                next |= (unsigned)(index + 1) << 8;
            }
            /* Cocoa's Learn/Suggest callbacks supply no token location.
             * Ambiguous spellings default to prose rather than implicitly
             * teaching a command/environment/key from the wrong occurrence. */
            *category = *category && *category != next ? TA_WORD + 1 : next;
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
    for (size_t i = 0; i < c.ninputs; i++)
        free(c.inputs[i]);
    free(c.inputs);
    *out = issues;
    return ni;
}

/* ------------------------------------------------------------------------
 * Suggestions and learning
 * ---------------------------------------------------------------------- */

static size_t suggest_names(texaspell *ta, cmddb_kind kind,
                            const char *name, size_t len, char **out, size_t max)
{
    const strset *user = kind == CMDDB_COMMANDS ? &ta->user_cmds : &ta->user_envs;
    const strset *local = kind == CMDDB_COMMANDS ? &ta->inferred_cmds : &ta->inferred_envs;
    const strset *recent = kind == CMDDB_COMMANDS ? &ta->fragment_cmds : &ta->fragment_envs;
    strset extra = {0};
    const strset *sets[] = {user, local, recent};
    for (size_t k = 0; k < (ta->is_fragment ? 3u : 2u); k++)
        for (size_t i = 0; i < sets[k]->cap; i++)
            if (sets[k]->slots[i]) strset_add(&extra, sets[k]->slots[i]);
    size_t n = cmddb_suggest(&ta->db, &extra, kind, name, len, out, max);
    strset_free(&extra);
    return n;
}

/* The category of a word in the most recent check that contained it
 * (0 if none of the last KIND_HISTORY checks did). */
static unsigned recent_category(const texaspell *ta, const char *word, size_t len)
{
    for (int i = 0; i < KIND_HISTORY; i++) {
        const strset *k = &ta->kinds[(ta->kinds_next - i + KIND_HISTORY) % KIND_HISTORY];
        unsigned *v = strset_valn(k, word, len);
        if (v && *v)
            return *v;
    }
    return 0;
}

size_t ta_suggest(texaspell *ta, const char *word, size_t len, char **out, size_t max)
{
    if (len > 1 && word[0] == '\\') {
        size_t n = suggest_names(ta, CMDDB_COMMANDS, word + 1, len - 1, out, max);
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
    unsigned category = recent_category(ta, word, len);
    if ((category & 255) == TA_ENVIRONMENT + 1)
        return suggest_names(ta, CMDDB_ENVIRONMENTS, word, len, out, max);
    if ((category & 255) == TA_OPTION + 1)
        return cmddb_suggest(option_db(ta, (int)(category >> 8) - 1), &ta->user_keys, CMDDB_KEYS,
                             word, len, out, max);
    speller *sp = ta->sp;
    for (int i = 0; i < RECENT_WORDS; i++) {
        /* newest first */
        const recent_word *r =
            &ta->recent_words[(ta->recent_next - 1 - i + 2 * RECENT_WORDS) % RECENT_WORDS];
        if (r->sp && strlen(r->word) == len && memcmp(r->word, word, len) == 0) {
            sp = r->sp;
            break;
        }
    }
    return speller_suggest(sp, word, len, out, max);
}

void ta_learn(texaspell *ta, const char *word, size_t len)
{
    if (len == 0 || len > 250 || memchr(word, '\n', len))
        return;
    unsigned category = recent_category(ta, word, len);
    if (len > 1 && word[0] == '\\') {
        strset_addn(&ta->user_cmds, word + 1, len - 1);
        append_line(ta, "commands.txt", word, len);
    } else if ((category & 255) == TA_ENVIRONMENT + 1) {
        strset_addn(&ta->user_envs, word, len);
        append_line(ta, "commands.txt", word, len);
    } else if ((category & 255) == TA_OPTION + 1) {
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
