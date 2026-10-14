#!/bin/bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
sdk="$(cd "$here/../.." && pwd)"
out="${1:?usage: test_nlr.sh NEW_OUTPUT_DIRECTORY}"
[[ ! -e "$out" ]] || { echo "Output already exists" >&2; exit 2; }
mkdir -p "$out"
compiler="${NLR_LIBC_HOST_CC:-clang}"
flags=(-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Werror
    -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200
    -I "$sdk" -I "$here/engine/doomgeneric" -iquote "$sdk/libc/include")
"$compiler" "${flags[@]}" -Dexit=nlr_test_exit -c "$here/doomgeneric_nlr.c" -o "$out/adapter.o"
"$compiler" "${flags[@]}" "$here/test_nlr.c" "$out/adapter.o" -o "$out/test_nlr"
"$out/test_nlr"
