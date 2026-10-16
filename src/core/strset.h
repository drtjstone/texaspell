/* strset.h - a simple open-addressing hash set of strings, each carrying
 * an unsigned value (0 when added). */
#ifndef TEXASPELL_STRSET_H
#define TEXASPELL_STRSET_H

#include <stddef.h>

typedef struct strset {
    char **slots;
    unsigned *vals; /* vals[i] belongs to slots[i] */
    size_t cap;   /* always a power of two */
    size_t count;
} strset;

void strset_init(strset *s);
void strset_free(strset *s);

/* Adds a copy of the first len bytes of str. Returns 1 if newly added, 0 if
 * already present, -1 on allocation failure. */
int strset_addn(strset *s, const char *str, size_t len);
int strset_add(strset *s, const char *str);

int strset_hasn(const strset *s, const char *str, size_t len);
int strset_has(const strset *s, const char *str);

/* The value stored with str, or NULL if str is not in the set. */
unsigned *strset_valn(const strset *s, const char *str, size_t len);

/* Iteration: for (size_t i = 0; i < s->cap; i++) if (s->slots[i]) ... */

#endif
