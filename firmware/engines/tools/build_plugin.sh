#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Builds an engine plugin (src/felucca_plugin.h) as a shared library for the machine it runs on:
#   sh tools/build_plugin.sh plugins/lofi.c [OUT_DIR]    -> OUT_DIR/lofi.{so,dylib,dll} (default build/plugins)
# CC picks the compiler (CC=x86_64-w64-mingw32-gcc makes a .dll). FS=48000 builds it for a host at that rate.
# SHARED=1: the plugin uses its host's tables (gen_engine_tables.py --shared), as on the FM-1 (tools/build_plugin_fm1.sh).
set -e
src=$1
[ -n "$src" ] || { echo "usage: $0 plugin.c [out_dir]" >&2; exit 2; }
here=$(cd "$(dirname "$0")/.." && pwd)
out=${2:-$here/build/plugins}
mkdir -p "$out/tables"
python3 "$here/tools/gen_engine_tables.py" ${FS:+--fs $FS} $([ "${SHARED:-0}" = 1 ] && echo --shared) "$out/tables/fe_tables.h"
cc=${CC:-cc}
case "$($cc -dumpmachine 2>/dev/null)" in
*mingw*|*windows*) ext=dll; lib=-shared ;;
*darwin*|*apple*) ext=dylib; lib=-dynamiclib ;;
*) ext=so; lib=-shared ;;
esac
name=$(basename "$src" .c)
$cc -O2 -std=c99 -fPIC -fvisibility=hidden -ffp-contract=off -Wall -Wextra -Wno-unused-function \
    -Wno-unused-parameter -I"$out/tables" -I"$here/src" $lib -o "$out/$name.$ext" "$src"
echo "$out/$name.$ext"
