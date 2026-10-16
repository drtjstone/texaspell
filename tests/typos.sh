#!/bin/sh
# Check that every typo in tests/typos.txt is flagged and that the intended
# command, environment or option is among the first 5 suggestions.
#   sh tests/typos.sh [path/to/texaspell]
# Set VERBOSE=1 to list typos whose correction is not the first suggestion.
set -u
here=$(cd "$(dirname "$0")" && pwd)
bin=${1:-"$here/../build/texaspell"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

doc="$work/typos.tex"
sed '/\\begin{document}/q' "$here/fixtures/valid-commands.tex" > "$doc"
offset=$(wc -l < "$doc")
# typo lines, in order, with comments and blank lines removed
grep -v '^#' "$here/typos.txt" | grep -v '^[[:space:]]*$' > "$work/cases"
# snippet = text before the first run of two or more spaces
sed 's/  .*$//' "$work/cases" >> "$doc"
printf '%s\n' '\end{document}' >> "$doc"

TEXASPELL_HOME="$work/home" TEXASPELL_DICTIONARY=en_GB-ise \
    "$bin" -s "$doc" > "$work/out"

awk -v offset="$offset" -v verbose="${VERBOSE:-0}" '
    # first file: the cases
    FNR == NR {
        n++
        line = $0
        i = match(line, /  +/)
        snippet[n] = substr(line, 1, i - 1)
        want = substr(line, i)
        gsub(/^ +| +$/, "", want)
        expected[n] = want
        next
    }
    # second file: texaspell output  file:line:col: kind: token -> a, b, c
    {
        split($0, f, ":")
        ln = f[2] - offset
        if (ln < 1 || ln > n) {
            stray[++nstray] = $0
            next
        }
        if (ln in seen)
            next          # only the first flag on each line counts
        seen[ln] = 1
        s = $0
        rank[ln] = 0
        if (index(s, " -> ")) {
            list = substr(s, index(s, " -> ") + 4)
            k = split(list, sug, ", ")
            for (j = 1; j <= k && j <= 5; j++)
                if (sug[j] == expected[ln]) { rank[ln] = j; break }
            offered[ln] = list
        }
    }
    END {
        caught = first = 0
        for (i = 1; i <= n; i++) {
            if (!(i in seen)) {
                printf "FAIL  %-28s not flagged\n", snippet[i]
                bad++
            } else if (rank[i] == 0) {
                printf "FAIL  %-28s flagged, but %s not in top 5 (offered: %s)\n",
                       snippet[i], expected[i], offered[i]
                bad++
            } else {
                caught++
                if (rank[i] == 1) first++
                else if (verbose)
                    printf "note  %-28s %s is suggestion %d (offered: %s)\n",
                           snippet[i], expected[i], rank[i], offered[i]
            }
        }
        for (i = 1; i <= nstray; i++) {
            printf "FAIL  unexpected flag in the preamble: %s\n", stray[i]
            bad++
        }
        printf "typos: %d of %d caught with the right suggestion (%d as the first suggestion)\n",
               caught, n, first
        exit bad > 0
    }
' "$work/cases" "$work/out"
