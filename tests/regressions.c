/* Stateful regressions: run several requests on the same engine. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/checker.h"
static int failures;
static void expect(texaspell *ta, const char *name, const char *text, size_t want)
{
    ta_issue *issues = NULL;
    size_t n = ta_check(ta, text, strlen(text), &issues, NULL);
    if (n != want) {
        fprintf(stderr, "FAIL %s: wanted %zu issues, got %zu\n", name, want, n);
        for (size_t i = 0; i < n; i++)
            fprintf(stderr, "  %d: %.*s\n", issues[i].kind,
                    (int)(issues[i].end - issues[i].start), text + issues[i].start);
        failures++;
    } else printf("ok    %s\n", name);
    free(issues);
}
static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(2); }
    fputs(text, f);
    if (fclose(f)) { perror(path); exit(2); }
}
int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    char err[1024];
    texaspell *ta = ta_open(argv[1], err, sizeof err);
    if (!ta) { fprintf(stderr, "%s\n", err); return 2; }
    if (!ta_commands_enabled(ta)) { fprintf(stderr, "TeX required\n"); return 2; }
    expect(ta, "equals preserves prose checks", "Result = teh mispellt", 2);
    expect(ta, "ordinary equals inside document", "\\begin{document}Result = teh mispellt\\end{document}", 2);
    expect(ta, "PDF fragment does not swallow following prose",
           "pdftitle={Title},\nResult = teh mispellt", 2);
    expect(ta, "PDF fragment metadata text checked", "pdftitle={mispellt}", 1);
    expect(ta, "hyperref rejects geometry key", "\\hypersetup{margin=2cm}", 1);
    expect(ta, "geometry still accepts its own key", "\\geometry{margin=2cm}", 0);
    expect(ta, "literal percent in URL preserves following prose", "\\url{https://example.test/%20} mispellt", 1);
    expect(ta, "reference comment before argument", "\\ref% comment\n{mispelltlabel}", 0);
    expect(ta, "citation comment before argument", "\\cite% comment\n{mispelltkey}", 0);
    expect(ta, "brace inside reference comment is ignored", "\\ref{key% }\nmispelltkey}", 0);
    expect(ta, "comment brace in option preserves prose", "\\hypersetup{pdftitle={Title% }\n mispellt}}", 1);
    expect(ta, "definition available in same request", "\\newcommand{\\zzlocalmacro}{Title}\\zzlocalmacro", 0);
    expect(ta, "local definition suggestion context", "\\newcommand{\\zzlocalmacro}{Title}\\zzlocalmarco", 1);
    char *local_suggestions[10];
    size_t local_count = ta_suggest(ta, "\\zzlocalmarco", strlen("\\zzlocalmarco"), local_suggestions, 10);
    int local_found = 0;
    for (size_t i=0; i<local_count; i++) {
        if (!strcmp(local_suggestions[i], "\\zzlocalmacro")) local_found = 1;
        free(local_suggestions[i]);
    }
    if (!local_found) { fprintf(stderr,"FAIL local command suggestions\n"); failures++; }
    else puts("ok    local command suggestions");
    expect(ta, "definition does not leak to next request", "\\zzlocalmacro", 1);
    expect(ta, "explicit command learning still works", "\\zzlearnedmacro", 1);
    ta_learn(ta, "\\zzlearnedmacro", strlen("\\zzlearnedmacro"));
    expect(ta, "explicit command persists", "\\zzlearnedmacro", 0);
    ta_forget(ta, "\\zzlearnedmacro", strlen("\\zzlearnedmacro"));
    expect(ta, "explicit command forgetting", "\\zzlearnedmacro", 1);
    expect(ta, "environment with prose-like name", "\\begin{recieve}\\end{recieve}", 2);
    expect(ta, "subsequent prose classified as prose", "recieve", 1);
    char *suggestions[10];
    size_t ns = ta_suggest(ta, "recieve", 7, suggestions, 10);
    int receive = 0;
    for (size_t i=0; i<ns; i++) { if (!strcmp(suggestions[i],"receive")) receive=1; free(suggestions[i]); }
    if (!receive) { fprintf(stderr,"FAIL prose suggestions after environment\n"); failures++; }
    else puts("ok    prose suggestions after environment");
    expect(ta, "same spelling as prose and environment", "recieve \\begin{recieve}\\end{recieve}", 3);
    ta_learn(ta, "recieve", 7);
    expect(ta, "Learn Spelling uses current prose category", "recieve", 0);
    ta_forget(ta, "recieve", 7);
    char dir[2048], path[2200];
    snprintf(dir,sizeof dir,"%s",argv[2]);
    snprintf(path,sizeof path,"%s/preamble.tex",dir);
    ta_set_document_dir(ta,dir);
    expect(ta, "missing preamble initial check", "\\input{preamble}", 0);
    write_file(path,"\\newcommand{\\zzfirstmacro}{Title}");
    expect(ta, "newly created preamble retried", "\\input{preamble}\\zzfirstmacro", 0);
    write_file(path,"\\newcommand{\\zzsecondmacro}{Title}");
    expect(ta, "edited preamble reread and removed definition expires",
           "\\input{preamble}\\zzsecondmacro\\zzfirstmacro", 1);
    write_file(path,"\\input{preamble}\\newcommand{\\zzcyclemacro}{Title}");
    expect(ta, "cyclic input terminates", "\\input{preamble}\\zzcyclemacro", 0);
    write_file(path,"\\input{./preamble}\\newcommand{\\zzaliasmacro}{Title}");
    expect(ta, "aliased cyclic input terminates", "\\input{preamble}\\zzaliasmacro", 0);
    char package_path[2200], texinputs[4096];
    snprintf(package_path,sizeof package_path,"%s/zzreviewpackage.sty",dir);
    write_file(package_path,"\\ProvidesPackage{zzreviewpackage}\n\\newcommand{\\zzpackagecommand}{Title}\n");
    snprintf(texinputs,sizeof texinputs,"%s:%s",dir,getenv("TEXINPUTS") ? getenv("TEXINPUTS") : "");
    setenv("TEXINPUTS",texinputs,1);
    expect(ta, "document package available in current request",
           "\\usepackage{zzreviewpackage}\\zzpackagecommand", 0);
    expect(ta, "document package does not leak to next request", "\\zzpackagecommand", 1);
    remove(package_path);
    remove(path);
    expect(ta, "deleted preamble no longer defines commands", "\\input{preamble}\\zzaliasmacro", 1);

    /* 0.3: percent signs in verbatim-like arguments are not comments */
    expect(ta, "percent in href URL preserves following prose",
           "\\href{https://example.test/a%20b}{the mispellt link}\nA secnd line", 2);
    expect(ta, "percent in lstinline code preserves following prose",
           "\\usepackage{listings}\\lstinline{printf(\"%d\", x)} then mispellt", 1);
    expect(ta, "percent in mintinline code preserves following prose",
           "\\usepackage{minted}\\mintinline{c}{printf(\"%d\")} then mispellt", 1);

    /* 0.3: a lone line of a setup command's options */
    expect(ta, "lone line of known hyperref options", "  colorlinks=true, linkcolor=blue,", 0);
    expect(ta, "lone line with bare known options", "  bookmarks=true, bookmarksnumbered, hidelinks", 0);
    expect(ta, "known key followed by prose is still prose", "width = the mispellt value", 1);

    /* 0.3: partial text uses the context of recent full documents */
    char ctx_path[2200];
    snprintf(ctx_path,sizeof ctx_path,"%s/zzctxpackage.sty",dir);
    write_file(ctx_path,"\\ProvidesPackage{zzctxpackage}\n\\newcommand{\\zzctxcommand}{Title}\n");
    expect(ta, "full document with its own package and macro",
           "\\documentclass{article}\\usepackage{zzctxpackage}\\newcommand{\\zzdocmacro}{x}"
           "\\begin{document}\\zzdocmacro\\zzctxcommand\\end{document}", 0);
    expect(ta, "fragment knows recent document's macro and package",
           "Later \\zzdocmacro and \\zzctxcommand.", 0);
    expect(ta, "another full document uses only its own context",
           "\\documentclass{article}\\begin{document}\\zzdocmacro\\zzctxcommand\\end{document}", 2);
    expect(ta, "fragment still knows the earlier recent document", "\\zzdocmacro", 0);
    expect(ta, "fragment definitions are still not remembered", "\\zzlocalmacro", 1);

    /* 0.3: categories survive an intervening check */
    expect(ta, "misspelt environment flagged", "\\begin{itemzie}\\end{itemzie}", 2);
    expect(ta, "intervening check of other text", "Some other text.", 0);
    char *env_suggestions[10];
    size_t env_count = ta_suggest(ta, "itemzie", 7, env_suggestions, 10);
    int env_found = 0;
    for (size_t i = 0; i < env_count; i++) {
        if (!strcmp(env_suggestions[i], "itemize")) env_found = 1;
        free(env_suggestions[i]);
    }
    if (!env_found) { fprintf(stderr, "FAIL environment suggestions after another check\n"); failures++; }
    else puts("ok    environment suggestions after another check");
    ta_learn(ta, "itemzie", 7);
    expect(ta, "Learn Spelling after another check learns the environment",
           "\\begin{itemzie}\\end{itemzie}", 0);
    expect(ta, "learnt environment is not a prose word", "itemzie", 1);
    ta_forget(ta, "itemzie", 7);
    if (ta_use_language(ta,"de_DE",err,sizeof err)) {
        ta_use_language(ta,"en_GB",err,sizeof err);
        expect(ta, "ordinary environment restores language",
               "\\begin{quote}\\selectlanguage{ngerman}Wort\\end{quote}colour", 0);
        expect(ta, "nested ordinary environments restore language",
               "\\begin{quote}\\selectlanguage{ngerman}Wort\\begin{center}\\selectlanguage{british}colour\\end{center}Wort\\end{quote}colour", 0);
        expect(ta, "document language applies to current request",
               "\\documentclass{article}\\usepackage[ngerman]{babel}colour", 1);
        ta_use_language(ta,"en_GB",err,sizeof err);
        expect(ta, "document language does not override next selection", "colour", 0);
    } else puts("skip  German language regressions (dictionary not installed)");
    ta_learn(ta, "\\zzpersistentmacro", strlen("\\zzpersistentmacro"));
    ta_close(ta);
    char legacy[2200];
    snprintf(legacy,sizeof legacy,"%s/auto-commands.txt",argv[1]);
    write_file(legacy,"\\zzlegacyignored\n");
    ta = ta_open(argv[1],err,sizeof err);
    if (!ta) return 2;
    expect(ta, "inferred command does not persist after reopening", "\\zzlocalmacro", 1);
    expect(ta, "legacy auto commands do not silently persist", "\\zzlegacyignored", 1);
    expect(ta, "explicit learned command survives reopening", "\\zzpersistentmacro", 0);
    expect(ta, "recent document context survives reopening", "\\zzdocmacro", 0);
    ta_close(ta);
    return failures ? 1 : 0;
}
