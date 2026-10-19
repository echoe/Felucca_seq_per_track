#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Felucca-Engines' own test: the tables, the standalone host (tests/fe_host.c), its renders against
# tests/golden.txt. Run from anywhere:  sh tests/run.sh   (CC=... to pick the compiler; UPDATE=1 rewrites
# the golden file: only for a reviewed, intended change of the sound)
set -e
cd "$(dirname "$0")/.."
mkdir -p build
python3 tools/gen_engine_tables.py build/fe_tables.h
${CC:-cc} -O2 -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Ibuild \
    -o build/fe_host tests/fe_host.c
build/fe_host > build/renders.txt
if [ "${UPDATE:-0}" = 1 ]; then
    cp build/renders.txt tests/golden.txt
    echo "tests/golden.txt rewritten ($(wc -l < tests/golden.txt | tr -d ' ') renders)"
    exit 0
fi
if diff -u tests/golden.txt build/renders.txt; then
    echo "ok: $(wc -l < tests/golden.txt | tr -d ' ') renders as in tests/golden.txt"
else
    echo "FAIL: the renders above differ from tests/golden.txt"
    exit 1
fi
