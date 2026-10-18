#!/bin/sh
# build.sh <new out dir> [clang]: PROPWATCH.NLB and PROPSET.NLB, the Z 8
# fixtures for family D (prop_get / prop_info / prop_watch, prop_set), and
# one MANIFEST naming both. They include the Z 8's settings table
# (sdk/z8/nlr_props.h, ahead of sdk/). The output directory must not exist.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
OUT=${1:?usage: build.sh <new out dir> [clang]}
CC=${2:-clang}
[ -e "$OUT" ] && { echo "refusing: $OUT exists" >&2; exit 1; }
RES=$($CC -print-resource-dir)/include
mkdir -p "$OUT/obj"
$CC --target=aarch64-none-elf -x assembler-with-cpp -DNLR_PROGRAM_FOREGROUND \
    -c "$ROOT/sdk/crt0.s" -o "$OUT/obj/crt0.o"
for name in propwatch propset; do
    $CC --target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only -ffreestanding \
        -fno-common -fno-stack-protector -fno-unwind-tables \
        -fno-asynchronous-unwind-tables -fPIC -fvisibility=hidden -nostdinc \
        -isystem "$RES" -I "$ROOT/sdk/z8" -I "$ROOT/sdk" -fno-jump-tables -fno-builtin -O2 \
        -Wall -Wextra -Werror -c "$HERE/$name.c" -o "$OUT/obj/$name.o"
    upper=$(echo $name | tr a-z A-Z)
    "$ROOT/tool/nloader/target/debug/nloader" program --decl "$HERE/$name.json" \
        --object "$OUT/obj/crt0.o" --object "$OUT/obj/$name.o" \
        --manifest "$OUT/$name.manifest" --out "$OUT/$upper.NLB"
done
cat "$OUT/propwatch.manifest" > "$OUT/MANIFEST"
grep -v '^#' "$OUT/propset.manifest" >> "$OUT/MANIFEST"
