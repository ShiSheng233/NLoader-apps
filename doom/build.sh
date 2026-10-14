#!/bin/sh
# build.sh <new out dir> [clang]: DOOM as a foreground program (DOOM.NLB).
#
# Compiles the pinned doomgeneric engine (engine/, a submodule), the NLoader
# platform layer (doomgeneric_nlr.c) and the SDK's freestanding C library
# (sdk/libc) for the camera, then packages them with `nloader program`.
# The output directory must not exist yet: nothing is ever overwritten.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
OUT=${1:?usage: build.sh <new out dir> [clang]}
CC=${2:-clang}
[ -e "$OUT" ] && { echo "refusing: $OUT exists" >&2; exit 1; }
DG=$HERE/engine/doomgeneric
[ -f "$DG/doomgeneric.c" ] || { echo "engine/ is empty: git submodule update --init sdk/examples/doom/engine" >&2; exit 1; }
RES=$($CC -print-resource-dir)/include
mkdir -p "$OUT/obj"
# The loader's program contract: AArch64, freestanding, PIC with every
# symbol local (nlr_hidden.h), no relative lookup tables across segments.
COMMON="--target=aarch64-none-elf -march=armv8-a -ffreestanding -fno-common
 -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables
 -fPIC -fvisibility=hidden -nostdinc -isystem $RES -I $ROOT/sdk/libc/include
 -I $ROOT/sdk -include nlr_hidden.h -fno-jump-tables -O2"
DOOMDEFS="-DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -DNORMALUNIX -I $DG -Wno-everything"

SRCS="dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop
 d_main d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff info i_cdmus
 i_endoom i_joystick i_scale i_sound i_system i_timer memio m_argv m_bbox
 m_cheat m_config m_controls m_fixed m_menu m_misc m_random p_ceilng p_doors
 p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj p_plats p_pspr
 p_saveg p_setup p_sight p_spec p_switch p_telept p_tick p_user r_bsp r_data
 r_draw r_main r_plane r_segs r_sky r_things sha1 sounds statdump st_lib
 st_stuff s_sound tables v_video wi_stuff w_checksum w_file w_main w_wad
 z_zone w_file_stdc i_input i_video doomgeneric"
OBJS=""
for s in $SRCS; do
    $CC $COMMON $DOOMDEFS -c "$DG/$s.c" -o "$OUT/obj/$s.o"
    OBJS="$OBJS --object $OUT/obj/$s.o"
done
$CC $COMMON $DOOMDEFS -c "$HERE/doomgeneric_nlr.c" -o "$OUT/obj/doomgeneric_nlr.o"
# The C library must not be turned back into calls to itself.
$CC $COMMON -fno-builtin -c "$ROOT/sdk/libc/libc.c" -o "$OUT/obj/libc.o"
$CC $COMMON -c "$ROOT/sdk/libc/libm.c" -o "$OUT/obj/libm.o"
$CC --target=aarch64-none-elf -x assembler-with-cpp -DNLR_PROGRAM_FOREGROUND \
    -c "$ROOT/sdk/crt0.s" -o "$OUT/obj/crt0.o"
"$ROOT/tool/nloader/target/debug/nloader" program --decl "$HERE/doom.json" \
    --object "$OUT/obj/crt0.o" $OBJS --object "$OUT/obj/doomgeneric_nlr.o" \
    --object "$OUT/obj/libc.o" --object "$OUT/obj/libm.o" \
    --manifest "$OUT/MANIFEST" --out "$OUT/DOOM.NLB"
