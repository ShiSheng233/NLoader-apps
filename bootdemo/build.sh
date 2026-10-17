#!/bin/sh
# build.sh <new out dir> [clang]: BOOTDEMO.NLB, the boot program of the
# Z 8 QEMU boot fixture (tests/qemu/boot_start.py): it logs its start and
# how long it ran to APPS/BOOTLOG.TXT and ends by itself after 16 s. Also
# writes MANIFEST and BOOT (naming it) beside it. The output directory must
# not exist yet.
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
    -Wall -Wextra -Werror -c "$HERE/bootdemo.c" -o "$OUT/obj/bootdemo.o"
$CC --target=aarch64-none-elf -x assembler-with-cpp -DNLR_PROGRAM_FOREGROUND \
    -c "$ROOT/sdk/crt0.s" -o "$OUT/obj/crt0.o"
"$ROOT/tool/nloader/target/debug/nloader" program --decl "$HERE/bootdemo.json" \
    --object "$OUT/obj/crt0.o" --object "$OUT/obj/bootdemo.o" \
    --manifest "$OUT/MANIFEST" --out "$OUT/BOOTDEMO.NLB"
printf 'bootdemo\n' > "$OUT/BOOT"
