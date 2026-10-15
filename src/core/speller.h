/* speller.h - thin wrapper around the GNU Aspell C library. */
#ifndef TEXASPELL_SPELLER_H
#define TEXASPELL_SPELLER_H

#include <stddef.h>

typedef struct speller speller;

/* dict is an aspell dictionary name such as "en_GB-ise" or "en_GB".
 * On failure returns NULL and, if err is not NULL, writes a message. */
speller *speller_open(const char *dict, char *err, size_t errlen);
void speller_close(speller *sp);

/* 1 if correct, 0 if not. */
int speller_check(speller *sp, const char *word, size_t len);

/* Fill out[] with up to max suggestions (malloc'd strings the caller frees).
 * Returns the number written. */
size_t speller_suggest(speller *sp, const char *word, size_t len,
                       char **out, size_t max);

#endif
