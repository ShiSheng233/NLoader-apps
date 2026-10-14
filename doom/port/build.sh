#!/bin/bash
set -euo pipefail

port_dir="$(cd "$(dirname "$0")" && pwd)"
sdk_dir="$(cd "$port_dir/../../.." && pwd)"
repo_dir="$(cd "$sdk_dir/.." && pwd)"
engine_dir="$port_dir/../engine"
compiler="${NLR_DOOM_CC:-clang}"
nloader="$repo_dir/tool/nloader/target/release/nloader"
output=""
manifest=""
work=""
temporary_work=0
diagnostic=0

while (($#)); do
    case "$1" in
        --out) output="$2"; shift 2 ;;
        --manifest) manifest="$2"; shift 2 ;;
        --nloader) nloader="$2"; shift 2 ;;
        --cc) compiler="$2"; shift 2 ;;
        --work-dir) work="$2"; shift 2 ;;
        --diagnostic) diagnostic=1; shift ;;
        *) echo "Usage: $0 --out DOOM.NLB [--diagnostic] [--manifest MANIFEST] [--nloader PATH] [--cc PATH] [--work-dir NEW_DIRECTORY]" >&2; exit 2 ;;
    esac
done
[[ -n "$output" && ! -e "$output" ]] || { echo "A new --out file is required" >&2; exit 2; }
[[ -z "$manifest" || ! -e "$manifest" ]] || { echo "Refusing existing MANIFEST" >&2; exit 2; }
[[ -x "$nloader" ]] || { echo "Build the Rust nloader CLI or pass --nloader" >&2; exit 2; }
[[ "$(git -C "$engine_dir" rev-parse HEAD)" == e257a09527062bfb986b6e5e0780839b6cbd5b71 ]] || { echo "Unexpected doomgeneric revision" >&2; exit 2; }
git -C "$engine_dir" diff --quiet HEAD -- || { echo "The pinned engine has local changes" >&2; exit 2; }
(cd "$port_dir/third_party/printf" && shasum -a 256 -c SHA256SUMS)
if [[ -n "$work" ]]; then
    [[ ! -e "$work" ]] || { echo "Work directory must be new" >&2; exit 2; }
    mkdir -p "$work"
    work="$(cd "$work" && pwd)"
else
    work="$(mktemp -d "${TMPDIR:-/tmp}/nloader-doom.XXXXXX")"
    temporary_work=1
fi
cleanup() { if [[ "$temporary_work" == 1 ]]; then rm -rf "$work"; fi; }
trap cleanup EXIT

flags=(--target=aarch64-none-elf -march=armv8-a -ffreestanding -fno-builtin -fno-common
    -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables
    -fPIC -fvisibility=hidden -Os -Wall -Wextra
    -DNORMALUNIX -DLINUX -DSNDSERV -D_DEFAULT_SOURCE
    -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200
    "-I$port_dir/include" "-I$port_dir" "-I$sdk_dir" "-I$engine_dir/doomgeneric")
declaration="$port_dir/../doom_program.json"
if [[ "$diagnostic" == 1 ]]; then
    flags+=(-DNLR_DOOM_DIAGNOSTIC_FRAMES=90u)
    declaration="$port_dir/../doom_render.json"
fi
objects=()
# Keep the fixed-magic observer first in both the supplied order and object
# name order, so bounded readers find it at the start of initialized data.
"$compiler" "${flags[@]}" -Werror -c "$port_dir/observation.c" \
    -o "$work/000_observation.o" 2>"$work/observation.log"
objects+=(--object "$work/000_observation.o")
while IFS= read -r source; do
    [[ -n "$source" ]] || continue
    object="$work/${source%.c}.o"
    # Preserve vendor diagnostics. The pinned upstream predates current Clang
    # warnings; only our adapter sources below use warnings-as-errors.
    source_flags=("${flags[@]}")
    if [[ "$source" == i_system.c ]]; then source_flags+=(-DI_Quit=nlr_doom_engine_shutdown); fi
    "$compiler" "${source_flags[@]}" -c "$engine_dir/doomgeneric/$source" -o "$object" 2>"$work/${source%.c}.log"
    objects+=(--object "$object")
done < "$port_dir/engine-sources.txt"
for source in entry platform input quit libc_memory libc_string libc_stdio libc_format printf_vendor; do
    object="$work/$source.o"
    "$compiler" "${flags[@]}" -Werror -c "$port_dir/$source.c" -o "$object" 2>"$work/$source.log"
    objects+=(--object "$object")
done
pack=("$nloader" program --decl "$declaration" "${objects[@]}")
if [[ -n "$manifest" ]]; then pack+=(--manifest "$manifest"); fi
"${pack[@]}" --out "$output"
