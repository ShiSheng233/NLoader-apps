#!/bin/sh
# build.sh <new out dir> [decl] [clang]: UIDEMO.NLB, every kind of row the
# camera's own menus draw (ABI 6). The declaration names the body:
# uidemo.json for the Z 6III 2.00 (the default; no description, so the card
# needs the MANIFEST line written next to the program), uidemo-z8.json for
# the Z 8 3.11 (describes itself: no MANIFEST line needed). The source is the
# same. The output directory must not exist yet.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
OUT=${1:?usage: build.sh <new out dir> [decl] [clang]}
DECL=${2:-$HERE/uidemo.json}
CC=${3:-clang}
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
"$ROOT/tool/nloader/target/debug/nloader" program --decl "$DECL" \
    --object "$OUT/obj/crt0.o" --object "$OUT/obj/uidemo.o" \
    --manifest "$OUT/MANIFEST" --out "$OUT/UIDEMO.NLB"
