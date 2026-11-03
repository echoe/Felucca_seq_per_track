#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Builds an engine plugin (src/felucca_plugin.h) for the FM-1: linked for one of its slots (FE_FM1_SLOT_*), on the
# host's tables (gen_engine_tables.py --shared), packed with its header (tools/fep_pack.py):
#   sh tools/build_plugin_fm1.sh plugins/lofi.c SLOT [OUT_DIR]    -> OUT_DIR/lofi.s0.fep (SLOT 0 or 1; default build/fm1)
# Needs the JieLi toolchain (clang for pi32v2; JIELI_TOOLCHAIN, default ~/.jieli/toolchain). It runs natively on
# Linux x86-64, elsewhere in Docker (linux/amd64 debian:bookworm-slim; JIELI_DOCKER=0 / 1 forces either way).
set -e
src=$1
slot=$2
[ -n "$src" ] && { [ "$slot" = 0 ] || [ "$slot" = 1 ]; } || { echo "usage: $0 plugin.c 0|1 [out_dir]" >&2; exit 2; }
here=$(cd "$(dirname "$0")/.." && pwd)
out=${3:-$here/build/fm1}
mkdir -p "$out"
out=$(cd "$out" && pwd)
srcdir=$(cd "$(dirname "$src")" && pwd)
name=$(basename "$src" .c)
tc=${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}
[ -x "$tc/pi32v2/bin/clang" ] || { echo "build_plugin_fm1: no JieLi toolchain in $tc" >&2; exit 1; }
work="$out/$name.s$slot.work"
mkdir -p "$work/tables"
python3 "$here/tools/gen_engine_tables.py" --shared "$work/tables/fe_tables.h"
if [ "$slot" = 0 ]; then flash=0x93000; size=0x2000; ram=0x01C72000; else flash=0x95000; size=0x1E00; ram=0x01C73000; fi
# (felucca_plugin.h FE_FM1_SLOT_*: the same numbers; the loader checks the header against its own)
xip=$(printf '0x%08X' $((0x02000000 + flash - 0x4000)))
cat > "$work/slot.ld" <<LD
MEMORY
{
    XIP (rx) : ORIGIN = $xip + 96, LENGTH = $size - 96
    RAM (rw) : ORIGIN = $ram, LENGTH = 0x1000
}
SECTIONS
{
    .text : { KEEP(*(.text.felucca_plugin)) *(.text*) *(.rodata*) . = ALIGN(4); } > XIP
    .data : { . = ALIGN(4); _fe_data = .; *(.data*) *(.sdata*) . = ALIGN(4); _fe_edata = .; } > RAM AT > XIP
    _fe_data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _fe_bss = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _fe_ebss = .; } > RAM
    /DISCARD/ : { *(.comment) *(.note*) }
}
LD
native=0
[ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] && native=1
case "${JIELI_DOCKER:-}" in 0) native=1 ;; 1) native=0 ;; esac
run() {   # a toolchain binary ($1, relative to the toolchain) with /eng, /src and /out as the paths below
    t=$1; shift
    if [ $native = 1 ]; then
        "$tc/$t" "$@"
    else
        docker run --rm --platform linux/amd64 -v "$here:/eng:ro" -v "$srcdir:/src:ro" -v "$out:/out" \
            -v "$tc:/opt/jieli:ro" -w /out debian:bookworm-slim "/opt/jieli/$t" "$@"
    fi
}
if [ $native = 1 ]; then E=$here; S=$srcdir; O=$out; else E=/eng; S=/src; O=/out; fi
W=$O/$name.s$slot.work
run pi32v2/bin/clang -target pi32v2 -Os -ffunction-sections -fdata-sections -fno-builtin -Wall \
    -Wno-unused-function -I"$W/tables" -I"$E/src" -c "$S/$name.c" -o "$W/$name.o"
run pi32v2/bin/ld -T "$W/slot.ld" --gc-sections -e felucca_plugin "$W/$name.o" -o "$W/$name.elf"
run common/bin/objcopy -O binary -j .text "$W/$name.elf" "$W/text.bin"
run common/bin/objcopy -O binary -j .data "$W/$name.elf" "$W/data.bin"
run common/bin/objdump -t "$W/$name.elf" > "$work/syms.txt"
python3 "$here/tools/fep_pack.py" "$work" "$slot" "$out/$name.s$slot.fep"
