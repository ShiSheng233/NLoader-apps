#!/bin/sh
# build.sh <new out dir> [clang]: the Log3G10 settings page (LOG3G10.NLB,
# foreground) and its start-up extension (LOG3G10X.NLB). The output
# directory must not exist yet. The curve file comes from mkcurve.py.
# The programs describe themselves (NPLD v3): a loader with ABI 7 or later
# finds them in APPS with no MANIFEST line. PLAIN=1 builds them the old way
# for an older loader, which refuses a described file: then add the
# MANIFEST lines this writes (MANIFEST.page, MANIFEST.start).
# SDK=<dir with nlr.h and crt0.s> builds against another SDK than this
# tree's (the one the loader on the camera was built with); NLOADER=<repo>
# names the NLoader tree (for its `nloader` tool) when this directory is
# not inside it at sdk/examples/log3g10.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${NLOADER:-$(cd "$HERE/../../.." && pwd)}
OUT=${1:?usage: build.sh <new out dir> [clang]}
CC=${2:-clang}
SDK=${SDK:-$ROOT/sdk}
[ -e "$OUT" ] && { echo "refusing: $OUT exists" >&2; exit 1; }
RES=$($CC -print-resource-dir)/include
mkdir -p "$OUT/obj"
# -mgeneral-regs-only: no floating point or SIMD register is ever touched.
COMMON="--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only
 -ffreestanding -fno-common -fno-stack-protector -fno-unwind-tables
 -fno-asynchronous-unwind-tables -fPIC -fvisibility=hidden -nostdinc
 -isystem $RES -I $SDK -I $HERE -fno-jump-tables -fno-builtin -O2
 -Wall -Wextra -Werror"
for f in log3g10 page start; do
    $CC $COMMON -c "$HERE/$f.c" -o "$OUT/obj/$f.o"
done
# the declarations, without their description for an older loader
for d in log3g10 log3g10x; do
    if [ -n "${PLAIN:-}" ]; then
        sed 's/"describe": true/"describe": false/' "$HERE/$d.json" > "$OUT/obj/$d.json"
    else
        cp "$HERE/$d.json" "$OUT/obj/$d.json"
    fi
done
$CC --target=aarch64-none-elf -x assembler-with-cpp -DNLR_PROGRAM_FOREGROUND \
    -c "$SDK/crt0.s" -o "$OUT/obj/crt0_app.o"
$CC --target=aarch64-none-elf -x assembler-with-cpp -DNLR_PROGRAM_EXTENSION \
    -c "$SDK/crt0.s" -o "$OUT/obj/crt0_ext.o"
N="$ROOT/tool/nloader/target/debug/nloader"
"$N" program --decl "$OUT/obj/log3g10.json" \
    --object "$OUT/obj/crt0_app.o" --object "$OUT/obj/page.o" \
    --object "$OUT/obj/log3g10.o" \
    --manifest "$OUT/MANIFEST.page" --out "$OUT/LOG3G10.NLB"
"$N" program --decl "$OUT/obj/log3g10x.json" \
    --object "$OUT/obj/crt0_ext.o" --object "$OUT/obj/start.o" \
    --object "$OUT/obj/log3g10.o" \
    --manifest "$OUT/MANIFEST.start" --out "$OUT/LOG3G10X.NLB"
