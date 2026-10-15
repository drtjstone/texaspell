/* languages.c - babel / polyglossia language names and aspell dictionaries. */
#include "languages.h"

#include <string.h>

typedef struct langmap {
    const char *name;
    const char *code;
} langmap;

/* "" means the default language: British English unless another language
 * is chosen in the Spelling panel. babel's "english" is ambiguous (it means
 * US English to babel, but is widely used for British documents), so it
 * follows the default. */
static const langmap names[] = {
    {"british", "en_GB"}, {"UKenglish", "en_GB"}, {"english", ""},
    {"american", "en_US"}, {"USenglish", "en_US"}, {"canadian", "en_CA"},
    {"australian", "en_AU"}, {"newzealand", "en_NZ"},
    {"german", "de_DE"}, {"ngerman", "de_DE"}, {"austrian", "de_AT"},
    {"naustrian", "de_AT"}, {"swissgerman", "de_CH"}, {"nswissgerman", "de_CH"},
    {"french", "fr_FR"}, {"francais", "fr_FR"}, {"acadian", "fr_CA"},
    {"canadien", "fr_CA"}, {"spanish", "es"}, {"catalan", "ca"},
    {"italian", "it"}, {"portuguese", "pt_PT"}, {"portuges", "pt_PT"},
    {"brazil", "pt_BR"}, {"brazilian", "pt_BR"}, {"dutch", "nl"},
    {"danish", "da"}, {"swedish", "sv"}, {"norsk", "nb"}, {"norwegian", "nb"},
    {"nynorsk", "nn"}, {"finnish", "fi"}, {"icelandic", "is"},
    {"polish", "pl"}, {"czech", "cs"}, {"slovak", "sk"}, {"slovene", "sl"},
    {"slovenian", "sl"}, {"croatian", "hr"}, {"serbian", "sr"},
    {"hungarian", "hu"}, {"magyar", "hu"}, {"romanian", "ro"},
    {"bulgarian", "bg"}, {"russian", "ru"}, {"ukrainian", "uk"},
    {"greek", "el"}, {"turkish", "tr"}, {"welsh", "cy"}, {"irish", "ga"},
    {"scottish", "gd"}, {"scottishgaelic", "gd"}, {"latin", "la"},
    {"esperanto", "eo"}, {"estonian", "et"}, {"latvian", "lv"},
    {"lithuanian", "lt"}, {"basque", "eu"}, {"galician", "gl"},
    {"hebrew", "he"}, {"afrikaans", "af"}, {"breton", "br"},
    {NULL, NULL}
};

int lang_is_name(const char *name, size_t len)
{
    for (const langmap *m = names; m->name; m++)
        if (strlen(m->name) == len && memcmp(m->name, name, len) == 0)
            return 1;
    return 0;
}

const char *lang_code(const char *name, size_t len)
{
    for (const langmap *m = names; m->name; m++)
        if (strlen(m->name) == len && memcmp(m->name, name, len) == 0)
            return m->code;

    /* BCP 47: "fr", "de-DE", "en-GB" */
    static char buf[16];
    int lower = 0;
    while ((size_t)lower < len && name[lower] >= 'a' && name[lower] <= 'z')
        lower++;
    if (lower < 2 || lower > 3)
        return NULL;
    if ((size_t)lower == len) {
        memcpy(buf, name, len);
        buf[len] = '\0';
    } else if (len == (size_t)lower + 3 && name[lower] == '-' &&
               name[lower + 1] >= 'A' && name[lower + 1] <= 'Z' &&
               name[lower + 2] >= 'A' && name[lower + 2] <= 'Z') {
        memcpy(buf, name, len);
        buf[lower] = '_';
        buf[len] = '\0';
    } else {
        return NULL;
    }
    if (strcmp(buf, "en") == 0)
        return "";
    return buf;
}
