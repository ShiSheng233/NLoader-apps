#!/bin/sh
# build.sh <new out dir> [clang]: UIDEMO.NLB, every kind of row the
# camera's own menus draw (ABI 6). The output directory must not exist yet.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
OUT=${1:?usage: build.sh <new out dir> [clang]}
CC=${2:-clang}
[ -e "$OUT" ] && { echo "refusing: $OUT exists" >&2; exit 1; }
RES=$($CC -print-resource-dir)/include
mkdir -p "$OUT/obj"
$CC --target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only -ffreestanding \
    -fno-common -fno-stack-protector -fno-unwind-tables \
    -fno-asynchronous-unwind-tables -fPIC -fvisibility=hidden -nostdinc \
    -isystem "$RES" -I "$ROOT/sdk" -fno-jump-tables -fno-builtin -O2 \
    -Wall -Wextra -Werror -c "$HERE/uidemo.c" -o "$OUT/obj/uidemo.o"
$CC --target=aarch64-none-elf -x assembler-with-cpp -DNLR_PROGRAM_FOREGROUND \
    -c "$ROOT/sdk/crt0.s" -o "$OUT/obj/crt0.o"
"$ROOT/tool/nloader/target/debug/nloader" program --decl "$HERE/uidemo.json" \
    --object "$OUT/obj/crt0.o" --object "$OUT/obj/uidemo.o" \
    --manifest "$OUT/MANIFEST" --out "$OUT/UIDEMO.NLB"
