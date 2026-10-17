#!/bin/sh
# build.sh <new out dir> [clang]: the PLATTEST fixture programs.
#   PLATTEST.NLB  NPLD v3, the checks (variant 0), 1 MiB reserved
#   PLATBIG.NLB   NPLD v3, only the reservation (variant 1), 1024 MiB asked
#   PLATV2.NLB    NPLD v2, the checks (variant 2): needs a MANIFEST line
#   PLATZ6.NLB    NPLD v3, built for another body (listed, not runnable)
#   ZDUP.NLB      NPLD v3, the same id as PLATTEST and later by name
# Each program's MANIFEST line is written to <out>/<NAME>.line; a card
# without MANIFEST needs none of them. The output directory must not exist.
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
for v in 0 1 2; do
    $CC --target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only -ffreestanding \
        -fno-common -fno-stack-protector -fno-unwind-tables \
        -fno-asynchronous-unwind-tables -fPIC -fvisibility=hidden -nostdinc \
        -isystem "$RES" -I "$ROOT/sdk" -fno-jump-tables -fno-builtin -O2 \
        -Wall -Wextra -Werror -DPLAT_VARIANT=$v -c "$HERE/plattest.c" -o "$OUT/obj/plattest$v.o"
done
build() { # decl variant name
    "$ROOT/tool/nloader/target/debug/nloader" program --decl "$HERE/decl/$1.json" \
        --object "$OUT/obj/crt0.o" --object "$OUT/obj/plattest$2.o" \
        --manifest "$OUT/$3.line" --out "$OUT/$3.NLB"
}
build plattest 0 PLATTEST
build platbig 1 PLATBIG
build platv2 2 PLATV2
build platz6 0 PLATZ6
build platdup 0 ZDUP
