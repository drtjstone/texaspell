/* scan.c - split LaTeX source into prose words, command names and
 * environment names. See scan.h. */
#include "scan.h"

#include "languages.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Argument specifications
 *
 * Each command we treat specially has a spec string, one character per
 * argument. Lower case is an optional [..] argument, upper case a mandatory
 * {..} argument:
 *
 *   S / s  skip: nothing inside is checked (labels, keys, file names)
 *   R      skip a URL/path argument whose percent signs are literal
 *   K / k  key=value: commands are validated but words are not checked
 *   P / p  prose: words are checked (also inside maths, e.g. \text{..})
 *   M / m  maths: commands validated, words not checked
 *   N      the name of a command being defined: \newcommand{\foo}
 *   V      the name of an environment being defined
 *   C      the name of a counter being defined
 *   G      a comma separated list of packages
 *   L      a document class
 *   A      a language name; the next prose argument is in that language
 *   O      a key=value option list: keys are reported for checking, values
 *          of keys such as pdftitle are prose, other values key=value
 *   ~      optional key=value argument, only if '[' follows immediately
 *
 * Commands not in the table get "~".
 * ---------------------------------------------------------------------- */

typedef struct spec_entry {
    const char *name;
    const char *spec;
} spec_entry;

static spec_entry cmd_specs[] = {
    /* packages and classes */
    {"documentclass", "kL"}, {"LoadClass", "kLk"}, {"LoadClassWithOptions", "L"},
    {"usepackage", "kGk"}, {"RequirePackage", "kGk"},
    {"RequirePackageWithOptions", "G"},
    {"PassOptionsToPackage", "SS"}, {"PassOptionsToClass", "SS"},
    {"ProvidesPackage", "Sk"}, {"ProvidesClass", "Sk"}, {"ProvidesFile", "Sk"},
    {"NeedsTeXFormat", "Sk"}, {"DeclareOption", "SP"}, {"ExecuteOptions", "S"},
    /* cross references and citations */
    {"label", "S"}, {"ref", "S"}, {"pageref", "S"}, {"eqref", "S"},
    {"autoref", "S"}, {"Autoref", "S"}, {"nameref", "S"}, {"vref", "S"},
    {"cref", "S"}, {"Cref", "S"}, {"crefrange", "SS"}, {"Crefrange", "SS"},
    {"labelcref", "S"}, {"cpageref", "S"}, {"zref", "kS"},
    {"cite", "kkS"}, {"citep", "kkS"}, {"citet", "kkS"}, {"citealp", "kkS"},
    {"citealt", "kkS"}, {"citeauthor", "kkS"}, {"Citeauthor", "kkS"},
    {"citeyear", "kkS"}, {"citeyearpar", "kkS"}, {"citetitle", "kkS"},
    {"nocite", "S"}, {"parencite", "kkS"}, {"Parencite", "kkS"},
    {"textcite", "kkS"}, {"Textcite", "kkS"}, {"autocite", "kkS"},
    {"Autocite", "kkS"}, {"footcite", "kkS"}, {"footcitetext", "kkS"},
    {"fullcite", "kkS"}, {"smartcite", "kkS"}, {"supercite", "kkS"},
    {"bibliography", "S"}, {"bibliographystyle", "S"},
    {"addbibresource", "kS"}, {"printbibliography", "k"}, {"bibitem", "kS"},
    {"index", "S"}, {"glossary", "S"},
    /* files, graphics, links */
    {"input", "S"}, {"include", "S"}, {"includeonly", "S"}, {"subfile", "S"},
    {"InputIfFileExists", "S"}, {"includegraphics", "kS"},
    {"graphicspath", "S"}, {"DeclareGraphicsExtensions", "S"},
    {"includepdf", "kS"}, {"lstinputlisting", "kS"}, {"inputminted", "kSS"},
    {"url", "R"}, {"path", "R"}, {"nolinkurl", "R"}, {"href", "kRP"},
    {"hyperref", "kP"}, {"hyperlink", "SP"}, {"hypertarget", "SP"},
    {"hypersetup", "O"}, {"pdfbookmark", "kPS"},
    /* definitions */
    {"newcommand", "NkkP"}, {"renewcommand", "NkkP"}, {"providecommand", "NkkP"},
    {"DeclareRobustCommand", "NkkP"}, {"NewDocumentCommand", "NSP"},
    {"RenewDocumentCommand", "NSP"}, {"ProvideDocumentCommand", "NSP"},
    {"DeclareDocumentCommand", "NSP"}, {"NewExpandableDocumentCommand", "NSP"},
    {"NewCommandCopy", "NS"}, {"RenewCommandCopy", "NS"},
    {"DeclareMathOperator", "NK"}, {"DeclarePairedDelimiter", "NKK"},
    {"newlength", "N"}, {"newsavebox", "N"}, {"newfontfamily", "NkSk"},
    {"newenvironment", "VkkPP"}, {"renewenvironment", "VkkPP"},
    {"NewDocumentEnvironment", "VSPP"}, {"RenewDocumentEnvironment", "VSPP"},
    {"newtheorem", "VkPk"}, {"declaretheorem", "kVk"}, {"newlist", "VSS"},
    {"newtcolorbox", "kVkkK"}, {"newcounter", "Ck"},
    {"theoremstyle", "S"}, {"newcolumntype", "SkK"},
    /* lengths, counters, spacing, boxes */
    {"setlength", "KK"}, {"addtolength", "KK"}, {"settowidth", "KP"},
    {"settoheight", "KP"}, {"settodepth", "KP"},
    {"setcounter", "SK"}, {"addtocounter", "SK"}, {"stepcounter", "S"},
    {"refstepcounter", "S"}, {"value", "S"}, {"arabic", "S"}, {"roman", "S"},
    {"Roman", "S"}, {"alph", "S"}, {"Alph", "S"}, {"fnsymbol", "S"},
    {"pagestyle", "S"}, {"thispagestyle", "S"}, {"pagenumbering", "S"},
    {"hspace", "K"}, {"vspace", "K"}, {"enlargethispage", "K"},
    {"linespread", "K"}, {"rule", "kKK"}, {"raisebox", "KkkP"},
    {"makebox", "kkP"}, {"framebox", "kkP"}, {"parbox", "kkkKP"},
    {"resizebox", "KKP"}, {"scalebox", "KkP"}, {"rotatebox", "kKP"},
    {"multicolumn", "KKP"}, {"multirow", "kKkKP"}, {"cline", "S"},
    {"cmidrule", "kS"},
    /* colour, fonts, setup */
    {"color", "kS"}, {"textcolor", "kSP"}, {"colorbox", "kSP"},
    {"fcolorbox", "kSSP"}, {"pagecolor", "kS"}, {"definecolor", "SSS"},
    {"colorlet", "SS"}, {"fontsize", "KK"}, {"usefont", "SSSS"},
    {"fontfamily", "S"}, {"setmainfont", "kSk"}, {"setsansfont", "kSk"},
    {"setmonofont", "kSk"}, {"setmathfont", "kSk"},
    {"usetheme", "kS"}, {"usecolortheme", "kS"}, {"usefonttheme", "kS"},
    {"useinnertheme", "kS"}, {"useoutertheme", "kS"},
    {"setbeamercolor", "SK"}, {"setbeamerfont", "SK"},
    {"setbeamertemplate", "SkK"}, {"setbeamersize", "K"},
    {"usetikzlibrary", "S"}, {"tikzset", "K"}, {"pgfplotsset", "K"},
    {"usepgfplotslibrary", "S"}, {"tikz", "kK"},
    {"geometry", "O"}, {"newgeometry", "O"}, {"captionsetup", "kO"},
    {"lstset", "O"}, {"sisetup", "O"}, {"setlist", "kO"},
    {"foreignlanguage", "kAP"}, {"textlang", "kAP"}, {"foreignquote", "AP"},
    {"foreignblockquote", "AP"}, {"hyphenrules", "S"},
    {"setotherlanguage", "kS"}, {"setotherlanguages", "S"},
    {"setmainlanguage", "kS"}, {"setdefaultlanguage", "kS"},
    {"babelprovide", "kS"}, {"babeltags", "S"},
    /* sectioning and other prose-carrying commands */
    {"part", "pP"}, {"chapter", "pP"}, {"section", "pP"}, {"subsection", "pP"},
    {"subsubsection", "pP"}, {"paragraph", "pP"}, {"subparagraph", "pP"},
    {"caption", "pP"}, {"title", "pP"}, {"author", "pP"},
    {"footnote", "kP"}, {"footnotetext", "kP"}, {"marginpar", "pP"},
    {"item", "p"}, {"addcontentsline", "SSP"}, {"addtocontents", "SP"},
    /* text inside maths */
    {"text", "P"}, {"mbox", "P"}, {"textrm", "P"}, {"textit", "P"},
    {"textbf", "P"}, {"textsf", "P"}, {"textsl", "P"}, {"textsc", "P"},
    {"textup", "P"}, {"textmd", "P"}, {"texttt", "P"}, {"textnormal", "P"},
    {"emph", "P"}, {"intertext", "P"}, {"shortintertext", "P"},
    {"ensuremath", "M"},
    /* siunitx */
    {"SI", "kKK"}, {"si", "kK"}, {"num", "kK"}, {"qty", "kKK"}, {"unit", "kK"},
    {"ang", "kK"}, {"SIrange", "kKKK"}, {"numrange", "kKK"},
    {"qtyrange", "kKKK"},
    /* glossaries / acronyms */
    {"gls", "kS"}, {"Gls", "kS"}, {"glspl", "kS"}, {"Glspl", "kS"},
    {"glsentrytext", "S"}, {"acrshort", "kS"}, {"acrlong", "kS"},
    {"acrfull", "kS"}, {"ac", "S"}, {"Ac", "S"}, {"acs", "S"}, {"acl", "S"},
    {"acp", "S"}, {"acf", "S"}, {"acsp", "S"}, {"aclp", "S"}, {"acfp", "S"},
    {"newacronym", "kSKP"}, {"newglossaryentry", "SK"},
    {"DeclareAcronym", "SK"},
    /* chemistry: formulae, not prose */
    {"ce", "S"}, {"pu", "S"},
};

typedef enum { ENV_PLAIN, ENV_MATH, ENV_VERB, ENV_KV, ENV_LANG } env_kind;

typedef struct env_entry {
    const char *name;
    env_kind kind;
    const char *spec; /* arguments after \begin{name} */
} env_entry;

static const env_entry env_table[] = {
    /* maths */
    {"equation", ENV_MATH, NULL}, {"equation*", ENV_MATH, NULL},
    {"align", ENV_MATH, NULL}, {"align*", ENV_MATH, NULL},
    {"alignat", ENV_MATH, "K"}, {"alignat*", ENV_MATH, "K"},
    {"flalign", ENV_MATH, NULL}, {"flalign*", ENV_MATH, NULL},
    {"gather", ENV_MATH, NULL}, {"gather*", ENV_MATH, NULL},
    {"multline", ENV_MATH, NULL}, {"multline*", ENV_MATH, NULL},
    {"eqnarray", ENV_MATH, NULL}, {"eqnarray*", ENV_MATH, NULL},
    {"math", ENV_MATH, NULL}, {"displaymath", ENV_MATH, NULL},
    {"dmath", ENV_MATH, NULL}, {"dmath*", ENV_MATH, NULL},
    {"split", ENV_MATH, NULL}, {"aligned", ENV_MATH, "k"},
    {"gathered", ENV_MATH, "k"}, {"alignedat", ENV_MATH, "kK"},
    {"cases", ENV_MATH, NULL},
    /* verbatim-like: nothing up to \end{name} is looked at */
    {"verbatim", ENV_VERB, NULL}, {"verbatim*", ENV_VERB, NULL},
    {"Verbatim", ENV_VERB, NULL}, {"Verbatim*", ENV_VERB, NULL},
    {"BVerbatim", ENV_VERB, NULL}, {"LVerbatim", ENV_VERB, NULL},
    {"lstlisting", ENV_VERB, NULL}, {"minted", ENV_VERB, NULL},
    {"comment", ENV_VERB, NULL}, {"filecontents", ENV_VERB, NULL},
    {"filecontents*", ENV_VERB, NULL}, {"alltt", ENV_VERB, NULL},
    {"tcblisting", ENV_VERB, NULL}, {"luacode", ENV_VERB, NULL},
    {"luacode*", ENV_VERB, NULL}, {"pycode", ENV_VERB, NULL},
    {"sagesilent", ENV_VERB, NULL}, {"sageblock", ENV_VERB, NULL},
    /* drawing code: commands validated, words skipped */
    {"tikzpicture", ENV_KV, "k"}, {"axis", ENV_KV, "k"},
    {"semilogxaxis", ENV_KV, "k"}, {"semilogyaxis", ENV_KV, "k"},
    {"loglogaxis", ENV_KV, "k"}, {"groupplot", ENV_KV, "k"},
    {"scope", ENV_KV, "k"}, {"circuitikz", ENV_KV, "k"},
    {"pspicture", ENV_KV, NULL}, {"picture", ENV_KV, NULL},
    /* ordinary environments that take arguments */
    {"tabular", ENV_PLAIN, "kK"}, {"tabular*", ENV_PLAIN, "KkK"},
    {"tabularx", ENV_PLAIN, "KkK"}, {"tabulary", ENV_PLAIN, "KkK"},
    {"longtable", ENV_PLAIN, "kK"}, {"array", ENV_PLAIN, "kK"},
    {"supertabular", ENV_PLAIN, "K"}, {"xtabular", ENV_PLAIN, "K"},
    {"tblr", ENV_PLAIN, "kK"}, {"NiceTabular", ENV_PLAIN, "K"},
    {"minipage", ENV_PLAIN, "kkkK"}, {"multicols", ENV_PLAIN, "Kp"},
    {"multicols*", ENV_PLAIN, "Kp"}, {"thebibliography", ENV_PLAIN, "K"},
    {"list", ENV_PLAIN, "PK"}, {"wrapfigure", ENV_PLAIN, "kKkK"},
    {"wraptable", ENV_PLAIN, "kKkK"}, {"subfigure", ENV_PLAIN, "kK"},
    {"subtable", ENV_PLAIN, "kK"}, {"figure", ENV_PLAIN, "k"},
    {"figure*", ENV_PLAIN, "k"}, {"table", ENV_PLAIN, "k"},
    {"table*", ENV_PLAIN, "k"}, {"frame", ENV_PLAIN, "kkP"},
    {"block", ENV_PLAIN, "P"}, {"alertblock", ENV_PLAIN, "P"},
    {"exampleblock", ENV_PLAIN, "P"}, {"column", ENV_PLAIN, "kK"},
    {"otherlanguage", ENV_LANG, NULL}, {"otherlanguage*", ENV_LANG, NULL},
};

static int spec_cmp(const void *a, const void *b)
{
    return strcmp(((const spec_entry *)a)->name, ((const spec_entry *)b)->name);
}

static int specs_sorted;

static const char *lookup_spec(const char *name, size_t len)
{
    size_t n = sizeof cmd_specs / sizeof cmd_specs[0];
    char key[64];
    if (len >= sizeof key)
        return NULL;
    if (!specs_sorted) {
        qsort(cmd_specs, n, sizeof cmd_specs[0], spec_cmp);
        specs_sorted = 1;
    }
    memcpy(key, name, len);
    key[len] = '\0';
    spec_entry k = {key, NULL};
    const spec_entry *e = bsearch(&k, cmd_specs, n, sizeof cmd_specs[0], spec_cmp);
    return e ? e->spec : NULL;
}

static const env_entry *lookup_env(const char *name, size_t len)
{
    for (size_t i = 0; i < sizeof env_table / sizeof env_table[0]; i++) {
        const char *e = env_table[i].name;
        if (strlen(e) == len && memcmp(e, name, len) == 0)
            return &env_table[i];
    }
    return NULL;
}

/* ------------------------------------------------------------------------
 * Scanner state
 * ---------------------------------------------------------------------- */

typedef enum { M_PROSE, M_MATH, M_KV, M_SKIP, M_RAW_SKIP } mode;

typedef enum {
    C_BASE, C_BRACE, C_BRACKET, C_DOLLAR, C_DDOLLAR, C_PAREN, C_SQUARE, C_ENV
} closer;

#define MAX_DEPTH 256
#define MAX_ENV 64
#define MAX_LANG 32

typedef struct frame {
    mode mode;
    closer closer;
    char env[MAX_ENV];
    char lang[MAX_LANG]; /* language of the text ("" = main language) */
    const char *resume; /* remaining argument spec once this frame closes */
    const char *cmd;    /* ... and the command it belongs to */
    size_t cmdlen;
} frame;

typedef struct scanner {
    const char *s;
    size_t n;
    size_t p;
    const ts_callbacks *cb;
    frame st[MAX_DEPTH];
    int depth;
    int overflow;
    int at_letter;
    int expl;
    const char *cmd;    /* command whose arguments are being read */
    size_t cmdlen;
    char pending_lang[MAX_LANG]; /* language for the next frame pushed */
    int have_pending_lang;
    char announced[MAX_LANG];    /* last TS_LANGUAGE reported */
    int have_announced;
    int pdf_fragment; /* only continue known keys after PDF fragment evidence */
    size_t class_opts_a, class_opts_b; /* \documentclass[..] options */
} scanner;

static frame *top(scanner *sc) { return &sc->st[sc->depth]; }

static void push(scanner *sc, mode m, closer c, const char *env, size_t envlen,
                 const char *resume)
{
    if (sc->depth >= MAX_DEPTH - 1) {
        sc->overflow++;
        return;
    }
    frame *parent = &sc->st[sc->depth];
    frame *f = &sc->st[++sc->depth];
    if (sc->have_pending_lang) {
        memcpy(f->lang, sc->pending_lang, MAX_LANG);
        sc->have_pending_lang = 0;
    } else {
        memcpy(f->lang, parent->lang, MAX_LANG);
    }
    f->mode = m;
    f->closer = c;
    f->resume = resume;
    f->cmd = sc->cmd;
    f->cmdlen = sc->cmdlen;
    f->env[0] = '\0';
    if (env && envlen < MAX_ENV) {
        memcpy(f->env, env, envlen);
        f->env[envlen] = '\0';
    }
}

/* Pop the top frame; return its resume spec. */
static const char *pop(scanner *sc)
{
    if (sc->overflow) {
        sc->overflow--;
        return NULL;
    }
    if (sc->depth == 0)
        return NULL;
    frame *f = &sc->st[sc->depth--];
    sc->cmd = f->cmd;
    sc->cmdlen = f->cmdlen;
    return f->resume;
}

/* Pop frames down to and including the nearest one with closer c.
 * Returns 1 and sets *resume if found, else 0 and leaves the stack alone. */
static int pop_to(scanner *sc, closer c, const char **resume)
{
    for (int i = sc->depth; i > 0; i--) {
        if (sc->st[i].closer == c) {
            if (resume)
                *resume = sc->st[i].resume;
            sc->cmd = sc->st[i].cmd;
            sc->cmdlen = sc->st[i].cmdlen;
            sc->depth = i - 1;
            sc->overflow = 0;
            return 1;
        }
    }
    return 0;
}

static void emit(scanner *sc, ts_kind k, const char *t, size_t len,
                 size_t start, size_t end)
{
    if (!sc->cb || !sc->cb->token)
        return;
    if (k == TS_WORD) {
        const char *lang = sc->st[sc->depth].lang;
        if (!sc->have_announced || strcmp(lang, sc->announced) != 0) {
            sc->cb->token(sc->cb->ctx, TS_LANGUAGE, lang, strlen(lang), start, start);
            memcpy(sc->announced, lang, MAX_LANG);
            sc->have_announced = 1;
        }
    }
    sc->cb->token(sc->cb->ctx, k, t, len, start, end);
}

static void set_lang(char *dst, const char *name, size_t len)
{
    if (len >= MAX_LANG)
        len = 0; /* not a language name we could know */
    memcpy(dst, name, len);
    dst[len] = '\0';
}

static void define(const ts_callbacks *cb, ts_def_kind k, const char *name, size_t len)
{
    if (cb && cb->define && len)
        cb->define(cb->ctx, k, name, len);
}

/* ------------------------------------------------------------------------
 * Character helpers
 * ---------------------------------------------------------------------- */

static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_digit(int c) { return c >= '0' && c <= '9'; }

static int is_cmd_letter(const scanner *sc, int c)
{
    return is_alpha(c) || (c == '@' && sc->at_letter) ||
           ((c == '_' || c == ':') && sc->expl);
}

/* Decode one UTF-8 character at s[p]. Returns its byte length (>= 1). */
static size_t utf8_decode(const char *s, size_t n, size_t p, unsigned *cp)
{
    unsigned char c = (unsigned char)s[p];
    size_t len;
    unsigned v;
    if (c < 0x80) { *cp = c; return 1; }
    if ((c & 0xE0) == 0xC0) { len = 2; v = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { len = 3; v = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { len = 4; v = c & 0x07; }
    else { *cp = 0xFFFD; return 1; }
    if (p + len > n) { *cp = 0xFFFD; return 1; }
    for (size_t i = 1; i < len; i++) {
        unsigned char d = (unsigned char)s[p + i];
        if ((d & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        v = (v << 6) | (d & 0x3F);
    }
    *cp = v;
    return len;
}

/* Is this non-ASCII code point a letter (as opposed to punctuation,
 * typographic quotes, dashes, symbols)? A deliberately rough test. */
static int is_letter_cp(unsigned cp)
{
    if (cp >= 0xC0 && cp < 0x2000)
        return cp != 0xD7 && cp != 0xF7;
    if (cp >= 0x2C00 && cp < 0x2E00)
        return 1;
    if (cp >= 0x3040 && cp < 0xFE00)
        return 1;
    if (cp >= 0x10000 && cp < 0x1F000)
        return 1;
    return 0;
}

static int letter_at(const char *s, size_t n, size_t p)
{
    if (p >= n)
        return 0;
    unsigned char c = (unsigned char)s[p];
    if (c < 0x80)
        return is_alpha(c);
    unsigned cp;
    utf8_decode(s, n, p, &cp);
    return is_letter_cp(cp);
}

/* Index of the '}' matching the '{' at q, or n if unbalanced. */
static size_t match_brace(const char *s, size_t n, size_t q)
{
    int d = 0;
    for (size_t i = q; i < n; i++) {
        char c = s[i];
        if (c == '\\') { i++; continue; }
        if (c == '%') { while (i < n && s[i] != '\n') i++; continue; }
        if (c == '{') d++;
        else if (c == '}' && --d == 0) return i;
    }
    return n;
}

/* As match_brace, but for verbatim-like arguments (\lstinline{..},
 * \mintinline{..}{..}) where % is a literal character, not a comment. */
static size_t match_brace_raw(const char *s, size_t n, size_t q)
{
    int d = 0;
    for (size_t i = q; i < n; i++) {
        char c = s[i];
        if (c == '\\') { i++; continue; }
        if (c == '{') d++;
        else if (c == '}' && --d == 0) return i;
    }
    return n;
}

/* Index of the ']' closing the '[' at q (braces protect), or n. */
static size_t match_bracket(const char *s, size_t n, size_t q)
{
    for (size_t i = q + 1; i < n; i++) {
        char c = s[i];
        if (c == '\\') { i++; continue; }
        if (c == '%') { while (i < n && s[i] != '\n') i++; continue; }
        if (c == '{') { i = match_brace(s, n, i); continue; }
        if (c == ']') return i;
    }
    return n;
}

/* Skip spaces, tabs and at most one line break (a blank line ends the
 * search, as it would end a paragraph in TeX). */
static size_t skip_ws(const scanner *sc, size_t q)
{
    int newlines = 0;
    while (q < sc->n) {
        char c = sc->s[q];
        if (c == '%') {
            while (q < sc->n && sc->s[q] != '\n') q++;
            if (q < sc->n) q++; /* the commented newline is not whitespace */
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') { q++; continue; }
        if (c == '\n' && newlines == 0) { newlines++; q++; continue; }
        break;
    }
    return q;
}

static void trim(const char *s, size_t *a, size_t *b)
{
    while (*a < *b && isspace((unsigned char)s[*a])) (*a)++;
    while (*b > *a && isspace((unsigned char)s[*b - 1])) (*b)--;
}

static size_t find_sub(const char *s, size_t n, size_t from, const char *needle, size_t nl)
{
    if (nl == 0)
        return from;
    for (size_t i = from; i + nl <= n; i++)
        if (s[i] == needle[0] && memcmp(s + i, needle, nl) == 0)
            return i;
    return n;
}

static int valid_env_name(const char *s, size_t len)
{
    if (!len)
        return 0;
    for (size_t i = 0; i < len; i++) {
        if (is_alpha((unsigned char)s[i]))
            continue;
        if (s[i] == '*' && i == len - 1 && i > 0)
            continue;
        return 0;
    }
    return 1;
}

static int name_is(const char *name, size_t len, const char *lit)
{
    return strlen(lit) == len && memcmp(name, lit, len) == 0;
}

static int name_in(const char *name, size_t len, const char *const *list)
{
    for (; *list; list++)
        if (name_is(name, len, *list))
            return 1;
    return 0;
}

/* Read a control sequence name starting just after a backslash at q.
 * Returns the end index. A control symbol is one character. */
static size_t read_cs(const scanner *sc, size_t q)
{
    if (q >= sc->n)
        return q;
    if (!is_cmd_letter(sc, (unsigned char)sc->s[q]))
        return q + 1;
    while (q < sc->n && is_cmd_letter(sc, (unsigned char)sc->s[q]))
        q++;
    return q;
}

/* ------------------------------------------------------------------------
 * Words
 * ---------------------------------------------------------------------- */

static const char *const accent_words[] = {
    "c", "v", "u", "H", "r", "d", "b", "k", "t", NULL
};
static const char *const special_letters[] = {
    "i", "j", "o", "O", "ae", "AE", "oe", "OE", "aa", "AA", "ss", "SS",
    "l", "L", "dh", "DH", "th", "TH", "ng", "NG", "dj", "DJ",
    NULL
};

static int is_accent_symbol(int c)
{
    return c == '\'' || c == '`' || c == '^' || c == '"' || c == '~' ||
           c == '=' || c == '.';
}

/* If a TeX accent or special letter starts at q, return the index just
 * past it; otherwise return q. Handles \'e, \'{e}, \"\i, \c{c}, \c c, \ss,
 * {\'e}, {\ss}. */
static size_t skip_accent(const scanner *sc, size_t q)
{
    const char *s = sc->s;
    size_t n = sc->n;
    if (q >= n)
        return q;
    if (s[q] == '{') {
        if (q + 2 < n && s[q + 1] == '\\') {
            size_t inner = skip_accent(sc, q + 1);
            if (inner > q + 1) {
                size_t close = match_brace(s, n, q);
                if (close < n && close <= inner + 2)
                    return close + 1;
            }
        }
        return q;
    }
    if (s[q] != '\\' || q + 1 >= n)
        return q;
    unsigned char c = (unsigned char)s[q + 1];
    if (is_accent_symbol(c)) {
        size_t r = q + 2;
        if (r < n && s[r] == '{') {
            size_t close = match_brace(s, n, r);
            return close < n ? close + 1 : n;
        }
        if (r + 1 < n && s[r] == '\\' && (s[r + 1] == 'i' || s[r + 1] == 'j'))
            return r + 2;
        if (r < n && letter_at(s, n, r)) {
            unsigned cp;
            return r + utf8_decode(s, n, r, &cp);
        }
        return q;
    }
    if (is_alpha(c)) {
        size_t e = q + 1;
        while (e < n && is_alpha((unsigned char)s[e]))
            e++;
        const char *name = s + q + 1;
        size_t len = e - (q + 1);
        if (name_in(name, len, accent_words)) {
            size_t r = e;
            if (r < n && s[r] == '{') {
                size_t close = match_brace(s, n, r);
                return close < n ? close + 1 : n;
            }
            while (r < n && s[r] == ' ')
                r++;
            if (r < n && is_alpha((unsigned char)s[r]))
                return r + 1;
            return q;
        }
        if (name_in(name, len, special_letters)) {
            if (e + 1 < n && s[e] == '{' && s[e + 1] == '}')
                return e + 2;
            return e;
        }
    }
    return q;
}

#define WORD_MAX 128

/* Read a prose word starting at sc->p and emit it unless it should be
 * ignored (contains digits or TeX accents, or is too long). */
static void read_word(scanner *sc)
{
    const char *s = sc->s;
    size_t n = sc->n;
    size_t start = sc->p, p = sc->p, end = sc->p;
    char buf[WORD_MAX];
    size_t bl = 0;
    int ignore = 0;

    while (p < n) {
        unsigned char c = (unsigned char)s[p];
        if (is_alpha(c)) {
            if (bl < WORD_MAX) buf[bl] = (char)c;
            bl++;
            end = ++p;
            continue;
        }
        if (is_digit(c)) {
            ignore = 1;
            end = ++p;
            continue;
        }
        if (c >= 0x80) {
            unsigned cp;
            size_t cl = utf8_decode(s, n, p, &cp);
            if (is_letter_cp(cp)) {
                for (size_t i = 0; i < cl; i++) {
                    if (bl < WORD_MAX) buf[bl] = s[p + i];
                    bl++;
                }
                p += cl;
                end = p;
                continue;
            }
            /* U+2019 right single quotation mark used as an apostrophe */
            if (cp == 0x2019 && bl && letter_at(s, n, p + cl)) {
                if (bl < WORD_MAX) buf[bl] = '\'';
                bl++;
                p += cl;
                continue;
            }
            break;
        }
        if (c == '\'' && bl && letter_at(s, n, p + 1)) {
            if (bl < WORD_MAX) buf[bl] = '\'';
            bl++;
            p++;
            continue;
        }
        if (c == '\\' && p + 2 < n && s[p + 1] == '-' && letter_at(s, n, p + 2)) {
            p += 2; /* discretionary hyphen: hyph\-en\-ation */
            continue;
        }
        if (c == '\\' || c == '{') {
            size_t a = skip_accent(sc, p);
            if (a > p) {
                ignore = 1;
                p = end = a;
                continue;
            }
        }
        break;
    }

    sc->p = end > start ? end : start + 1;
    if (end > start && !ignore && bl > 0 && bl <= WORD_MAX)
        emit(sc, TS_WORD, buf, bl, start, end);
}

/* Skip a run of word characters without reporting it. */
static void skip_word(scanner *sc)
{
    const char *s = sc->s;
    size_t n = sc->n, p = sc->p;
    while (p < n) {
        unsigned char c = (unsigned char)s[p];
        if (is_alpha(c) || is_digit(c) || c == '\'') { p++; continue; }
        if (c >= 0x80) {
            unsigned cp;
            size_t cl = utf8_decode(s, n, p, &cp);
            if (is_letter_cp(cp)) { p += cl; continue; }
        }
        break;
    }
    sc->p = p > sc->p ? p : sc->p + 1;
}

/* ------------------------------------------------------------------------
 * Arguments
 * ---------------------------------------------------------------------- */

static mode mode_for(char k)
{
    switch (k) {
    case 'S': case 's': return M_SKIP;
    case 'R': return M_RAW_SKIP;
    case 'P': case 'p': return M_PROSE;
    case 'M': case 'm': return M_MATH;
    default: return M_KV;
    }
}

/* Handle the content of a {..} argument that names something. */
static void name_arg(scanner *sc, char kind, size_t a, size_t b)
{
    const char *s = sc->s;
    trim(s, &a, &b);
    if (a >= b)
        return;
    switch (kind) {
    case 'N':
        if (s[a] == '\\') {
            size_t e = read_cs(sc, a + 1);
            define(sc->cb, TS_DEF_COMMAND, s + a + 1, e - (a + 1));
        }
        break;
    case 'V':
        if (valid_env_name(s + a, b - a))
            define(sc->cb, TS_DEF_ENVIRONMENT, s + a, b - a);
        break;
    case 'C':
        define(sc->cb, TS_DEF_COUNTER, s + a, b - a);
        break;
    case 'A':
        set_lang(sc->pending_lang, s + a, b - a);
        sc->have_pending_lang = 1;
        break;
    case 'G':
    case 'L': {
        size_t i = a;
        while (i < b) {
            size_t j = i;
            while (j < b && s[j] != ',')
                j++;
            size_t x = i, y = j;
            trim(s, &x, &y);
            if (y > x && !memchr(s + x, '\\', y - x) && !memchr(s + x, '#', y - x))
                define(sc->cb, kind == 'L' ? TS_DEF_CLASS : TS_DEF_PACKAGE,
                       s + x, y - x);
            i = j + 1;
        }
        break;
    }
    }
}

/* Process the argument spec for the command just read, starting at sc->p.
 * Arguments that need scanning push a frame and record the rest of the
 * spec; processing resumes when that frame closes. */
static void option_list(scanner *sc, size_t a, size_t b);

static void args(scanner *sc, const char *spec)
{
    if (!spec)
        return;
    while (*spec) {
        char k = *spec;
        size_t q;
        if (k == '~') {
            q = sc->p;
            if (q < sc->n && sc->s[q] == '[') {
                sc->p = q + 1;
                push(sc, M_KV, C_BRACKET, NULL, 0, spec + 1);
                return;
            }
            spec++;
            continue;
        }
        q = skip_ws(sc, sc->p);
        if (islower((unsigned char)k)) {
            if (q >= sc->n || sc->s[q] != '[') {
                spec++;
                continue;
            }
            sc->p = q + 1;
            push(sc, mode_for(k), C_BRACKET, NULL, 0, spec + 1);
            return;
        }
        if (q >= sc->n)
            return;
        char c = sc->s[q];
        if (k == 'N' && c == '\\') {
            size_t e = read_cs(sc, q + 1);
            define(sc->cb, TS_DEF_COMMAND, sc->s + q + 1, e - (q + 1));
            sc->p = e;
            spec++;
            continue;
        }
        if (c != '{') {
            sc->have_pending_lang = 0;
            return; /* argument given as a single token, or absent */
        }
        if (k == 'O') {
            size_t close = match_brace(sc->s, sc->n, q);
            option_list(sc, q + 1, close);
            sc->p = close < sc->n ? close + 1 : sc->n;
            spec++;
            continue;
        }
        switch (k) {
        case 'S': case 'R': case 'K': case 'P': case 'M':
            sc->p = q + 1;
            push(sc, mode_for(k), C_BRACE, NULL, 0, spec + 1);
            return;
        default: {
            size_t close = match_brace(sc->s, sc->n, q);
            name_arg(sc, k, q + 1, close);
            sc->p = close < sc->n ? close + 1 : sc->n;
            spec++;
            continue;
        }
        }
    }
}

/* ------------------------------------------------------------------------
 * Commands
 * ---------------------------------------------------------------------- */

/* Read {name} after \begin or \end. Returns 1 and the name range on success
 * and moves sc->p past the closing brace. */
static int env_arg(scanner *sc, size_t *a, size_t *b)
{
    size_t q = skip_ws(sc, sc->p);
    if (q >= sc->n || sc->s[q] != '{')
        return 0;
    size_t close = match_brace(sc->s, sc->n, q);
    if (close >= sc->n) {
        sc->p = sc->n;
        return 0;
    }
    *a = q + 1;
    *b = close;
    trim(sc->s, a, b);
    sc->p = close + 1;
    return 1;
}

static void do_begin(scanner *sc)
{
    size_t a, b;
    if (!env_arg(sc, &a, &b))
        return;
    const char *name = sc->s + a;
    size_t len = b - a;
    /* polyglossia: \begin{german} ... \end{german} */
    int lang_env = lang_is_name(name, len) && !lookup_env(name, len);
    if (valid_env_name(name, len) && !lang_env)
        emit(sc, TS_ENVIRONMENT, name, len, a, b);
    const env_entry *e = lookup_env(name, len);
    if (lang_env) {
        set_lang(sc->pending_lang, name, len);
        sc->have_pending_lang = 1;
        push(sc, M_PROSE, C_ENV, name, len, NULL);
        args(sc, "k");
        return;
    }
    if (e && e->kind == ENV_LANG) {
        /* babel: \begin{otherlanguage}{german} */
        size_t la, lb;
        if (env_arg(sc, &la, &lb)) {
            set_lang(sc->pending_lang, sc->s + la, lb - la);
            sc->have_pending_lang = 1;
        }
        push(sc, M_PROSE, C_ENV, name, len, NULL);
        return;
    }
    if (e && e->kind == ENV_VERB) {
        char needle[MAX_ENV + 8];
        if (len < MAX_ENV) {
            memcpy(needle, "\\end{", 5);
            memcpy(needle + 5, name, len);
            needle[5 + len] = '}';
            sc->p = find_sub(sc->s, sc->n, sc->p, needle, len + 6);
        } else {
            sc->p = sc->n;
        }
        return;
    }
    if (e && e->kind == ENV_MATH)
        push(sc, M_MATH, C_ENV, name, len, NULL);
    else if (e && e->kind == ENV_KV)
        push(sc, M_KV, C_ENV, name, len, NULL);
    else
        push(sc, top(sc)->mode, C_ENV, name, len, NULL);
    args(sc, e && e->spec ? e->spec : "~");
}

static void do_end(scanner *sc)
{
    size_t a, b;
    if (!env_arg(sc, &a, &b))
        return;
    const char *name = sc->s + a;
    size_t len = b - a;
    int lang_env = lang_is_name(name, len) && !lookup_env(name, len);
    if (valid_env_name(name, len) && !lang_env)
        emit(sc, TS_ENVIRONMENT, name, len, a, b);
    for (int i = sc->depth; i > 0; i--) {
        frame *f = &sc->st[i];
        if (f->closer == C_ENV && strlen(f->env) == len &&
            memcmp(f->env, name, len) == 0) {
            sc->depth = i - 1;
            sc->overflow = 0;
            break;
        }
    }
}

/* Skip a verbatim argument starting at q: {..} or a delimiter character. */
static size_t skip_verb_arg(const scanner *sc, size_t q)
{
    if (q >= sc->n)
        return q;
    if (sc->s[q] == '{') {
        size_t close = match_brace_raw(sc->s, sc->n, q);
        return close < sc->n ? close + 1 : sc->n;
    }
    char d = sc->s[q];
    for (size_t i = q + 1; i < sc->n; i++) {
        if (sc->s[i] == d)
            return i + 1;
        if (sc->s[i] == '\n')
            return i;
    }
    return sc->n;
}

static const char *const def_family[] = {
    "def", "gdef", "edef", "xdef", "let", "futurelet", "chardef",
    "mathchardef", "countdef", "dimendef", "skipdef", "toksdef", "muskipdef",
    "newcount", "newdimen", "newskip", "newmuskip", "newtoks", "newbox",
    "newread", "newwrite", "newlanguage", "newinsert", "newfam", "font",
    "newif", NULL
};

static void do_def(scanner *sc, const char *cmd, size_t cmdlen)
{
    size_t q = skip_ws(sc, sc->p);
    if (q + 1 >= sc->n || sc->s[q] != '\\')
        return;
    size_t e = read_cs(sc, q + 1);
    const char *name = sc->s + q + 1;
    size_t len = e - (q + 1);
    define(sc->cb, TS_DEF_COMMAND, name, len);
    if (name_is(cmd, cmdlen, "newif") && len > 2 && memcmp(name, "if", 2) == 0) {
        char buf[128];
        if (len - 2 + 5 < sizeof buf) {
            memcpy(buf, name + 2, len - 2);
            memcpy(buf + len - 2, "true", 4);
            define(sc->cb, TS_DEF_COMMAND, buf, len + 2);
            memcpy(buf + len - 2, "false", 5);
            define(sc->cb, TS_DEF_COMMAND, buf, len + 3);
        }
    }
    sc->p = e;
}

/* A babel option naming a language: "british", "ngerman", "en-GB". */
static int is_language_option(const char *s, size_t len)
{
    if (lang_is_name(s, len))
        return 1;
    return memchr(s, '-', len) && lang_code(s, len) != NULL;
}

/* The language named by a list of options [a, b): main=X if given,
 * otherwise the last language listed (babel's rule). */
static int main_from_options(const char *s, size_t a, size_t b,
                             size_t *la, size_t *lb)
{
    int found = 0;
    size_t i = a;
    while (i < b) {
        size_t j = i;
        while (j < b && s[j] != ',')
            j++;
        size_t x = i, y = j;
        trim(s, &x, &y);
        if (y - x > 5 && memcmp(s + x, "main=", 5) == 0) {
            x += 5;
            trim(s, &x, &y);
            *la = x;
            *lb = y;
            return 1;
        }
        if (y > x && is_language_option(s + x, y - x)) {
            *la = x;
            *lb = y;
            found = 1;
        }
        i = j + 1;
    }
    return found;
}

/* Look ahead at the arguments of \documentclass, \usepackage{babel} or
 * polyglossia's \setmainlanguage to find the document's main language.
 * Does not move sc->p. */
static void find_main_language(scanner *sc, const char *cmd, size_t cmdlen)
{
    const char *s = sc->s;
    size_t n = sc->n, oa = 0, ob = 0;
    size_t q = skip_ws(sc, sc->p);
    if (q < n && s[q] == '[') {
        size_t close = match_bracket(s, n, q);
        if (close >= n)
            return;
        oa = q + 1;
        ob = close;
        q = skip_ws(sc, close + 1);
    }
    if (q >= n || s[q] != '{')
        return;
    size_t close = match_brace(s, n, q);
    if (close >= n)
        return;
    size_t na = q + 1, nb = close;
    trim(s, &na, &nb);

    if (name_is(cmd, cmdlen, "documentclass")) {
        sc->class_opts_a = oa;
        sc->class_opts_b = ob;
        return;
    }
    if (name_is(cmd, cmdlen, "setmainlanguage") ||
        name_is(cmd, cmdlen, "setdefaultlanguage")) {
        /* \setmainlanguage[variant=british]{english} */
        size_t i = oa;
        while (i < ob) {
            size_t j = i;
            while (j < ob && s[j] != ',')
                j++;
            size_t x = i, y = j;
            trim(s, &x, &y);
            if (y - x > 8 && memcmp(s + x, "variant=", 8) == 0) {
                x += 8;
                trim(s, &x, &y);
                if (name_is(s + x, y - x, "uk") || name_is(s + x, y - x, "british")) {
                    define(sc->cb, TS_DEF_MAIN_LANGUAGE, "british", 7);
                    return;
                }
                if (name_is(s + x, y - x, "us") || name_is(s + x, y - x, "american")) {
                    define(sc->cb, TS_DEF_MAIN_LANGUAGE, "american", 8);
                    return;
                }
                if (lang_is_name(s + x, y - x)) {
                    define(sc->cb, TS_DEF_MAIN_LANGUAGE, s + x, y - x);
                    return;
                }
            }
            i = j + 1;
        }
        define(sc->cb, TS_DEF_MAIN_LANGUAGE, s + na, nb - na);
        return;
    }

    /* \usepackage[...]{babel} or a list including babel */
    int babel = 0;
    for (size_t i = na; i < nb;) {
        size_t j = i;
        while (j < nb && s[j] != ',')
            j++;
        size_t x = i, y = j;
        trim(s, &x, &y);
        if (name_is(s + x, y - x, "babel"))
            babel = 1;
        i = j + 1;
    }
    if (!babel)
        return;
    size_t la, lb;
    if ((ob > oa && main_from_options(s, oa, ob, &la, &lb)) ||
        (sc->class_opts_b > sc->class_opts_a &&
         main_from_options(s, sc->class_opts_a, sc->class_opts_b, &la, &lb)))
        define(sc->cb, TS_DEF_MAIN_LANGUAGE, s + la, lb - la);
}

static void do_backslash(scanner *sc)
{
    const char *s = sc->s;
    size_t n = sc->n, p = sc->p;
    frame *t = top(sc);

    if (p + 1 >= n) {
        sc->p = n;
        return;
    }
    unsigned char c2 = (unsigned char)s[p + 1];

    if (!is_cmd_letter(sc, c2)) {
        /* control symbol */
        switch (c2) {
        case '(':
            sc->p = p + 2;
            push(sc, M_MATH, C_PAREN, NULL, 0, NULL);
            return;
        case ')':
            sc->p = p + 2;
            pop_to(sc, C_PAREN, NULL);
            return;
        case '[':
            sc->p = p + 2;
            push(sc, M_MATH, C_SQUARE, NULL, 0, NULL);
            return;
        case ']':
            sc->p = p + 2;
            pop_to(sc, C_SQUARE, NULL);
            return;
        case '\\':
            sc->p = p + 2;
            if (sc->p < n && s[sc->p] == '*')
                sc->p++;
            args(sc, "~");
            return;
        default:
            if (t->mode == M_PROSE && is_accent_symbol(c2) && skip_accent(sc, p) > p) {
                read_word(sc);
                return;
            }
            if (c2 >= 0x80) {
                unsigned cp;
                sc->p = p + 1 + utf8_decode(s, n, p + 1, &cp);
            } else {
                sc->p = p + 2;
            }
            return;
        }
    }

    size_t e = p + 1;
    while (e < n && is_cmd_letter(sc, (unsigned char)s[e]))
        e++;
    const char *name = s + p + 1;
    size_t len = e - (p + 1);

    if (t->mode == M_PROSE && skip_accent(sc, p) > p) {
        read_word(sc); /* \c{c}a\c{c}ade, \ss, ... part of a word */
        return;
    }

    sc->p = e;
    if (e < n && s[e] == '*')
        sc->p++;

    int internal = memchr(name, '@', len) || memchr(name, '_', len) ||
                   memchr(name, ':', len);
    int defining = name_in(name, len, def_family);
    /* polyglossia defines \textgerman{..} etc. when the document runs */
    int text_lang = len > 4 && memcmp(name, "text", 4) == 0 &&
                    lang_is_name(name + 4, len - 4);
    if (!internal && !text_lang)
        emit(sc, TS_COMMAND, name, len, p, e);

    if (text_lang) {
        set_lang(sc->pending_lang, name + 4, len - 4);
        sc->have_pending_lang = 1;
        /* the [options] argument must not take the language */
        size_t q = skip_ws(sc, sc->p);
        if (q < n && s[q] == '[')
            sc->p = match_bracket(s, n, q) + 1;
        sc->cmd = name;
        sc->cmdlen = len;
        args(sc, "P");
        return;
    }
    if (name_is(name, len, "input") || name_is(name, len, "include") ||
        name_is(name, len, "subfile") || name_is(name, len, "InputIfFileExists")) {
        /* report the file so the checker can read its definitions */
        size_t q = skip_ws(sc, sc->p);
        if (q < n && s[q] == '{') {
            size_t close = match_brace(s, n, q);
            size_t a = q + 1, b = close < n ? close : n;
            trim(s, &a, &b);
            if (b > a && !memchr(s + a, '\\', b - a) && !memchr(s + a, '#', b - a))
                define(sc->cb, TS_DEF_INPUT, s + a, b - a);
        } else if (name_is(name, len, "input")) {
            /* \input file  (TeX syntax): the name ends at a space */
            size_t b = q;
            while (b < n && !isspace((unsigned char)s[b]) && s[b] != '\\' &&
                   s[b] != '%' && s[b] != '}' && s[b] != '{')
                b++;
            if (b > q)
                define(sc->cb, TS_DEF_INPUT, s + q, b - q);
            sc->p = b;
            return;
        }
    }
    /* biblatex multicites: \autocites(pre)(post)[pre][post]{key}[..]{key}..
     * take any number of arguments, all citation keys or notes */
    if (len > 5 && memcmp(name + len - 5, "cites", 5) == 0) {
        size_t q = sc->p;
        for (;;) {
            q = skip_ws(sc, q);
            if (q >= n)
                break;
            if (s[q] == '(') {
                size_t r = q + 1;
                int d = 1;
                while (r < n && d) {
                    if (s[r] == '(') d++;
                    else if (s[r] == ')') d--;
                    r++;
                }
                q = r;
            } else if (s[q] == '[') {
                q = match_bracket(s, n, q) + 1;
            } else if (s[q] == '{') {
                q = match_brace(s, n, q) + 1;
            } else {
                break;
            }
        }
        sc->p = q < n ? q : n;
        return;
    }
    if (name_is(name, len, "selectlanguage")) {
        size_t a, b;
        if (env_arg(sc, &a, &b))
            set_lang(top(sc)->lang, s + a, b - a);
        return;
    }
    if (name_is(name, len, "documentclass") || name_is(name, len, "usepackage") ||
        name_is(name, len, "RequirePackage") || name_is(name, len, "setmainlanguage") ||
        name_is(name, len, "setdefaultlanguage"))
        find_main_language(sc, name, len);

    if (name_is(name, len, "begin")) { do_begin(sc); return; }
    if (name_is(name, len, "end")) { do_end(sc); return; }
    if (name_is(name, len, "makeatletter")) { sc->at_letter = 1; return; }
    if (name_is(name, len, "makeatother")) { sc->at_letter = 0; return; }
    if (name_is(name, len, "ExplSyntaxOn")) { sc->expl = 1; return; }
    if (name_is(name, len, "ExplSyntaxOff")) { sc->expl = 0; return; }
    if (name_is(name, len, "verb")) {
        sc->p = skip_verb_arg(sc, sc->p);
        return;
    }
    if (name_is(name, len, "lstinline") || name_is(name, len, "mintinline") ||
        name_is(name, len, "mint")) {
        size_t q = sc->p;
        if (q < n && s[q] == '[')
            q = match_bracket(s, n, q) + 1;
        if (!name_is(name, len, "lstinline")) {
            if (q < n && s[q] == '{')
                q = match_brace(s, n, q) + 1; /* language */
        }
        sc->p = skip_verb_arg(sc, q < n ? q : n);
        return;
    }
    if (name_is(name, len, "csname")) {
        size_t q = find_sub(s, n, sc->p, "\\endcsname", 10);
        sc->p = q < n ? q + 10 : n;
        return;
    }
    if (name_is(name, len, "cmidrule")) {
        size_t q = sc->p;
        if (q < n && s[q] == '(') {
            while (q < n && s[q] != ')' && s[q] != '\n')
                q++;
            sc->p = q < n ? q + 1 : n;
        }
    }
    if (defining) {
        do_def(sc, name, len);
        return;
    }
    const char *spec = lookup_spec(name, len);
    sc->cmd = name;
    sc->cmdlen = len;
    args(sc, spec ? spec : "~");
}

/* ------------------------------------------------------------------------
 * Main loop
 * ---------------------------------------------------------------------- */

/* The text we are given may be a fragment (a paragraph or a few lines) cut
 * from the middle of a document. If it contains the end of a maths or
 * verbatim environment before any beginning, assume it starts inside one. */
static void prescan(scanner *sc)
{
    const char *s = sc->s;
    size_t n = sc->n, p = 0;
    while (p < n) {
        char c = s[p];
        if (c == '%') {
            while (p < n && s[p] != '\n')
                p++;
            continue;
        }
        if (c != '\\') {
            p++;
            continue;
        }
        if (p + 1 >= n)
            return;
        char c2 = s[p + 1];
        if (c2 == '[')
            return;
        if (c2 == ']') {
            push(sc, M_MATH, C_SQUARE, NULL, 0, NULL);
            return;
        }
        if (!is_alpha((unsigned char)c2)) {
            p += 2;
            continue;
        }
        size_t e = p + 1;
        while (e < n && is_alpha((unsigned char)s[e]))
            e++;
        int is_begin = name_is(s + p + 1, e - p - 1, "begin");
        int is_end = name_is(s + p + 1, e - p - 1, "end");
        if ((is_begin || is_end) && e < n && s[e] == '{') {
            size_t close = match_brace(s, n, e);
            if (close >= n)
                return;
            const env_entry *env = lookup_env(s + e + 1, close - e - 1);
            if (env && (env->kind == ENV_MATH || env->kind == ENV_VERB ||
                        env->kind == ENV_LANG)) {
                if (is_begin)
                    return;
                if (env->kind == ENV_MATH) {
                    push(sc, M_MATH, C_ENV, s + e + 1, close - e - 1, NULL);
                } else if (env->kind == ENV_LANG) {
                    /* inside another language, but we cannot tell which:
                     * "?" is unknown, so its words are not checked */
                    set_lang(sc->pending_lang, "?", 1);
                    sc->have_pending_lang = 1;
                    push(sc, M_PROSE, C_ENV, s + e + 1, close - e - 1, NULL);
                }
                else
                    sc->p = p;
                return;
            }
            p = close + 1;
            continue;
        }
        p = e;
    }
}

static int fragment_option_line(scanner *sc);

static void run(scanner *sc)
{
    while (sc->p < sc->n) {
        frame *t = top(sc);
        unsigned char c = (unsigned char)sc->s[sc->p];
        const char *resume = NULL;

        /* a line of a key=value list cut off from its \hypersetup{ */
        if (sc->depth == 0 && t->mode == M_PROSE &&
            (sc->p == 0 || sc->s[sc->p - 1] == '\n') && fragment_option_line(sc))
            continue;

        if (t->mode == M_SKIP || t->mode == M_RAW_SKIP) {
            if (c == '\\') {
                sc->p += 2;
            } else if (c == '%' && t->mode == M_SKIP) {
                while (sc->p < sc->n && sc->s[sc->p] != '\n') sc->p++;
            } else if (c == '{') {
                sc->p++;
                push(sc, t->mode, C_BRACE, NULL, 0, NULL);
            } else if ((c == '}' && t->closer == C_BRACE) ||
                       (c == ']' && t->closer == C_BRACKET)) {
                sc->p++;
                resume = pop(sc);
                args(sc, resume);
            } else {
                sc->p++;
            }
            continue;
        }

        switch (c) {
        case '%':
            while (sc->p < sc->n && sc->s[sc->p] != '\n')
                sc->p++;
            break;
        case '\\':
            do_backslash(sc);
            break;
        case '{':
            if (t->mode == M_PROSE && skip_accent(sc, sc->p) > sc->p) {
                read_word(sc);
                break;
            }
            sc->p++;
            push(sc, t->mode, C_BRACE, NULL, 0, NULL);
            break;
        case '}':
            sc->p++;
            if (t->closer == C_BRACE) {
                resume = pop(sc);
                args(sc, resume);
            } else if (pop_to(sc, C_BRACE, &resume)) {
                args(sc, resume);
            }
            break;
        case ']':
            sc->p++;
            if (t->closer == C_BRACKET) {
                resume = pop(sc);
                args(sc, resume);
            }
            break;
        case '$':
            if (sc->p + 1 < sc->n && sc->s[sc->p + 1] == '$') {
                sc->p += 2;
                if (!pop_to(sc, C_DDOLLAR, NULL))
                    push(sc, M_MATH, C_DDOLLAR, NULL, 0, NULL);
            } else {
                sc->p++;
                if (!pop_to(sc, C_DOLLAR, NULL))
                    push(sc, M_MATH, C_DOLLAR, NULL, 0, NULL);
            }
            break;
        default:
            if (is_digit(c)) {
                /* 12pt, 2cm, 3rd: numbers and anything glued to them */
                while (sc->p < sc->n &&
                       (is_alpha((unsigned char)sc->s[sc->p]) ||
                        is_digit((unsigned char)sc->s[sc->p])))
                    sc->p++;
            } else if (letter_at(sc->s, sc->n, sc->p)) {
                if (t->mode == M_PROSE)
                    read_word(sc);
                else
                    skip_word(sc);
            } else if (c >= 0x80) {
                unsigned cp;
                sc->p += utf8_decode(sc->s, sc->n, sc->p, &cp);
            } else {
                sc->p++;
            }
        }
    }
}

/* Scan [a, b) as a separate piece of text in the given mode. */
static void sub_scan(scanner *sc, size_t a, size_t b, mode m)
{
    scanner *sub = calloc(1, sizeof *sub);
    if (!sub)
        return;
    sub->s = sc->s;
    sub->n = b;
    sub->p = a;
    sub->cb = sc->cb;
    sub->st[0].mode = m;
    memcpy(sub->st[0].lang, top(sc)->lang, MAX_LANG);
    memcpy(sub->announced, sc->announced, MAX_LANG);
    sub->have_announced = sc->have_announced;
    sub->st[0].closer = C_BASE;
    sub->at_letter = sc->at_letter;
    sub->expl = sc->expl;
    run(sub);
    memcpy(sc->announced, sub->announced, MAX_LANG);
    sc->have_announced = sub->have_announced;
    free(sub);
}

/* Keys whose values are text that ends up in the document or the PDF. */
static const char *const prose_keys[] = {
    "pdftitle", "pdfauthor", "pdfsubject", "pdfkeywords", NULL
};

static int valid_key(const char *s, size_t len)
{
    if (!len || len > 64 || !is_alpha((unsigned char)s[0]))
        return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!(is_alpha(c) || is_digit(c) || c == '-' || c == ' '))
            return 0;
    }
    return 1;
}

/* Handle the content [a, b) of a key=value list such as the argument of
 * \hypersetup. Each key is reported; each value is scanned as prose for
 * text keys like pdftitle and as key=value material otherwise. */
static void option_list(scanner *sc, size_t a, size_t b)
{
    const char *s = sc->s;
    size_t i = a;
    while (i < b) {
        size_t j, eq = b;
        int d = 0;
        for (j = i; j < b; j++) {
            char c = s[j];
            if (c == '\\') { j++; continue; }
            if (c == '%') {
                while (j < b && s[j] != '\n')
                    j++;
                continue;
            }
            if (c == '{') d++;
            else if (c == '}') d--;
            else if (d == 0 && c == ',') break;
            else if (d == 0 && c == '=' && eq == b) eq = j;
        }
        if (j > b)
            j = b;
        size_t ka = i, kb = eq < j ? eq : j;
        /* skip comment lines before the key */
        for (;;) {
            trim(s, &ka, &kb);
            if (ka < kb && s[ka] == '%') {
                while (ka < kb && s[ka] != '\n')
                    ka++;
                continue;
            }
            break;
        }
        int prose = 0;
        if (valid_key(s + ka, kb - ka)) {
            if (sc->cb && sc->cb->option)
                sc->cb->option(sc->cb->ctx, sc->cmd, sc->cmdlen, s + ka, kb - ka,
                               ka, kb);
            for (const char *const *k = prose_keys; *k; k++)
                if (strlen(*k) == kb - ka && memcmp(*k, s + ka, kb - ka) == 0)
                    prose = 1;
        }
        if (eq < j) {
            size_t va = eq + 1, vb = j;
            trim(s, &va, &vb);
            if (va < vb && s[va] == '{' && match_brace(s, vb, va) == vb - 1) {
                va++;
                vb--;
            }
            if (va < vb)
                sub_scan(sc, va, vb, prose ? M_PROSE : M_KV);
        }
        i = j + 1;
    }
}

/* While typing, the spell checker is usually given only the line being
 * edited, so a line such as
 *     pdftitle={Nuclear Decommissioning},
 * arrives without the \hypersetup{ above it. If a line at the top level
 * starts "name=" treat it as one item of an option list: the name is not
 * spell-checked (names starting "pdf" are checked as hyperref options),
 * and the value is prose for pdftitle and the like. Returns 1 if the line
 * was handled. */
static int fragment_option_line(scanner *sc)
{
    const char *s = sc->s;
    size_t n = sc->n, q = sc->p;
    int items = 0;

    for (;;) {
        while (q < n && (s[q] == ' ' || s[q] == '\t'))
            q++;
        size_t ka = q;
        if (q >= n || !is_alpha((unsigned char)s[q]))
            break;
        while (q < n && (is_alpha((unsigned char)s[q]) || is_digit((unsigned char)s[q]) ||
                         s[q] == '-'))
            q++;
        size_t kb = q;
        while (q < n && (s[q] == ' ' || s[q] == '\t'))
            q++;
        int has_value = q < n && s[q] == '=';
        int pdf_key = kb - ka > 3 && memcmp(s + ka, "pdf", 3) == 0;
        static const char *const continuation_keys[] = {
            "colorlinks", "linkcolor", "urlcolor", "citecolor", "filecolor",
            "bookmarks", "bookmarksnumbered", "hidelinks", NULL
        };
        int continuation = sc->pdf_fragment && name_in(s + ka, kb - ka, continuation_keys);
        int known = !pdf_key && !continuation && sc->cb && sc->cb->is_option_key &&
                    sc->cb->is_option_key(sc->cb->ctx, s + ka, kb - ka);
        if (!pdf_key && !continuation && !known)
            break; /* ambiguous name=value text remains prose */
        if (known && has_value && !(items > 0 || sc->pdf_fragment)) {
            /* "width = the value" is prose: a first item named only by a
             * known key needs a value without spaces, such as "blue" or
             * "{2cm}" */
            size_t v = q + 1;
            while (v < n && (s[v] == ' ' || s[v] == '\t'))
                v++;
            int d = 0, spaced = 0;
            for (; v < n && s[v] != '\n'; v++) {
                if (s[v] == '{') d++;
                else if (s[v] == '}') { if (d == 0) break; d--; }
                else if (d == 0 && s[v] == ',') break;
                else if (d == 0 && (s[v] == ' ' || s[v] == '\t')) {
                    size_t w = v;
                    while (w < n && (s[w] == ' ' || s[w] == '\t'))
                        w++;
                    if (w < n && s[w] != ',' && s[w] != '\n') spaced = 1;
                    v = w - 1;
                }
            }
            if (spaced)
                break;
        }
        /* the first item must be name=value; later ones may be bare names
         * ("bookmarks, hidelinks") */
        if (kb - ka < 2 || (!has_value &&
                            (items == 0 || !(q >= n || s[q] == ',' || s[q] == '\n'))))
            break;

        size_t va = q + 1, vb = va;
        if (has_value) {
            /* the value runs to a top-level comma, the end of the line, or
             * an unmatched closing brace */
            int d = 0;
            while (vb < n) {
                char c = s[vb];
                if (c == '\\') { vb += 2; continue; }
                if (c == '{') d++;
                else if (c == '}') { if (d == 0) break; d--; }
                else if (d == 0 && (c == ',' || c == '\n')) break;
                vb++;
            }
            if (vb > n)
                vb = n;
        } else {
            va = vb = q;
        }
        size_t end = vb < n && s[vb] == ',' ? vb + 1 : vb;

        if (pdf_key) sc->pdf_fragment = 1;
        int is_pdf = !known; /* a PDF option or one that follows it: check as hyperref's */
        if (sc->cb && sc->cb->option)
            sc->cb->option(sc->cb->ctx, is_pdf ? "hypersetup" : "", is_pdf ? 10 : 0,
                           s + ka, kb - ka, ka, kb);
        int prose = 0;
        for (const char *const *k = prose_keys; *k; k++)
            if (strlen(*k) == kb - ka && memcmp(*k, s + ka, kb - ka) == 0)
                prose = 1;
        trim(s, &va, &vb);
        if (va < vb && s[va] == '{' && match_brace(s, vb, va) == vb - 1) {
            va++;
            vb--;
        }
        if (va < vb)
            sub_scan(sc, va, vb, prose ? M_PROSE : M_KV);
        items++;
        sc->p = q = end;
        if (q >= n || s[q] == '\n' || s[q] == '}')
            break;
    }
    return items > 0;
}

void ts_scan(const char *src, size_t len, const ts_callbacks *cb)
{
    scanner *sc = calloc(1, sizeof *sc);
    if (!sc)
        return;
    sc->s = src;
    sc->n = len;
    sc->cb = cb;
    sc->st[0].mode = M_PROSE;
    sc->st[0].closer = C_BASE;

    prescan(sc);

    run(sc);
    free(sc);
}

/* ------------------------------------------------------------------------
 * Package scanning
 * ---------------------------------------------------------------------- */

static const char *const env_definers[] = {
    "begin", "end", "newenvironment", "renewenvironment", "provideenvironment",
    "NewDocumentEnvironment", "RenewDocumentEnvironment",
    "ProvideDocumentEnvironment", "DeclareDocumentEnvironment", "NewEnviron",
    "RenewEnviron", "newtheorem", "declaretheorem", "newfloat",
    "DeclareFloatingEnvironment", "lstnewenvironment", "newtcolorbox",
    "newtcblisting", "newtcbtheorem", "NewTColorBox", "DeclareTColorBox",
    "NewTCBListing", "newmdenv", "newlist", "surroundwithmdframed",
    "newcases", "renewcases",
    NULL
};

static const char *const alg_definers[] = {
    "algdef", "algblock", "algblockdefx", "algblockx", "algloop",
    "algloopdefx", "algcblock", "algcblockdefx", "algcblockx", "algcloop",
    "algcloopdefx", NULL
};

static const char *const package_loaders[] = {
    "RequirePackage", "usepackage", "RequirePackageWithOptions", NULL
};
static const char *const class_loaders[] = {
    "LoadClass", "LoadClassWithOptions", NULL
};
static const char *const input_commands[] = {
    "input", "@input", "@@input", "InputIfFileExists", "@input@", NULL
};

/* Commands that define option keys, and which brace argument (0-based,
 * ignoring [..] arguments) holds the key or a comma separated list of keys.
 * A negative index means the argument holds expl3/l3keys style
 * "key .property = value" items. */
typedef struct key_definer {
    const char *name;
    int arg;
} key_definer;

static const key_definer key_definers[] = {
    {"define@key", 1}, {"define@cmdkey", 1}, {"define@cmdkeys", 1},
    {"define@choicekey", 1}, {"define@boolkey", 1}, {"define@boolkeys", 1},
    {"enitkv@key", 1}, {"lst@Key", 0}, {"DeclareCaptionOption", 0},
    {"DeclareStringOption", 0}, {"DeclareBoolOption", 0},
    {"DeclareComplementaryOption", 0}, {"DeclareVoidOption", 0},
    {"DeclareOption", 0}, {"Hy@DefNameKey", 0},
    {"keys_define:nn", -2}, {"keys_define:nx", -2}, {"keys_define:ne", -2},
    {"DeclareKeys", -1},
    {NULL, 0}
};

static void define_key_list(const ts_callbacks *cb, const char *s, size_t a, size_t b)
{
    size_t i = a;
    while (i < b) {
        size_t j = i;
        while (j < b && s[j] != ',')
            j++;
        size_t x = i, y = j;
        trim(s, &x, &y);
        if (valid_key(s + x, y - x))
            define(cb, TS_DEF_KEY, s + x, y - x);
        i = j + 1;
    }
}

/* l3keys items: "key .code:n = {...}, other-key .bool_set:N = \l_x". */
static void define_l3keys(const ts_callbacks *cb, const char *s, size_t a, size_t b)
{
    size_t i = a;
    while (i < b) {
        size_t j = i, dot = b;
        int d = 0;
        for (; j < b; j++) {
            char c = s[j];
            if (c == '\\') { j++; continue; }
            if (c == '%') {
                while (j < b && s[j] != '\n')
                    j++;
                continue;
            }
            if (c == '{') d++;
            else if (c == '}') d--;
            else if (d == 0 && c == ',') break;
            else if (d == 0 && c == '.' && dot == b) dot = j;
        }
        if (dot < j) {
            size_t x = i, y = dot;
            trim(s, &x, &y);
            if (valid_key(s + x, y - x))
                define(cb, TS_DEF_KEY, s + x, y - x);
        }
        i = j + 1;
    }
}

/* Find the n-th (0-based) brace argument after q, skipping *, + and [..]
 * arguments. Sets [*a, *b) to its content. */
static int nth_brace_arg(const char *s, size_t len, size_t q, int n, size_t *a, size_t *b)
{
    for (int k = 0;; k++) {
        for (;;) {
            if (q < len && s[q] == '%') {
                while (q < len && s[q] != '\n') q++;
                if (q < len) q++;
                continue;
            }
            while (q < len && (s[q] == ' ' || s[q] == '\t' || s[q] == '\n' ||
                               s[q] == '\r' || s[q] == '*' || s[q] == '+'))
                q++;
            if (q < len && s[q] == '[') {
                q = match_bracket(s, len, q) + 1;
                continue;
            }
            break;
        }
        if (q >= len || s[q] != '{')
            return 0;
        size_t close = match_brace(s, len, q);
        if (close >= len)
            return 0;
        if (k == n) {
            *a = q + 1;
            *b = close;
            return 1;
        }
        q = close + 1;
    }
}

/* keyval stores key "k" of family "f" as the macro \KV@f@k, and enumitem
 * as \enitkv@enumitem@k. Report the key if name is such a macro. */
static int key_macro(const ts_callbacks *cb, const char *name, size_t nl)
{
    const char *key = NULL;
    if (nl > 4 && memcmp(name, "KV@", 3) == 0) {
        const char *at = memchr(name + 3, '@', nl - 3);
        if (at)
            key = at + 1;
    } else if (nl > 16 && memcmp(name, "enitkv@enumitem@", 16) == 0) {
        key = name + 16;
    }
    if (!key)
        return 0;
    size_t kl = (size_t)(name + nl - key);
    if (valid_key(key, kl))
        define(cb, TS_DEF_KEY, key, kl);
    return 1;
}

/* Commands whose next control word is the name of a command being
 * defined: \def\foo, \newcommand{\foo}, \DeclareMathSymbol{\alpha}... */
static const char *const definers[] = {
    "def", "gdef", "edef", "xdef", "let", "futurelet", "chardef",
    "mathchardef", "countdef", "dimendef", "skipdef", "toksdef", "font",
    "newcommand", "renewcommand", "providecommand", "DeclareRobustCommand",
    "NewDocumentCommand", "RenewDocumentCommand", "ProvideDocumentCommand",
    "DeclareDocumentCommand", "NewExpandableDocumentCommand",
    "RenewExpandableDocumentCommand", "DeclareExpandableDocumentCommand",
    "NewCommandCopy", "RenewCommandCopy", "DeclareCommandCopy",
    "newrobustcmd", "renewrobustcmd", "providerobustcmd", "robustify",
    "DeclareTextFontCommand", "DeclareOldFontCommand", "DeclareMathSymbol",
    "DeclareMathAlphabet", "DeclareSymbolFontAlphabet", "DeclareMathAccent",
    "DeclareMathDelimiter", "DeclareMathRadical", "DeclareMathOperator",
    "DeclareTextSymbol", "DeclareTextCommand", "DeclareTextAccent",
    "DeclareTextCommandDefault", "DeclareTextSymbolDefault",
    "ProvideTextCommand", "ProvideTextCommandDefault", "DeclareTextComposite",
    "DeclareUnicodeCharacter", "DeclarePairedDelimiter",
    "DeclarePairedDelimiterX", "DeclareSIUnit", "DeclareSIPrefix",
    "DeclareSIPower", "DeclareSIQualifier", "newlength", "newcount",
    "newdimen", "newskip", "newmuskip", "newtoks", "newbox", "newread",
    "newwrite", "newif", "newsavebox", "newlanguage", "newinsert", "newfam",
    "algnewcommand", "algrenewcommand", "newtheorem", "newcolumntype",
    "NewAcroCommand", "RenewAcroCommand", "DeclareAcroCommand",
    "ProvideAcroCommand", "DeclareCiteCommand", "DeclareMultiCiteCommand",
    "newbibmacro", "DeclareBibliographyDriver",
    "pgfkeys", "tikzset", NULL
};

/* May come between a definer and the name: \global\let\foo,
 * \expandafter\def\csname ... */
static const char *const def_prefixes[] = {
    "global", "long", "protected", "outer", "expandafter", "noexpand",
    "relax", NULL
};

static int is_definer(const char *name, size_t len)
{
    if (name_in(name, len, definers))
        return 1;
    /* expl3: \cs_new:Npn \Foo, \cs_gset_protected:Npn \Foo, ...
     * siunitx: \siunitx_declare_unit:Nn \metre, ..._prefix:Nnn \kilo */
    static const char *const expl[] = {
        "cs_new", "cs_set", "cs_gset", "cs_gnew", "siunitx_declare"
    };
    for (size_t i = 0; i < sizeof expl / sizeof expl[0]; i++) {
        size_t l = strlen(expl[i]);
        if (len > l && memcmp(name, expl[i], l) == 0 &&
            (name[l] == ':' || name[l] == '_'))
            return 1;
    }
    return 0;
}

static int all_letters(const char *s, size_t len)
{
    if (!len)
        return 0;
    for (size_t i = 0; i < len; i++)
        if (!is_alpha((unsigned char)s[i]))
            return 0;
    return 1;
}

/* After an env-defining or package-loading command: skip '*', spaces,
 * optional [..] arguments and beamer overlay specifications <..>; return
 * the position of '{' or n. */
static size_t to_brace_arg(const scanner *sc, size_t q)
{
    for (;;) {
        q = skip_ws(sc, q);
        while (q < sc->n && (sc->s[q] == ' ' || sc->s[q] == '\t' ||
                             sc->s[q] == '\n' || sc->s[q] == '\r' ||
                             sc->s[q] == '*'))
            q++;
        if (q < sc->n && sc->s[q] == '[') {
            q = match_bracket(sc->s, sc->n, q) + 1;
            continue;
        }
        if (q < sc->n && sc->s[q] == '<') {
            size_t r = q + 1;
            while (r < sc->n && r < q + 64 && sc->s[r] != '>' && sc->s[r] != '{' &&
                   sc->s[r] != '\n')
                r++;
            if (r < sc->n && sc->s[r] == '>') {
                q = r + 1;
                continue;
            }
        }
        break;
    }
    return q < sc->n && sc->s[q] == '{' ? q : sc->n;
}

void ts_scan_package(const char *src, size_t len, const ts_callbacks *cb,
                     void (*input)(void *ctx, const char *name, size_t len))
{
    scanner sc_storage;
    scanner *sc = &sc_storage;
    memset(sc, 0, sizeof *sc);
    sc->s = src;
    sc->n = len;
    sc->cb = cb;
    sc->at_letter = 1;
    sc->expl = 1; /* treat _ and : as letters so expl3 names are excluded */

    size_t p = 0;
    int defining = 0; /* how many of the next control words are defined */
    while (p < len) {
        char c = src[p];
        if (c == '%') {
            while (p < len && src[p] != '\n')
                p++;
            continue;
        }
        if (c != '\\') {
            p++;
            continue;
        }
        size_t e = read_cs(sc, p + 1);
        const char *name = src + p + 1;
        size_t nl = e - (p + 1);
        p = e;
        if (nl == 0 || !is_cmd_letter(sc, (unsigned char)name[0]))
            continue;

        int was_defining = defining;
        if (all_letters(name, nl))
            define(cb, TS_DEF_MENTION, name, nl);
        if (was_defining && !name_in(name, nl, def_prefixes)) {
            /* (\global\let\foo: a prefix keeps us waiting for the name) */
            if (all_letters(name, nl))
                define(cb, TS_DEF_COMMAND, name, nl);
            defining--;
        }
        if (is_definer(name, nl)) {
            /* \siunitx_declare_power:NNn \square \squared defines two */
            defining = nl >= 21 && memcmp(name, "siunitx_declare_power", 21) == 0 ? 2 : 1;
        }

        key_macro(cb, name, nl);

        for (const key_definer *kd = key_definers; kd->name; kd++) {
            if (!name_is(name, nl, kd->name))
                continue;
            int which = kd->arg < 0 ? -kd->arg - 1 : kd->arg;
            size_t a, b;
            if (nth_brace_arg(src, len, p, which, &a, &b)) {
                if (kd->arg < 0)
                    define_l3keys(cb, src, a, b);
                else
                    define_key_list(cb, src, a, b);
            }
            break;
        }

        /* algorithmicx: \algdef{SE}[IF]{If}{EndIf}... defines \If, \EndIf */
        if (name_in(name, nl, alg_definers)) {
            int skip_first = name_is(name, nl, "algdef");
            size_t a, b;
            for (int k = skip_first; nth_brace_arg(src, len, p, k, &a, &b); k++) {
                /* stop at the first argument that is not a bare name, or
                 * at an [n] argument count */
                size_t prev_end = b;
                trim(src, &a, &b);
                if (!all_letters(src + a, b - a))
                    break;
                define(cb, TS_DEF_COMMAND, src + a, b - a);
                size_t q = prev_end + 1;
                while (q < len && (src[q] == ' ' || src[q] == '\n'))
                    q++;
                if (q < len && src[q] == '[')
                    break;
            }
        }

        if (name_in(name, nl, env_definers)) {
            size_t q = to_brace_arg(sc, p);
            if (q < len) {
                size_t close = match_brace(src, len, q);
                size_t a = q + 1, b = close;
                trim(src, &a, &b);
                if (b <= len && valid_env_name(src + a, b - a))
                    define(cb, TS_DEF_ENVIRONMENT, src + a, b - a);
            }
        } else if (name_is(name, nl, "newcounter")) {
            size_t q = to_brace_arg(sc, p);
            if (q < len) {
                size_t close = match_brace(src, len, q);
                size_t a = q + 1, b = close;
                trim(src, &a, &b);
                if (b <= len && all_letters(src + a, b - a))
                    define(cb, TS_DEF_COUNTER, src + a, b - a);
            }
        } else if (name_is(name, nl, "newif")) {
            scanner tmp = *sc;
            tmp.p = p;
            do_def(&tmp, name, nl);
        } else if (name_in(name, nl, package_loaders) ||
                   name_in(name, nl, class_loaders)) {
            size_t q = to_brace_arg(sc, p);
            if (q < len) {
                size_t close = match_brace(src, len, q);
                sc->cb = cb;
                name_arg(sc, name_in(name, nl, class_loaders) ? 'L' : 'G',
                         q + 1, close);
            }
        } else if (name_in(name, nl, input_commands) && input) {
            size_t q = p;
            while (q < len && (src[q] == ' ' || src[q] == '\t'))
                q++;
            size_t a, b;
            if (q < len && src[q] == '{') {
                size_t close = match_brace(src, len, q);
                a = q + 1;
                b = close;
            } else {
                a = q;
                b = q;
                while (b < len && !isspace((unsigned char)src[b]) &&
                       src[b] != '\\' && src[b] != '}' && src[b] != '{' &&
                       src[b] != '%')
                    b++;
            }
            trim(src, &a, &b);
            if (b > a && b <= len && !memchr(src + a, '\\', b - a) &&
                !memchr(src + a, '#', b - a))
                input(cb->ctx, src + a, b - a);
        } else if (name_is(name, nl, "csname") || name_is(name, nl, "@namedef")) {
            /* \@namedef{KV@f@k} and \csname KV@f@k\endcsname also define keys */
            size_t a = p, b;
            if (name_is(name, nl, "@namedef")) {
                if (a >= len || src[a] != '{')
                    continue;
                b = match_brace(src, len, a);
                a++;
            } else {
                b = find_sub(src, len, a, "\\endcsname", 10);
            }
            trim(src, &a, &b);
            if (b > a && b <= len && key_macro(cb, src + a, b - a)) {
                /* a key's macro, reported by key_macro */
            } else if (b > a && b <= len) {
                if (all_letters(src + a, b - a))
                    define(cb, TS_DEF_COMMAND, src + a, b - a);
                else if (valid_env_name(src + a, b - a))
                    define(cb, TS_DEF_ENVIRONMENT, src + a, b - a);
            }
        }
    }
}
