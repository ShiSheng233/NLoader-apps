#!/bin/sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${NLOADER:-$(cd "$HERE/../../.." && pwd)}
SDK=${SDK:-$ROOT/sdk}
OUT=$(mktemp -d "${TMPDIR:-/tmp}/heraw-install.XXXXXX")
trap 'rm -rf "$OUT"' EXIT INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -I "$SDK" -I "$HERE" \
    "$HERE/install.c" "$HERE/test_install.c" -o "$OUT/test_install"
"$OUT/test_install"
