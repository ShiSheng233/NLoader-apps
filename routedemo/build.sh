#!/bin/sh
# build.sh <new out dir> [clang] [routedemo|routeshl]: the input_route
# fixture (family B). routedemo (default) is a program over the menu,
# routeshl the same code declared as a shell; each appends one line per
# route and event to APPS/<NAME>.LOG. The output directory must not exist.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
OUT=${1:?usage: build.sh <new out dir> [clang] [routedemo|routeshl]}
CC=${2:-clang}
WHICH=${3:-routedemo}
case "$WHICH" in
    routedemo) NAME=ROUTEDEMO ;;
    routeshl) NAME=ROUTESHL ;;
    *) echo "unknown program $WHICH" >&2; exit 1 ;;
esac
[ -e "$OUT" ] && { echo "refusing: $OUT exists" >&2; exit 1; }
RES=$($CC -print-resource-dir)/include
mkdir -p "$OUT/obj"
$CC --target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only -ffreestanding \
    -fno-common -fno-stack-protector -fno-unwind-tables \
    -fno-asynchronous-unwind-tables -fPIC -fvisibility=hidden -nostdinc \
    -isystem "$RES" -I "$ROOT/sdk" -fno-jump-tables -fno-builtin -O2 \
    -Wall -Wextra -Werror "-DROUTEDEMO_LOG=\"APPS/$NAME.LOG\"" \
    -c "$HERE/routedemo.c" -o "$OUT/obj/routedemo.o"
$CC --target=aarch64-none-elf -x assembler-with-cpp -DNLR_PROGRAM_FOREGROUND \
    -c "$ROOT/sdk/crt0.s" -o "$OUT/obj/crt0.o"
"$ROOT/tool/nloader/target/debug/nloader" program --decl "$HERE/$WHICH.json" \
    --object "$OUT/obj/crt0.o" --object "$OUT/obj/routedemo.o" \
    --manifest "$OUT/MANIFEST" --out "$OUT/$NAME.NLB"
