/* scan.h - split LaTeX source into prose words, command names and
 * environment names, and report definitions found along the way.
 *
 * The scanner knows nothing about dictionaries. It only decides what each
 * piece of the source is:
 *
 *   - prose words (to be checked against the aspell dictionary);
 *   - control words such as \section (to be checked against the list of
 *     known LaTeX commands);
 *   - environment names in \begin{...} / \end{...};
 *   - things to skip: maths variables, labels, citation keys, file names,
 *     verbatim text, comments, key=value options and so on.
 *
 * All offsets are byte offsets into the UTF-8 source.
 */
#ifndef TEXASPELL_SCAN_H
#define TEXASPELL_SCAN_H

#include <stddef.h>

typedef enum {
    TS_WORD,        /* text: the word, normalised (\- removed, ’ -> ') */
    TS_COMMAND,     /* text: command name without the backslash        */
    TS_ENVIRONMENT, /* text: environment name, e.g. "itemize"          */
    TS_LANGUAGE     /* the words that follow are in this language: a
                     * babel/polyglossia name such as "ngerman", or ""
                     * for the document's main language                */
} ts_kind;

typedef enum {
    TS_DEF_COMMAND,     /* \newcommand{\foo}, \def\foo, ...  name = "foo"   */
    TS_DEF_MENTION,     /* package files only: \foo appears, defined or not */
    TS_DEF_ENVIRONMENT, /* \newenvironment{foo}, \newtheorem{foo}, ...      */
    TS_DEF_COUNTER,     /* \newcounter{foo}  (implies \thefoo)              */
    TS_DEF_PACKAGE,     /* \usepackage{foo}, \RequirePackage{foo}           */
    TS_DEF_CLASS,       /* \documentclass{foo}, \LoadClass{foo}             */
    TS_DEF_KEY,         /* an option key: \define@key{Hyp}{pdftitle}       */
    TS_DEF_MAIN_LANGUAGE, /* \usepackage[british]{babel}: name = "british"  */
    TS_DEF_INPUT        /* documents: \input{preamble}, \include{chap1}     */
} ts_def_kind;

typedef struct ts_callbacks {
    void *ctx;
    /* A token to check. [start, end) is the source range to underline: for a
     * command it includes the backslash. */
    void (*token)(void *ctx, ts_kind kind, const char *text, size_t len,
                  size_t start, size_t end);
    void (*define)(void *ctx, ts_def_kind kind, const char *name, size_t len);
    /* An option key in a key=value list such as \hypersetup{pdftitle=..}.
     * cmd is the command it was given to (without backslash). */
    void (*option)(void *ctx, const char *cmd, size_t cmdlen,
                   const char *key, size_t keylen, size_t start, size_t end);
} ts_callbacks;

void ts_scan(const char *src, size_t len, const ts_callbacks *cb);

/* Scan a package/class/kernel file (.sty, .cls, .ltx, ...) for the names it
 * makes available. Reports every control word made only of letters as
 * TS_DEF_MENTION, and those it defines (\def\foo, \newcommand\foo,
 * \DeclareMathSymbol{\foo}, ...) also as TS_DEF_COMMAND; environment names as TS_DEF_ENVIRONMENT, option keys as
 * TS_DEF_KEY, and files it
 * loads as TS_DEF_PACKAGE / TS_DEF_CLASS. Files read with \input are
 * reported through `input` (name as written, may lack an extension). */
void ts_scan_package(const char *src, size_t len, const ts_callbacks *cb,
                     void (*input)(void *ctx, const char *name, size_t len));

#endif
