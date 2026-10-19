#!/bin/sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
task_tmp=$(mktemp -d)
trap 'rm -rf "$task_tmp"' EXIT
mkdir -p "$task_tmp/doc"
export TEXASPELL_DICTIONARY=en_GB-ise
"$root/build/regressions" "$task_tmp/settings" "$task_tmp/doc"
set +e
TEXASPELL_HOME="$task_tmp/settings" "$root/build/texaspell" "$task_tmp/doc" >"$task_tmp/out" 2>"$task_tmp/err"
status=$?
set -e
if [ "$status" != 2 ] || ! grep -q 'cannot read' "$task_tmp/err"; then
    echo 'FAIL  unreadable input must return error status 2'
    exit 1
fi
echo 'ok    unreadable input returns error status 2'
if [ -x "$root/build/macos-regressions" ]; then
    "$root/build/macos-regressions" "$task_tmp/native-settings"
fi
