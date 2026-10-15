/* strset.c - a simple open-addressing hash set of strings. */
#include "strset.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint64_t hash_bytes(const char *p, size_t n)
{
    uint64_t h = 1469598103934665603ULL; /* FNV-1a */
    for (size_t i = 0; i < n; i++) {
        h ^= (unsigned char)p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

void strset_init(strset *s)
{
    s->slots = NULL;
    s->cap = 0;
    s->count = 0;
}

void strset_free(strset *s)
{
    for (size_t i = 0; i < s->cap; i++)
        free(s->slots[i]);
    free(s->slots);
    strset_init(s);
}

static int slot_matches(const char *slot, const char *str, size_t len)
{
    return strncmp(slot, str, len) == 0 && slot[len] == '\0';
}

static int grow(strset *s)
{
    size_t ncap = s->cap ? s->cap * 2 : 1024;
    char **nslots = calloc(ncap, sizeof *nslots);
    if (!nslots)
        return -1;
    for (size_t i = 0; i < s->cap; i++) {
        char *v = s->slots[i];
        if (!v)
            continue;
        size_t j = hash_bytes(v, strlen(v)) & (ncap - 1);
        while (nslots[j])
            j = (j + 1) & (ncap - 1);
        nslots[j] = v;
    }
    free(s->slots);
    s->slots = nslots;
    s->cap = ncap;
    return 0;
}

int strset_addn(strset *s, const char *str, size_t len)
{
    if ((s->count + 1) * 2 > s->cap && grow(s) != 0)
        return -1;
    size_t j = hash_bytes(str, len) & (s->cap - 1);
    while (s->slots[j]) {
        if (slot_matches(s->slots[j], str, len))
            return 0;
        j = (j + 1) & (s->cap - 1);
    }
    char *copy = malloc(len + 1);
    if (!copy)
        return -1;
    memcpy(copy, str, len);
    copy[len] = '\0';
    s->slots[j] = copy;
    s->count++;
    return 1;
}

int strset_add(strset *s, const char *str)
{
    return strset_addn(s, str, strlen(str));
}

int strset_hasn(const strset *s, const char *str, size_t len)
{
    if (!s->cap)
        return 0;
    size_t j = hash_bytes(str, len) & (s->cap - 1);
    while (s->slots[j]) {
        if (slot_matches(s->slots[j], str, len))
            return 1;
        j = (j + 1) & (s->cap - 1);
    }
    return 0;
}

int strset_has(const strset *s, const char *str)
{
    return strset_hasn(s, str, strlen(str));
}
