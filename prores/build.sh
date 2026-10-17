#!/bin/sh
# build.sh <new out dir> [clang]: PRORES.NLB, the ProRes-grades extension.
# The output directory must not exist yet.
#
# SDK=<dir with nlr.h and crt0.s> builds against another SDK than the
# NLoader tree's (use the one the loader on the camera was built with).
# NLOADER=<repo> names the NLoader tree, for its `nloader` tool.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${NLOADER:-$(cd "$HERE/../../.." && pwd)}
OUT=${1:?usage: build.sh <new out dir> [clang]}
CC=${2:-clang}
SDK=${SDK:-$ROOT/sdk}
[ -e "$OUT" ] && { echo "refusing: $OUT exists" >&2; exit 1; }
RES=$($CC -print-resource-dir)/include
mkdir -p "$OUT/obj"
# -mgeneral-regs-only: no floating point or SIMD register is ever touched,
# because every handler here runs on a firmware task.
COMMON="--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only
 -ffreestanding -fno-common -fno-stack-protector -fno-unwind-tables
 -fno-asynchronous-unwind-tables -fPIC -fvisibility=hidden -nostdinc
 -isystem $RES -I $SDK -I $HERE -fno-jump-tables -fno-builtin -O2
 -Wall -Wextra -Werror"
for f in block codec menu start; do
    $CC $COMMON -c "$HERE/$f.c" -o "$OUT/obj/$f.o"
done
$CC --target=aarch64-none-elf -x assembler-with-cpp -DNLR_PROGRAM_EXTENSION \
    -c "$SDK/crt0.s" -o "$OUT/obj/crt0.o"
N="$ROOT/tool/nloader/target/debug/nloader"
"$N" program --decl "$HERE/prores.json" \
    --object "$OUT/obj/crt0.o" --object "$OUT/obj/start.o" \
    --object "$OUT/obj/menu.o" --object "$OUT/obj/codec.o" \
    --object "$OUT/obj/block.o" \
    --manifest "$OUT/MANIFEST" --out "$OUT/PRORES.NLB"
ls -l "$OUT/PRORES.NLB"
