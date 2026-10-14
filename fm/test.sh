#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
sdk="$(cd "$here/../.." && pwd)"
out="${1:?usage: test.sh NEW_OUTPUT_DIRECTORY}"
[[ ! -e "$out" ]] || { echo "Output already exists" >&2; exit 2; }
mkdir -p "$out"
compiler="${NLR_LIBC_HOST_CC:-clang}"
"$compiler" -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined \
    -Wall -Wextra -Werror -I "$sdk" -iquote "$sdk/libc/include" \
    "$here/fm.c" "$here/test_fm.c" -o "$out/test_fm"
"$out/test_fm"
