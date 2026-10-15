/* languages.h - babel / polyglossia language names and aspell dictionaries. */
#ifndef TEXASPELL_LANGUAGES_H
#define TEXASPELL_LANGUAGES_H

#include <stddef.h>

/* The aspell language code for a babel or polyglossia language name, e.g.
 * "ngerman" -> "de_DE", "british" -> "en_GB". Returns "" for "english",
 * meaning the default language, and NULL for names it does not know.
 * BCP 47 tags such as "de-DE" or "fr" are also accepted. */
const char *lang_code(const char *name, size_t len);

/* Is this a language name lang_code knows (not counting BCP 47 tags)?
 * Used to recognise polyglossia's \textgerman{..} and \begin{german}. */
int lang_is_name(const char *name, size_t len);

#endif
