#!/bin/sh
# Print the packages and classes used by a test document that kpsewhich
# cannot find (nothing if all are installed).
#   sh tests/packages-needed.sh file.tex
kpse=kpsewhich
[ -x /Library/TeX/texbin/kpsewhich ] && kpse=/Library/TeX/texbin/kpsewhich
sed -n -E 's/^\\(usepackage|documentclass)(\[[^]]*\])?\{([^}]*)\}.*/\1 \3/p' "$1" |
while read -r kind names; do
    ext=sty
    [ "$kind" = documentclass ] && ext=cls
    for n in $(printf '%s' "$names" | tr ',' ' '); do
        "$kpse" "$n.$ext" >/dev/null 2>&1 || printf '%s ' "$n.$ext"
    done
done
