#!/bin/sh
# Run the TeXAspell tests. Needs aspell with the en_GB dictionary and a TeX
# installation (for kpsewhich and latex.ltx).
set -u
here=$(cd "$(dirname "$0")" && pwd)
bin="$here/../build/texaspell"
home=$(mktemp -d)
trap 'rm -rf "$home"' EXIT
export TEXASPELL_HOME="$home"
export TEXASPELL_DICTIONARY=en_GB-ise

if ! "$bin" --info | grep -q 'command checks:  on'; then
    echo "SKIP: command checking is off (TeX not found?)"
    "$bin" --info
    exit 1
fi

fail=0
for tex in "$here"/fixtures/*.tex; do
    expected="${tex%.tex}.expected"
    name=$(basename "$tex")
    if [ "$name" = languages.tex ] &&
       ! { aspell dicts | grep -qx de_DE && aspell dicts | grep -qx fr_FR; }; then
        echo "skip  $name (needs the de_DE and fr_FR aspell dictionaries)"
        continue
    fi
    missing=$(sh "$here/packages-needed.sh" "$tex")
    if [ -n "$missing" ]; then
        echo "skip  $name (TeX packages not installed: $missing)"
        continue
    fi
    actual=$(cd "$here/fixtures" && "$bin" "$name")
    if [ "$actual" = "$(cat "$expected")" ]; then
        echo "ok    $name"
    else
        echo "FAIL  $name"
        printf '%s\n' "$actual" | diff "$expected" - | sed 's/^/      /'
        fail=1
    fi
done

# Learning and forgetting
check() { # description, input, expected exit status
    printf '%s' "$2" | "$bin" >/dev/null
    status=$?
    if [ "$status" -eq "$3" ]; then echo "ok    $1"; else echo "FAIL  $1 (exit $status)"; fail=1; fi
}
check "unknown word flagged" 'gubbins' 1
"$bin" --learn gubbins
check "learnt word accepted" 'gubbins' 0
check "learnt word accepted capitalised" 'Gubbins' 0
"$bin" --forget gubbins
check "forgotten word flagged again" 'gubbins' 1
check "unknown command flagged" '\mymacro' 1
"$bin" --learn '\mymacro'
check "learnt command accepted" '\mymacro' 0
check "configured base package accepted" '\toprule' 0
check "-ise spelling accepted" 'organise' 0
check "-ize spelling flagged by en_GB-ise" 'organize' 1
check "acronyms skipped" 'NHS MPs HMT' 0
printf 'colour' | "$bin" -L en_US >/dev/null
if [ $? -eq 1 ]; then echo "ok    en_US dictionary selectable"; else echo "FAIL  en_US dictionary selectable"; fail=1; fi
check "unknown hyperref option flagged" '\usepackage{hyperref}\hypersetup{pdftitel={A}}' 1
check "pdftitle text is spell-checked" '\usepackage{hyperref}\hypersetup{pdftitle={Finanse}}' 1

# Command, environment and option typos
missing=$(sh "$here/packages-needed.sh" "$here/fixtures/valid-commands.tex")
if [ -n "$missing" ]; then
    echo "skip  typos (TeX packages not installed: $missing)"
elif ! sh "$here/typos.sh" "$bin"; then
    fail=1
fi

exit $fail
