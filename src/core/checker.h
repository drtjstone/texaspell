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
 * and commands examined. */
size_t ta_check(texaspell *ta, const char *text, size_t len,
                ta_issue **out, size_t *nwords);

/* Suggestions for a flagged word. A word starting with a backslash is
 * treated as a command; an environment name or option key flagged recently
 * is treated as one. Each out[i] is malloc'd. */
size_t ta_suggest(texaspell *ta, const char *word, size_t len,
                  char **out, size_t max);

/* "Learn Spelling" / "Unlearn Spelling". */
void ta_learn(texaspell *ta, const char *word, size_t len);
void ta_forget(texaspell *ta, const char *word, size_t len);

const char *ta_home(const texaspell *ta);
const char *ta_dictionary(const texaspell *ta);
int ta_commands_enabled(const texaspell *ta);
/* A problem worth telling the user about (e.g. TeX not found), or NULL. */
const char *ta_warning(const texaspell *ta);

/* Fill map[0..len] with the UTF-16 offset of each byte offset of the UTF-8
 * text s (for converting ranges for Cocoa). */
void ta_utf16_map(const char *s, size_t len, size_t *map);

#endif
