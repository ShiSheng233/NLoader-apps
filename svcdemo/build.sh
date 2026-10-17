#!/bin/sh
# build.sh <new out dir> [decl] [clang]: SVCDEMO.NLB, a service (a resident
# extension that starts a task of its own, ABI 7). svcdemo.json names both
# bodies (Z 6III 2.00 and Z 8 3.11): the program uses only the API table.
# The output directory must not exist yet.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
OUT=${1:?usage: build.sh <new out dir> [decl] [clang]}
DECL=${2:-$HERE/svcdemo.json}
CC=${3:-clang}
[ -e "$OUT" ] && { echo "refusing: $OUT exists" >&2; exit 1; }
RES=$($CC -print-resource-dir)/include
mkdir -p "$OUT/obj"
$CC --target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only -ffreestanding \
    -fno-common -fno-stack-protector -fno-unwind-tables \
    -fno-asynchronous-unwind-tables -fPIC -fvisibility=hidden -nostdinc \
    -isystem "$RES" -I "$ROOT/sdk" -fno-jump-tables -fno-builtin -O2 \
    -Wall -Wextra -Werror -c "$HERE/svcdemo.c" -o "$OUT/obj/svcdemo.o"
$CC --target=aarch64-none-elf -x assembler-with-cpp -DNLR_PROGRAM_EXTENSION \
    -c "$ROOT/sdk/crt0.s" -o "$OUT/obj/crt0.o"
"$ROOT/tool/nloader/target/debug/nloader" program --decl "$DECL" \
    --object "$OUT/obj/crt0.o" --object "$OUT/obj/svcdemo.o" \
    --manifest "$OUT/MANIFEST" --out "$OUT/SVCDEMO.NLB"
