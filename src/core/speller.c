/* speller.c - thin wrapper around the GNU Aspell C library. */
#include "speller.h"

#include <aspell.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct speller {
    AspellSpeller *sp;
};

speller *speller_open(const char *dict, char *err, size_t errlen)
{
    AspellConfig *cfg = new_aspell_config();
    if (!cfg) {
        if (err) snprintf(err, errlen, "aspell: cannot create config");
        return NULL;
    }
    aspell_config_replace(cfg, "master", dict);
    aspell_config_replace(cfg, "encoding", "utf-8");
    AspellCanHaveError *r = new_aspell_speller(cfg);
    delete_aspell_config(cfg);
    if (aspell_error_number(r) != 0) {
        if (err) snprintf(err, errlen, "aspell: %s", aspell_error_message(r));
        delete_aspell_can_have_error(r);
        return NULL;
    }
    speller *s = malloc(sizeof *s);
    if (!s) {
        delete_aspell_speller(to_aspell_speller(r));
        return NULL;
    }
    s->sp = to_aspell_speller(r);
    return s;
}

void speller_close(speller *s)
{
    if (!s)
        return;
    delete_aspell_speller(s->sp);
    free(s);
}

int speller_check(speller *s, const char *word, size_t len)
{
    return aspell_speller_check(s->sp, word, (int)len) == 1;
}

size_t speller_suggest(speller *s, const char *word, size_t len,
                       char **out, size_t max)
{
    const AspellWordList *wl = aspell_speller_suggest(s->sp, word, (int)len);
    if (!wl)
        return 0;
    AspellStringEnumeration *e = aspell_word_list_elements(wl);
    size_t k = 0;
    const char *w;
    while (k < max && (w = aspell_string_enumeration_next(e)) != NULL) {
        char *copy = strdup(w);
        if (copy)
            out[k++] = copy;
    }
    delete_aspell_string_enumeration(e);
    return k;
}
