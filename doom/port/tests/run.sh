#!/bin/bash
set -euo pipefail

port_dir="$(cd "$(dirname "$0")/.." && pwd)"
sdk_dir="$(cd "$port_dir/../../.." && pwd)"
engine_dir="$port_dir/../engine/doomgeneric"
compiler="${NLR_DOOM_HOST_CC:-clang}"
output=""
wad=""
sanitize=0
while (($#)); do
    case "$1" in
        --out) output="$2"; shift 2 ;;
        --wad) wad="$2"; shift 2 ;;
        --sanitize) sanitize=1; shift ;;
        *) echo "Usage: $0 --out NEW_DIRECTORY [--wad LEGAL_IWAD] [--sanitize]" >&2; exit 2 ;;
    esac
done
[[ -n "$output" && ! -e "$output" ]] || { echo "A new --out directory is required" >&2; exit 2; }
[[ "$sanitize" == 0 || -z "$wad" ]] || { echo "--sanitize covers the libc unit tests only" >&2; exit 2; }
mkdir -p "$output"
output="$(cd "$output" && pwd)"
flags=(-O1 -g -fno-builtin -fno-common -Wall -Wextra -Werror -DNLR_DOOM_HOST_TEST
    "-I$port_dir/include" "-I$port_dir" "-I$sdk_dir")
link_flags=(-O1 -g -Wall -Wextra -Werror "-I$sdk_dir")
if [[ "$sanitize" == 1 ]]; then
    flags+=(-fno-omit-frame-pointer -fsanitize=address,undefined)
    link_flags+=(-fsanitize=address,undefined)
fi
objects=()
for source in libc_memory libc_string libc_stdio libc_format printf_vendor; do
    "$compiler" "${flags[@]}" -c "$port_dir/$source.c" -o "$output/$source.o"
    objects+=("$output/$source.o")
done
"$compiler" "${link_flags[@]}" "$port_dir/tests/test_libc.c" "${objects[@]}" -o "$output/test_libc"
"$output/test_libc" >"$output/libc.log" 2>&1
cat "$output/libc.log"
"$compiler" "${flags[@]}" "-I$engine_dir" -c "$port_dir/input.c" -o "$output/input.o"
"$compiler" "${link_flags[@]}" "-I$engine_dir" "$port_dir/tests/test_input.c" \
    "$output/input.o" -o "$output/test_input"
"$output/test_input" >"$output/input.log" 2>&1
cat "$output/input.log"
if [[ -z "$wad" ]]; then exit 0; fi
[[ -f "$wad" ]] || { echo "WAD file not found" >&2; exit 2; }
wad="$(cd "$(dirname "$wad")" && pwd)/$(basename "$wad")"

engine_flags=(-Os -g -fno-builtin -fno-common -DNLR_DOOM_HOST_TEST
    -DNORMALUNIX -DLINUX -DSNDSERV -D_DEFAULT_SOURCE
    -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200
    "-I$port_dir/include" "-I$port_dir" "-I$sdk_dir" "-I$engine_dir")
while IFS= read -r source; do
    [[ -n "$source" ]] || continue
    object="$output/${source%.c}.o"
    source_flags=("${engine_flags[@]}")
    if [[ "$source" == i_system.c ]]; then source_flags+=(-DI_Quit=nlr_doom_engine_shutdown); fi
    "$compiler" "${source_flags[@]}" -c "$engine_dir/$source" -o "$object" 2>"$output/${source%.c}.log"
    objects+=("$object")
done < "$port_dir/engine-sources.txt"
for source in quit observation; do
    "$compiler" "${engine_flags[@]}" -Wall -Wextra -Werror -c "$port_dir/$source.c" -o "$output/$source.o"
    objects+=("$output/$source.o")
done
objects+=("$output/input.o")
for source in entry platform; do
    "$compiler" "${engine_flags[@]}" -Wall -Wextra -Werror -DNLR_DOOM_DIAGNOSTIC_FRAMES=90u \
        -c "$port_dir/$source.c" -o "$output/$source-diagnostic.o"
done
"$compiler" "${link_flags[@]}" "-I$engine_dir" "$port_dir/tests/test_engine.c" "${objects[@]}" \
    "$output/entry-diagnostic.o" "$output/platform-diagnostic.o" -o "$output/test_engine"
"$output/test_engine" "$wad" "$output/frame.rgba" 0 >"$output/engine.log" 2>&1
"$output/test_engine" "$wad" "$output/no-frame-begin.rgba" 1 >"$output/begin-unsupported.log" 2>&1
"$output/test_engine" "$wad" "$output/no-frame-end.rgba" 2 >"$output/end-failed.log" 2>&1
tail -n 1 "$output/engine.log"
tail -n 1 "$output/begin-unsupported.log"
tail -n 1 "$output/end-failed.log"
for source in entry platform; do
    "$compiler" "${engine_flags[@]}" -Wall -Wextra -Werror \
        -c "$port_dir/$source.c" -o "$output/$source-interactive.o"
done
"$compiler" "${link_flags[@]}" "-I$engine_dir" "$port_dir/tests/test_engine.c" "${objects[@]}" \
    "$output/entry-interactive.o" "$output/platform-interactive.o" -o "$output/test_interactive"
"$output/test_interactive" "$wad" "$output/interactive.rgba" 3 >"$output/interactive.log" 2>&1
tail -n 1 "$output/interactive.log"
