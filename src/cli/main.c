/* texaspell - command-line LaTeX spell checker using the TeXAspell engine.
 *
 *   texaspell [options] [file.tex ...]
 *
 * Prints one line per problem:  file:line:column: kind: token [suggestions]
 * Exits 0 if nothing was flagged, 1 if something was, 2 on error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/checker.h"

#define MAX_SUGGEST 6

static void usage(void)
{
    fputs(
        "usage: texaspell [options] [file.tex ...]\n"
        "Checks LaTeX files (or standard input) with aspell and validates\n"
        "LaTeX command and environment names.\n"
        "\n"
        "  -s, --suggest       show suggestions\n"
        "  -l, --list          print only the flagged words, one per line\n"
        "  -L, --lang LANG     language, e.g. en_US (default en_GB)\n"
        "  -d, --dict NAME     aspell dictionary for en_GB (default from config.txt)\n"
        "      --home DIR      settings folder\n"
        "      --learn WORD    add WORD (or \\command) to your own list\n"
        "      --forget WORD   remove WORD from your own lists\n"
        "      --info          show settings and exit\n"
        "  -h, --help          this help\n",
        stderr);
}

static char *read_all(FILE *f, size_t *len)
{
    size_t cap = 65536, n = 0;
    char *buf = malloc(cap);
    while (buf) {
        if (n == cap) {
            char *nb = realloc(buf, cap * 2);
            if (!nb) {
                free(buf);
                return NULL;
            }
            buf = nb;
            cap *= 2;
        }
        size_t r = fread(buf + n, 1, cap - n, f);
        if (r == 0)
            break;
        n += r;
    }
    if (ferror(f)) {
        free(buf);
        return NULL;
    }
    *len = n;
    return buf;
}

static const char *kind_name(ta_kind k)
{
    switch (k) {
    case TA_COMMAND: return "command";
    case TA_ENVIRONMENT: return "environment";
    case TA_OPTION: return "option";
    default: return "spelling";
    }
}

static int check_text(texaspell *ta, const char *name, const char *text, size_t len,
                      int suggest, int list)
{
    ta_issue *issues = NULL;
    size_t n = ta_check(ta, text, len, &issues, NULL);
    size_t line = 1, col = 1, pos = 0;
    for (size_t i = 0; i < n; i++) {
        const ta_issue *is = &issues[i];
        for (; pos < is->start; pos++) {
            if (text[pos] == '\n') {
                line++;
                col = 1;
            } else if (((unsigned char)text[pos] & 0xC0) != 0x80) {
                col++;
            }
        }
        const char *tok = text + is->start;
        int tl = (int)(is->end - is->start);
        if (list) {
            printf("%.*s\n", tl, tok);
            continue;
        }
        printf("%s:%zu:%zu: %s: %.*s", name, line, col, kind_name(is->kind), tl, tok);
        if (suggest) {
            char *sugg[MAX_SUGGEST];
            size_t ns = ta_suggest(ta, tok, (size_t)tl, sugg, MAX_SUGGEST);
            if (ns) {
                fputs(" -> ", stdout);
                for (size_t j = 0; j < ns; j++) {
                    printf("%s%s", j ? ", " : "", sugg[j]);
                    free(sugg[j]);
                }
            }
        }
        putchar('\n');
    }
    free(issues);
    return n > 0;
}

int main(int argc, char **argv)
{
    const char *home = NULL;
    const char *learn = NULL, *forget = NULL, *lang = NULL;
    int suggest = 0, list = 0, info = 0;
    int first_file = argc;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-s") == 0 || strcmp(a, "--suggest") == 0) {
            suggest = 1;
        } else if (strcmp(a, "-l") == 0 || strcmp(a, "--list") == 0) {
            list = 1;
        } else if ((strcmp(a, "-d") == 0 || strcmp(a, "--dict") == 0) && i + 1 < argc) {
            setenv("TEXASPELL_DICTIONARY", argv[++i], 1);
        } else if ((strcmp(a, "-L") == 0 || strcmp(a, "--lang") == 0) && i + 1 < argc) {
            lang = argv[++i];
        } else if (strcmp(a, "--home") == 0 && i + 1 < argc) {
            home = argv[++i];
        } else if (strcmp(a, "--learn") == 0 && i + 1 < argc) {
            learn = argv[++i];
        } else if (strcmp(a, "--forget") == 0 && i + 1 < argc) {
            forget = argv[++i];
        } else if (strcmp(a, "--info") == 0) {
            info = 1;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage();
            return 0;
        } else if (strcmp(a, "--") == 0) {
            first_file = i + 1;
            break;
        } else if (a[0] == '-' && a[1]) {
            fprintf(stderr, "texaspell: unknown option %s\n", a);
            usage();
            return 2;
        } else {
            first_file = i;
            break;
        }
    }

    char err[1024];
    texaspell *ta = ta_open(home, err, sizeof err);
    if (!ta) {
        fprintf(stderr, "texaspell: %s\n", err);
        return 2;
    }
    if (ta_warning(ta))
        fprintf(stderr, "texaspell: warning: %s\n", ta_warning(ta));
    if (lang && !ta_use_language(ta, lang, err, sizeof err)) {
        fprintf(stderr, "texaspell: %s\n", err);
        ta_close(ta);
        return 2;
    }

    if (info) {
        printf("settings folder: %s\n", ta_home(ta));
        printf("dictionary:      %s\n", ta_dictionary(ta));
        printf("command checks:  %s\n", ta_commands_enabled(ta) ? "on" : "off");
        char desc[4096];
        ta_describe(ta, desc, sizeof desc);
        fputs(desc, stdout);
        ta_close(ta);
        return 0;
    }
    if (learn || forget) {
        if (learn)
            ta_learn(ta, learn, strlen(learn));
        if (forget)
            ta_forget(ta, forget, strlen(forget));
        ta_close(ta);
        return 0;
    }

    int flagged = 0, failed = 0;
    if (first_file >= argc) {
        size_t len;
        char *text = read_all(stdin, &len);
        if (text) {
            flagged |= check_text(ta, "<stdin>", text, len, suggest, list);
            free(text);
        } else {
            fprintf(stderr, "texaspell: cannot read standard input (I/O error or out of memory)\n");
            failed = 1;
        }
    }
    for (int i = first_file; i < argc; i++) {
        /* \input{preamble} is looked for next to the document */
        char dir[1024];
        snprintf(dir, sizeof dir, "%s", argv[i]);
        char *slash = strrchr(dir, '/');
        if (slash)
            *slash = '\0';
        else
            snprintf(dir, sizeof dir, ".");
        ta_set_document_dir(ta, dir);
        FILE *f = fopen(argv[i], "rb");
        if (!f) {
            perror(argv[i]);
            failed = 1;
            continue;
        }
        size_t len;
        char *text = read_all(f, &len);
        fclose(f);
        if (text) {
            flagged |= check_text(ta, argv[i], text, len, suggest, list);
            free(text);
        } else {
            fprintf(stderr, "texaspell: cannot read %s (I/O error or out of memory)\n", argv[i]);
            failed = 1;
        }
    }
    ta_close(ta);
    return failed ? 2 : flagged ? 1 : 0;
}
