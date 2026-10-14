#!/bin/sh
# build.sh <new out dir> [clang]: the file manager (FM.NLB), on the SDK's
# freestanding C library. The output directory must not exist yet.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
OUT=${1:?usage: build.sh <new out dir> [clang]}
CC=${2:-clang}
NLOADER=${NLR_CLI:-"$ROOT/tool/nloader/target/debug/nloader"}
[ -e "$OUT" ] && { echo "refusing: $OUT exists" >&2; exit 1; }
[ -x "$NLOADER" ] || { echo "Build nloader or set NLR_CLI" >&2; exit 1; }
RES=$("$CC" -print-resource-dir)/include
mkdir -p "$OUT/obj"
compile() {
    "$CC" --target=aarch64-none-elf -march=armv8-a -ffreestanding -fno-common \
        -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
        -fPIC -fvisibility=hidden -nostdinc -isystem "$RES" -I "$ROOT/sdk/libc/include" \
        -I "$ROOT/sdk" -include nlr_hidden.h -fno-jump-tables -O2 "$@"
}
compile -Wall -Wextra -Werror -c "$HERE/fm.c" -o "$OUT/obj/fm.o"
compile -fno-builtin -Wall -Wextra -Werror -c "$ROOT/sdk/libc/libc.c" -o "$OUT/obj/libc.o"
"$CC" --target=aarch64-none-elf -x assembler-with-cpp -DNLR_PROGRAM_FOREGROUND \
    -c "$ROOT/sdk/crt0.s" -o "$OUT/obj/crt0.o"
"$NLOADER" program --decl "$HERE/fm.json" \
    --object "$OUT/obj/crt0.o" --object "$OUT/obj/fm.o" --object "$OUT/obj/libc.o" \
    --manifest "$OUT/MANIFEST" --out "$OUT/FM.NLB"
