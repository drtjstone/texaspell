/* cmddb.c - the set of LaTeX commands and environments that exist. */
#include "cmddb.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "scan.h"

#define MAX_FILE_BYTES (16u * 1024 * 1024)

/* Value stored with each command name: bit 0 is set when a loaded file
 * defines it; the rest counts the loaded files that mention it. A command
 * is known if it is defined, or mentioned in at least two files (a single
 * mention is often a list of commands from some other package). */
#define CMD_DEFINED 1u
#define CMD_FILE 2u

static unsigned cmd_value(const cmddb *db, const char *name, size_t len)
{
    unsigned *v = strset_valn(&db->cmds, name, len);
    return v ? *v : 0;
}

static int cmd_known_value(unsigned v)
{
    return (v & CMD_DEFINED) || v / CMD_FILE >= 2;
}

static void cmd_mark(cmddb *db, const char *name, size_t len, unsigned bits)
{
    if (strset_addn(&db->cmds, name, len) < 0)
        return;
    unsigned *v = strset_valn(&db->cmds, name, len);
    if (v)
        *v |= bits;
}
#define MAX_FILES_PER_LOAD 1500

/* TeX, e-TeX, pdfTeX, XeTeX and LuaTeX primitives a document might use.
 * Most also appear in latex.ltx; this list covers the rest. */
static const char *const primitives[] = {
    "above", "abovedisplayshortskip", "abovedisplayskip", "abovewithdelims",
    "accent", "adjdemerits", "advance", "afterassignment", "aftergroup",
    "atop", "atopwithdelims", "badness", "baselineskip", "batchmode",
    "begingroup", "belowdisplayshortskip", "belowdisplayskip", "binoppenalty",
    "botmark", "box", "boxmaxdepth", "brokenpenalty", "catcode", "char",
    "chardef", "cleaders", "closein", "closeout", "clubpenalty", "copy",
    "count", "countdef", "cr", "crcr", "csname", "day", "deadcycles", "def",
    "defaulthyphenchar", "defaultskewchar", "delcode", "delimiter",
    "delimiterfactor", "delimitershortfall", "dimen", "dimendef",
    "discretionary", "displayindent", "displaylimits", "displaystyle",
    "displaywidowpenalty", "displaywidth", "divide", "doublehyphendemerits",
    "dp", "dump", "edef", "else", "emergencystretch", "end", "endcsname",
    "endgroup", "endinput", "endlinechar", "eqno", "errhelp", "errmessage",
    "errorcontextlines", "errorstopmode", "escapechar", "everycr",
    "everydisplay", "everyhbox", "everyjob", "everymath", "everypar",
    "everyvbox", "exhyphenpenalty", "expandafter", "fam", "fi",
    "finalhyphendemerits", "firstmark", "floatingpenalty", "font",
    "fontdimen", "fontname", "futurelet", "gdef", "global", "globaldefs",
    "halign", "hangafter", "hangindent", "hbadness", "hbox", "hfil",
    "hfill", "hfilneg", "hfuzz", "hoffset", "holdinginserts", "hrule",
    "hsize", "hskip", "hss", "ht", "hyphenation", "hyphenchar",
    "hyphenpenalty", "if", "ifcase", "ifcat", "ifdim", "ifeof", "iffalse",
    "ifhbox", "ifhmode", "ifinner", "ifmmode", "ifnum", "ifodd", "iftrue",
    "ifvbox", "ifvmode", "ifvoid", "ifx", "ignorespaces", "immediate",
    "indent", "input", "inputlineno", "insert", "insertpenalties",
    "interlinepenalty", "jobname", "kern", "language", "lastbox",
    "lastkern", "lastpenalty", "lastskip", "lccode", "leaders", "left",
    "lefthyphenmin", "leftskip", "leqno", "let", "limits", "linepenalty",
    "lineskip", "lineskiplimit", "long", "looseness", "lower", "lowercase",
    "mag", "mark", "mathaccent", "mathbin", "mathchar", "mathchardef",
    "mathchoice", "mathclose", "mathcode", "mathinner", "mathop",
    "mathopen", "mathord", "mathpunct", "mathrel", "mathsurround",
    "maxdeadcycles", "maxdepth", "meaning", "medmuskip", "message",
    "middle", "mkern", "month", "moveleft", "moveright", "mskip",
    "multiply", "muskip", "muskipdef", "newlinechar", "noalign",
    "noboundary", "noexpand", "noindent", "nolimits", "nonscript",
    "nonstopmode", "nulldelimiterspace", "nullfont", "number", "omit",
    "openin", "openout", "or", "outer", "output", "outputpenalty", "over",
    "overfullrule", "overline", "overwithdelims", "pagedepth",
    "pagefilllstretch", "pagefillstretch", "pagefilstretch", "pagegoal",
    "pageshrink", "pagestretch", "pagetotal", "par", "parfillskip",
    "parindent", "parshape", "parskip", "patterns", "pausing", "penalty",
    "postdisplaypenalty", "predisplaypenalty", "predisplaysize",
    "pretolerance", "prevdepth", "prevgraf", "radical", "raise", "read",
    "relax", "relpenalty", "right", "righthyphenmin", "rightskip",
    "romannumeral", "scriptfont", "scriptscriptfont", "scriptscriptstyle",
    "scriptspace", "scriptstyle", "scrollmode", "setbox", "setlanguage",
    "sfcode", "shipout", "show", "showbox", "showboxbreadth",
    "showboxdepth", "showlists", "showthe", "skewchar", "skip", "skipdef",
    "spacefactor", "spaceskip", "span", "special", "splitbotmark",
    "splitfirstmark", "splitmaxdepth", "splittopskip", "string",
    "tabskip", "textfont", "textstyle", "the", "thickmuskip",
    "thinmuskip", "time", "toks", "toksdef", "tolerance", "topmark",
    "topskip", "tracingcommands", "tracinglostchars", "tracingmacros",
    "tracingonline", "tracingoutput", "tracingpages", "tracingparagraphs",
    "tracingrestores", "tracingstats", "uccode", "uchyph", "underline",
    "unhbox", "unhcopy", "unkern", "unpenalty", "unskip", "unvbox",
    "unvcopy", "uppercase", "vadjust", "valign", "vbadness", "vbox",
    "vcenter", "vfil", "vfill", "vfilneg", "vfuzz", "voffset", "vrule",
    "vsize", "vskip", "vsplit", "vss", "vtop", "wd", "widowpenalty",
    "write", "xdef", "xleaders", "xspaceskip", "year",
    /* e-TeX */
    "beginL", "beginR", "botmarks", "clubpenalties", "currentgrouplevel",
    "currentgrouptype", "currentifbranch", "currentiflevel",
    "currentiftype", "detokenize", "dimexpr", "displaywidowpenalties",
    "endL", "endR", "eTeXrevision", "eTeXversion", "everyeof",
    "firstmarks", "fontchardp", "fontcharht", "fontcharic", "fontcharwd",
    "glueexpr", "glueshrink", "glueshrinkorder", "gluestretch",
    "gluestretchorder", "gluetomu", "ifcsname", "ifdefined", "iffontchar",
    "interactionmode", "interlinepenalties", "lastlinefit", "lastnodetype",
    "marks", "muexpr", "mutoglue", "numexpr", "pagediscards", "parshapedimen",
    "parshapeindent", "parshapelength", "predisplaydirection", "protected",
    "readline", "savinghyphcodes", "savingvdiscards", "scantokens",
    "showgroups", "showifs", "showtokens", "splitbotmarks",
    "splitdiscards", "splitfirstmarks", "TeXXeTstate", "topmarks",
    "tracingassigns", "tracinggroups", "tracingifs", "tracingnesting",
    "tracingscantokens", "unexpanded", "unless", "widowpenalties",
    /* pdfTeX */
    "pdfoutput", "pdfpagewidth", "pdfpageheight", "pdfstrcmp",
    "pdfliteral", "pdfinfo", "pdfcatalog", "pdfcompresslevel",
    "pdfminorversion", "pdfobjcompresslevel", "pdfsavepos", "pdflastxpos",
    "pdflastypos", "pdfmdfivesum", "pdffilesize", "pdfcreationdate",
    "pdfelapsedtime", "pdfresettimer", "pdfuniformdeviate",
    "pdfnormaldeviate", "pdfrandomseed", "pdfsetrandomseed", "pdftexversion",
    "pdftexrevision", "pdfximage", "pdfrefximage", "pdflastximage",
    "pdfadjustspacing", "pdfprotrudechars", "pdfglyphtounicode",
    "pdfgentounicode", "pdfsuppresswarningpagegroup", "pdfmapfile",
    "pdfmapline", "pdfprimitive", "pdfdraftmode", "pdfpagesattr",
    "pdfpageattr", "pdfpkresolution", "pdfstartlink", "pdfendlink",
    "pdfdest", "pdfoutline", "pdfannot", "pdfobj", "pdfrefobj",
    "pdflastobj", "pdfescapestring", "pdfescapename", "pdfescapehex",
    "pdfunescapehex", "pdfmatch", "pdflastmatch", "pdfshellescape",
    "pdffilemoddate", "pdffiledump", "expanded", "pdfstrcmp",
    /* XeTeX and LuaTeX */
    "XeTeXversion", "XeTeXrevision", "XeTeXinterchartokenstate",
    "XeTeXinterchartoks", "XeTeXcharclass", "XeTeXlinebreaklocale",
    "XeTeXlinebreakskip", "XeTeXdashbreakstate", "XeTeXuseglyphmetrics",
    "XeTeXinputencoding", "XeTeXdefaultencoding", "Umathchardef",
    "Umathcode", "Udelcode", "Uchar", "Ucharcat", "directlua",
    "luaescapestring", "luatexversion", "luatexrevision", "latelua",
    "luafunction", "luadef", "begincsname", "lastnamedcs", "ifincsname",
    "ifprimitive", "primitive", "shellescape", "strcmp",
    NULL
};

void cmddb_init(cmddb *db, const char *setting)
{
    strset_init(&db->cmds);
    strset_init(&db->envs);
    strset_init(&db->keys);
    strset_init(&db->tried);
    strset_init(&db->loaded);
    db->have_kernel = 0;
    db->files_read = 0;
    db->kpsewhich[0] = '\0';

    for (const char *const *p = primitives; *p; p++)
        cmd_mark(db, *p, strlen(*p), CMD_DEFINED);

    if (setting && *setting && strcmp(setting, "auto") != 0) {
        snprintf(db->kpsewhich, sizeof db->kpsewhich, "%s", setting);
        return;
    }
    /* A spell service started by macOS has a minimal PATH, so look in the
     * usual places first. */
    static const char *const candidates[] = {
        "/Library/TeX/texbin/kpsewhich",
        "/opt/homebrew/bin/kpsewhich",
        "/usr/local/bin/kpsewhich",
        "/usr/bin/kpsewhich",
        NULL
    };
    for (const char *const *c = candidates; *c; c++) {
        if (access(*c, X_OK) == 0) {
            snprintf(db->kpsewhich, sizeof db->kpsewhich, "%s", *c);
            return;
        }
    }
    snprintf(db->kpsewhich, sizeof db->kpsewhich, "kpsewhich");
}

void cmddb_free(cmddb *db)
{
    strset_free(&db->cmds);
    strset_free(&db->envs);
    strset_free(&db->keys);
    strset_free(&db->tried);
    strset_free(&db->loaded);
}

/* ------------------------------------------------------------------------
 * A growable list of names
 * ---------------------------------------------------------------------- */

typedef struct namelist {
    char **v;
    size_t n, cap;
} namelist;

static void nl_push(namelist *l, const char *s, size_t len)
{
    if (l->n == l->cap) {
        size_t ncap = l->cap ? l->cap * 2 : 32;
        char **nv = realloc(l->v, ncap * sizeof *nv);
        if (!nv)
            return;
        l->v = nv;
        l->cap = ncap;
    }
    char *c = malloc(len + 1);
    if (!c)
        return;
    memcpy(c, s, len);
    c[len] = '\0';
    l->v[l->n++] = c;
}

static void nl_free(namelist *l)
{
    for (size_t i = 0; i < l->n; i++)
        free(l->v[i]);
    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

/* Only plain file names are passed to the shell. */
static int safe_file_name(const char *s)
{
    if (!*s || *s == '-' || *s == '.')
        return 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (!(isalnum(c) || c == '.' || c == '-' || c == '_' || c == '+'))
            return 0;
    }
    return 1;
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* Run kpsewhich on names[] and store the full path for each one found in
 * paths[] (malloc'd; NULL if not found). */
static void kpse_lookup(const cmddb *db, char **names, size_t count, char **paths)
{
    for (size_t i = 0; i < count; i++)
        paths[i] = NULL;
    size_t cap = strlen(db->kpsewhich) + 16;
    for (size_t i = 0; i < count; i++)
        cap += strlen(names[i]) + 3;
    char *cmd = malloc(cap);
    if (!cmd)
        return;
    size_t k = (size_t)snprintf(cmd, cap, "'%s'", db->kpsewhich);
    int any = 0;
    for (size_t i = 0; i < count; i++) {
        if (!safe_file_name(names[i]))
            continue;
        k += (size_t)snprintf(cmd + k, cap - k, " %s", names[i]);
        any = 1;
    }
    snprintf(cmd + k, cap - k, " 2>/dev/null");
    if (!any) {
        free(cmd);
        return;
    }
    FILE *f = popen(cmd, "r");
    free(cmd);
    if (!f)
        return;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r'))
            line[--l] = '\0';
        const char *b = base_name(line);
        for (size_t i = 0; i < count; i++) {
            if (!paths[i] && strcmp(names[i], b) == 0) {
                paths[i] = strdup(line);
                break;
            }
        }
    }
    pclose(f);
}

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t cap = 65536, n = 0;
    char *buf = malloc(cap);
    while (buf) {
        if (n == cap) {
            if (cap >= MAX_FILE_BYTES)
                break;
            char *nb = realloc(buf, cap * 2);
            if (!nb) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = nb;
            cap *= 2;
        }
        size_t r = fread(buf + n, 1, cap - n, f);
        if (r == 0)
            break;
        n += r;
    }
    fclose(f);
    *len = n;
    return buf;
}

/* ------------------------------------------------------------------------
 * Loading
 * ---------------------------------------------------------------------- */

typedef struct load_ctx {
    cmddb *db;
    namelist *next;
    strset mentioned; /* names already counted for the current file */
} load_ctx;

static void queue_file(load_ctx *lc, const char *name, size_t len, const char *ext)
{
    char buf[256];
    size_t el = strlen(ext);
    if (len + el + 1 > sizeof buf)
        return;
    memcpy(buf, name, len);
    memcpy(buf + len, ext, el + 1);
    if (!strset_has(&lc->db->tried, buf))
        nl_push(lc->next, buf, len + el);
}

static void on_define(void *ctx, ts_def_kind kind, const char *name, size_t len)
{
    load_ctx *lc = ctx;
    switch (kind) {
    case TS_DEF_MENTION:
        if (strset_addn(&lc->mentioned, name, len) == 1) {
            strset_addn(&lc->db->cmds, name, len);
            unsigned *v = strset_valn(&lc->db->cmds, name, len);
            if (v)
                *v += CMD_FILE;
        }
        break;
    case TS_DEF_COMMAND:
        cmd_mark(lc->db, name, len, CMD_DEFINED);
        break;
    case TS_DEF_ENVIRONMENT:
        strset_addn(&lc->db->envs, name, len);
        break;
    case TS_DEF_COUNTER: {
        char buf[256];
        if (len + 4 < sizeof buf) {
            memcpy(buf, "the", 3);
            memcpy(buf + 3, name, len);
            cmd_mark(lc->db, buf, len + 3, CMD_DEFINED);
        }
        break;
    }
    case TS_DEF_PACKAGE:
        queue_file(lc, name, len, ".sty");
        break;
    case TS_DEF_CLASS:
        queue_file(lc, name, len, ".cls");
        break;
    case TS_DEF_KEY:
        strset_addn(&lc->db->keys, name, len);
        break;
    case TS_DEF_MAIN_LANGUAGE:
        break;
    }
}

static void on_input(void *ctx, const char *name, size_t len)
{
    load_ctx *lc = ctx;
    const char *slash = memchr(name, '/', len);
    if (slash)
        return;
    int has_ext = 0;
    for (size_t i = 0; i < len; i++)
        if (name[i] == '.')
            has_ext = 1;
    queue_file(lc, name, len, has_ext ? "" : ".tex");
}

/* Some packages load files or define keys in ways a plain reading of the
 * source cannot follow. */
static void add_companions(cmddb *db, load_ctx *lc, const char *file)
{
    static const char *const hyperref_files[] = {
        "hpdftex.def", "hxetex.def", "hluatex.def", NULL
    };
    /* keys generated in a loop: \Hy@temp{link}{red} -> linkcolor */
    static const char *const hyperref_keys[] = {
        "linkcolor", "anchorcolor", "citecolor", "filecolor", "urlcolor",
        "menucolor", "runcolor", "linkbordercolor", "citebordercolor",
        "filebordercolor", "urlbordercolor", "menubordercolor",
        "runbordercolor", NULL
    };
    static const char *const listings_files[] = {
        "lstmisc.sty", "lstmisc0.sty", NULL
    };
    /* keyword classes generated by \lst@InstallKeywords */
    static const char *const listings_keys[] = {
        "keywords", "morekeywords", "deletekeywords", "keywordstyle",
        "ndkeywords", "morendkeywords", "deletendkeywords", "ndkeywordstyle",
        "emph", "moreemph", "deleteemph", "emphstyle",
        "texcs", "moretexcs", "deletetexcs", "texcsstyle",
        "directives", "moredirectives", "deletedirectives", "directivestyle",
        "keywordsprefix", "otherkeywords", NULL
    };
    /* environments made from the float types when the document starts */
    static const char *const subcaption_envs[] = {
        "subfigure", "subtable", NULL
    };
    const char *const *files = NULL, *const *keys = NULL, *const *envs = NULL;
    if (strcmp(file, "hyperref.sty") == 0) {
        files = hyperref_files;
        keys = hyperref_keys;
    } else if (strcmp(file, "listings.sty") == 0) {
        files = listings_files;
        keys = listings_keys;
    } else if (strcmp(file, "subcaption.sty") == 0) {
        envs = subcaption_envs;
    }
    for (; envs && *envs; envs++)
        strset_add(&db->envs, *envs);
    for (; files && *files; files++)
        queue_file(lc, *files, strlen(*files), "");
    for (; keys && *keys; keys++)
        strset_add(&db->keys, *keys);
}

size_t cmddb_load(cmddb *db, const char *const *names, size_t count, int *found)
{
    namelist cur = {0}, next = {0};
    size_t nfound = 0, files = 0;

    for (size_t i = 0; i < count; i++) {
        if (found)
            found[i] = 0;
        if (!strset_has(&db->tried, names[i]))
            nl_push(&cur, names[i], strlen(names[i]));
    }

    while (cur.n && files < MAX_FILES_PER_LOAD) {
        char **paths = calloc(cur.n, sizeof *paths);
        if (!paths)
            break;
        /* dedupe within the round and mark as tried */
        for (size_t i = 0; i < cur.n; i++) {
            if (strset_add(&db->tried, cur.v[i]) != 1) {
                free(cur.v[i]);
                cur.v[i] = strdup("");
            }
        }
        kpse_lookup(db, cur.v, cur.n, paths);

        load_ctx lc = {db, &next, {0}};
        strset_init(&lc.mentioned);
        ts_callbacks cb = {&lc, NULL, on_define, NULL};
        for (size_t i = 0; i < cur.n; i++) {
            if (!paths[i])
                continue;
            strset_add(&db->loaded, cur.v[i]);
            size_t len = 0;
            char *text = read_file(paths[i], &len);
            if (text) {
                strset_free(&lc.mentioned);
                ts_scan_package(text, len, &cb, on_input);
                free(text);
                files++;
                db->files_read++;
                if (strcmp(cur.v[i], "latex.ltx") == 0)
                    db->have_kernel = 1;
                add_companions(db, &lc, cur.v[i]);
            }
            free(paths[i]);
        }
        free(paths);
        strset_free(&lc.mentioned);
        nl_free(&cur);
        cur = next;
        next = (namelist){0};
    }
    nl_free(&cur);
    nl_free(&next);
    for (size_t i = 0; i < count; i++) {
        if (strset_has(&db->loaded, names[i])) {
            if (found)
                found[i] = 1;
            nfound++;
        }
    }
    return nfound;
}

/* ------------------------------------------------------------------------
 * Queries
 * ---------------------------------------------------------------------- */

int cmddb_has_command(const cmddb *db, const char *name, size_t len)
{
    return cmd_known_value(cmd_value(db, name, len));
}

int cmddb_has_environment(const cmddb *db, const char *name, size_t len)
{
    if (strset_hasn(&db->envs, name, len))
        return 1;
    /* An environment foo is usually the pair of macros \foo and \endfoo. */
    size_t base = len;
    if (base && name[base - 1] == '*')
        base--;
    char buf[256];
    if (base == 0 || base + 4 > sizeof buf)
        return 0;
    if (strset_hasn(&db->envs, name, base) && base != len)
        return 1;
    memcpy(buf, "end", 3);
    memcpy(buf + 3, name, base);
    return cmddb_has_command(db, name, base) && cmddb_has_command(db, buf, base + 3);
}

int cmddb_has_key(const cmddb *db, const char *name, size_t len)
{
    return strset_hasn(&db->keys, name, len);
}

int cmddb_loaded(const cmddb *db, const char *file)
{
    return strset_has(&db->loaded, file);
}

int cmddb_distance(const char *a, size_t al, const char *b, size_t bl, int limit)
{
    if (al > 63 || bl > 63)
        return limit + 1;
    if ((al > bl ? al - bl : bl - al) > (size_t)limit)
        return limit + 1;
    int d[64][64];
    for (size_t i = 0; i <= al; i++) d[i][0] = (int)i;
    for (size_t j = 0; j <= bl; j++) d[0][j] = (int)j;
    for (size_t i = 1; i <= al; i++) {
        int rowmin = limit + 1;
        for (size_t j = 1; j <= bl; j++) {
            int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            int v = d[i - 1][j] + 1;
            if (d[i][j - 1] + 1 < v) v = d[i][j - 1] + 1;
            if (d[i - 1][j - 1] + cost < v) v = d[i - 1][j - 1] + cost;
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] &&
                d[i - 2][j - 2] + 1 < v)
                v = d[i - 2][j - 2] + 1;
            d[i][j] = v;
            if (v < rowmin) rowmin = v;
        }
        if (rowmin > limit)
            return limit + 1;
    }
    return d[al][bl];
}

typedef struct cand {
    const char *name;
    int score;
} cand;

/* popularity: how many loaded files mention the candidate */
static void consider(cand *best, size_t *nbest, size_t max, const char *name,
                     const char *q, size_t ql, unsigned popularity)
{
    size_t nl = strlen(name);
    int d = cmddb_distance(q, ql, name, nl, 2);
    if (d > 2)
        return;
    /* Rank by distance; among equals prefer a case-only difference, then
     * commands used by many packages (\large rather than \marg for
     * \larg), then the length closest to the query. */
    int caseonly = (nl == ql);
    for (size_t i = 0; caseonly && i < nl; i++)
        if (tolower((unsigned char)name[i]) != tolower((unsigned char)q[i]))
            caseonly = 0;
    int pop = 0;
    while (popularity > 1 && pop < 12) {
        popularity >>= 1;
        pop++;
    }
    int score = d * 1000 - (caseonly ? 500 : 0) - pop * 20 +
                (int)(nl > ql ? nl - ql : ql - nl) * 10;
    /* plain "cases" before "cases*" unless a star was typed */
    if (nl && name[nl - 1] == '*' && !(ql && q[ql - 1] == '*'))
        score += 15;
    for (size_t i = 0; i < *nbest; i++)
        if (strcmp(best[i].name, name) == 0)
            return;
    size_t pos = *nbest;
    while (pos > 0 && (best[pos - 1].score > score ||
                       (best[pos - 1].score == score &&
                        strcmp(best[pos - 1].name, name) > 0)))
        pos--;
    if (pos >= max)
        return;
    size_t end = *nbest < max ? *nbest : max - 1;
    memmove(&best[pos + 1], &best[pos], (end - pos) * sizeof *best);
    best[pos].name = name;
    best[pos].score = score;
    if (*nbest < max)
        (*nbest)++;
}

size_t cmddb_suggest(const cmddb *db, const strset *extra, cmddb_kind kind,
                     const char *q, size_t ql, char **out, size_t max)
{
    if (max == 0)
        return 0;
    cand *best = calloc(max, sizeof *best);
    if (!best)
        return 0;
    size_t nbest = 0;

    if (kind == CMDDB_KEYS) {
        for (size_t i = 0; i < db->keys.cap; i++)
            if (db->keys.slots[i])
                consider(best, &nbest, max, db->keys.slots[i], q, ql, 0);
    } else if (kind == CMDDB_ENVIRONMENTS) {
        for (size_t i = 0; i < db->envs.cap; i++)
            if (db->envs.slots[i])
                consider(best, &nbest, max, db->envs.slots[i], q, ql, 0);
        /* \foo + \endfoo pairs */
        for (size_t i = 0; i < db->cmds.cap; i++) {
            const char *c = db->cmds.slots[i];
            if (c && strncmp(c, "end", 3) == 0 && c[3] &&
                cmd_known_value(db->cmds.vals[i]) &&
                cmddb_has_command(db, c + 3, strlen(c + 3)))
                consider(best, &nbest, max, c + 3, q, ql,
                         cmd_value(db, c + 3, strlen(c + 3)) / CMD_FILE);
        }
    } else {
        for (size_t i = 0; i < db->cmds.cap; i++)
            if (db->cmds.slots[i] && cmd_known_value(db->cmds.vals[i]))
                consider(best, &nbest, max, db->cmds.slots[i], q, ql,
                         db->cmds.vals[i] / CMD_FILE);
    }
    if (extra)
        for (size_t i = 0; i < extra->cap; i++)
            if (extra->slots[i])
                consider(best, &nbest, max, extra->slots[i], q, ql, 0);

    for (size_t i = 0; i < nbest; i++)
        out[i] = strdup(best[i].name);
    free(best);
    return nbest;
}
