/* checker.h - the TeXAspell engine: LaTeX-aware spell checking with aspell
 * dictionaries and validation of LaTeX command and environment names.
 *
 * Used by both the command-line tool and the macOS spell service. Not
 * thread-safe: callers must serialise access to one texaspell object.
 */
#ifndef TEXASPELL_CHECKER_H
#define TEXASPELL_CHECKER_H

#include <stddef.h>

typedef struct texaspell texaspell;

typedef enum {
    TA_WORD = 0,        /* misspelt prose word        */
    TA_COMMAND = 1,     /* unknown \command           */
    TA_ENVIRONMENT = 2, /* unknown environment name   */
    TA_OPTION = 3       /* unknown key in \hypersetup{..} and similar */
} ta_kind;

typedef struct ta_issue {
    size_t start, end; /* byte offsets into the checked text */
    ta_kind kind;
} ta_issue;

/* home: settings folder, or NULL for the default
 * (~/Library/Application Support/TeXAspell on macOS, ~/.config/texaspell
 * elsewhere, or $TEXASPELL_HOME if set). */
texaspell *ta_open(const char *home, char *err, size_t errlen);
void ta_close(texaspell *ta);

/* Check UTF-8 text. Returns the number of issues and sets *out to a
 * malloc'd array (free it). *nwords, if not NULL, gets the number of words
 * and commands examined. Each call has independent inferred definitions,
 * package additions and document language. Only explicit user allowlists
 * and configured base packages persist between calls. */
size_t ta_check(texaspell *ta, const char *text, size_t len,
                ta_issue **out, size_t *nwords);

/* Suggestions for a flagged word. A word starting with a backslash is
 * treated as a command. Other names use categories from the latest check
 * only; ambiguous categories default to prose. Each out[i] is malloc'd. */
size_t ta_suggest(texaspell *ta, const char *word, size_t len,
                  char **out, size_t max);

/* "Learn Spelling" / "Unlearn Spelling". */
void ta_learn(texaspell *ta, const char *word, size_t len);
void ta_forget(texaspell *ta, const char *word, size_t len);

const char *ta_home(const texaspell *ta);

/* The folder of the document being checked, so \input{preamble} can be
 * found there (NULL or "" when unknown, as for the spell service). */
void ta_set_document_dir(texaspell *ta, const char *dir);

/* A few lines describing the TeX installation found, for --info. */
void ta_describe(const texaspell *ta, char *out, size_t outlen);
const char *ta_dictionary(const texaspell *ta);

/* Choose the language for later ta_check / ta_suggest calls, e.g. "en_US".
 * NULL or "en_GB" means the British dictionary set in config.txt; any
 * other language uses the aspell dictionary of the same name unless
 * config.txt says otherwise (dictionary.en_US = ...). Returns 0, with a
 * message in err, if the dictionary cannot be opened; the language in use
 * is then unchanged. LaTeX checking and your own word lists are shared by
 * all languages. */
int ta_use_language(texaspell *ta, const char *lang, char *err, size_t errlen);
int ta_commands_enabled(const texaspell *ta);
/* A problem worth telling the user about (e.g. TeX not found), or NULL. */
const char *ta_warning(const texaspell *ta);

/* Fill map[0..len] with the UTF-16 offset of each byte offset of the UTF-8
 * text s (for converting ranges for Cocoa). */
void ta_utf16_map(const char *s, size_t len, size_t *map);

#endif
