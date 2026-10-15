/* cmddb.h - the set of LaTeX commands and environments that exist.
 *
 * Built by reading the files of the user's TeX installation (located with
 * kpsewhich): the LaTeX kernel, the document class and every package that a
 * document loads, followed recursively through \RequirePackage and \input.
 * Every control word that appears in those files counts as a known command.
 */
#ifndef TEXASPELL_CMDDB_H
#define TEXASPELL_CMDDB_H

#include <stddef.h>

#include "strset.h"

typedef struct cmddb {
    strset cmds;        /* command names without backslash */
    strset envs;        /* environment names                */
    strset keys;        /* option keys (pdftitle, margin)   */
    strset tried;       /* file names already looked up     */
    strset loaded;      /* file names found and read        */
    char kpsewhich[1024];
    int have_kernel;    /* latex.ltx was found and read      */
    size_t files_read;
} cmddb;

void cmddb_init(cmddb *db, const char *kpsewhich_setting);
void cmddb_free(cmddb *db);

/* Load files (e.g. "latex.ltx", "amsmath.sty", "article.cls") and, through
 * them, everything they load. Names already tried are ignored. Returns the
 * number of the given names that were found. found[i] is set to 1 for each
 * name found (found may be NULL). */
size_t cmddb_load(cmddb *db, const char *const *names, size_t count, int *found);

int cmddb_has_command(const cmddb *db, const char *name, size_t len);
int cmddb_has_environment(const cmddb *db, const char *name, size_t len);

int cmddb_has_key(const cmddb *db, const char *name, size_t len);
/* Was this file (e.g. "hyperref.sty") found and read? */
int cmddb_loaded(const cmddb *db, const char *file);

typedef enum { CMDDB_COMMANDS, CMDDB_ENVIRONMENTS, CMDDB_KEYS } cmddb_kind;

/* Find up to max known names of the given kind within edit distance 2 of
 * name. Results are malloc'd and returned best first. extra is a further
 * set of candidates to consider (may be NULL). */
size_t cmddb_suggest(const cmddb *db, const strset *extra, cmddb_kind kind,
                     const char *name, size_t len, char **out, size_t max);

/* Optimal string alignment distance, giving up (returning limit + 1) once
 * it must exceed limit. Exposed for tests. */
int cmddb_distance(const char *a, size_t al, const char *b, size_t bl, int limit);

#endif
