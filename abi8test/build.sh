#!/bin/sh
# build.sh <new out dir> [decl] [clang]: ABI8T.NLB, the firmware-by-name
# check (a shell, ABI 8). The declaration names the body: abi8test.json for
# the Z 6III 2.00 (the default), abi8test-z8.json for the Z 8 3.11. The
# source is the same; only the hook test's setting is body-specific (a store
# item id, not an address): white balance on the Z 6III, none on the Z 8,
# whose profile declares no store.notify. The output directory must not
# exist yet.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
OUT=${1:?usage: build.sh <new out dir> [decl] [clang]}
DECL=${2:-$HERE/abi8test.json}
CC=${3:-clang}
[ -e "$OUT" ] && { echo "refusing: $OUT exists" >&2; exit 1; }
[ -f "$DECL" ] || { echo "no declaration $DECL" >&2; exit 1; }
MODELS=$(tr 'A-Z' 'a-z' < "$DECL" | tr -d ' \n' | sed -n 's/.*"models":"\([^"]*\)".*/\1/p')
case "$MODELS" in
z6iii/2.00) BODY="-DABI8_NOTIFY_ID=0x07000510u -DABI8_NOTIFY_VALUE=3u -DABI8_NOTIFY_RESET=0u" ;;
z8/3.11) BODY="" ;;
*) echo "refusing: $DECL must name exactly one body (z6iii/2.00 or z8/3.11), not '$MODELS'" >&2
   exit 1 ;;
esac
RES=$($CC -print-resource-dir)/include
mkdir -p "$OUT/obj"
# shellcheck disable=SC2086 # BODY is a list of options
$CC --target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only -ffreestanding \
    -fno-common -fno-stack-protector -fno-unwind-tables \
    -fno-asynchronous-unwind-tables -fPIC -fvisibility=hidden -nostdinc \
    -isystem "$RES" -I "$ROOT/sdk" -fno-jump-tables -fno-builtin -O2 \
    -Wall -Wextra -Werror $BODY -c "$HERE/abi8test.c" -o "$OUT/obj/abi8test.o"
$CC --target=aarch64-none-elf -x assembler-with-cpp -DNLR_PROGRAM_FOREGROUND \
    -c "$ROOT/sdk/crt0.s" -o "$OUT/obj/crt0.o"
"$ROOT/tool/nloader/target/debug/nloader" program --decl "$DECL" \
    --object "$OUT/obj/crt0.o" --object "$OUT/obj/abi8test.o" \
    --manifest "$OUT/MANIFEST" --out "$OUT/ABI8T.NLB"
