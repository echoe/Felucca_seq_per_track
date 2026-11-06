#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Packs an FM-1 plugin (tools/build_plugin_fm1.sh) into its blob: fe_fm1_hdr_t (src/felucca_plugin_fm1.h), then the
image (the code and constants, then the .data's first values).

  fep_pack.py WORK_DIR SLOT OUT.fep      (WORK_DIR: text.bin, data.bin, syms.txt from the linked plugin)

It checks what the loader would refuse: the descriptor (FE_MAGIC, FE_ABI, 44.1 kHz, CTL 32, 8 voices, 4 parts), the
image in its slot, .data / .bss in the slot's RAM, and the update loader's records: bytes 0xF00..0xF07 of each 4 KiB
sector of this flash may hold anything but a record ("AT" at 6..7), as felucca-bsp's fm1_flash.h asks.
"""
import re
import struct
import sys
import zlib
from pathlib import Path

HDR_SIZE = 96
SLOT_FLASH = (0x93000, 0x95000)        # felucca_plugin_fm1.h FE_FM1_SLOT_FLASH
SLOT_SIZE = (0x2000, 0x1E00)
SLOT_RAM = (0x01C72000, 0x01C73000)
SLOT_RAMSIZE = 0x1000
FE_MAGIC, FE_ABI = 0x46455031, 1
ENGINE_SIZE_1 = 228                   # felucca_plugin.h FE_ENGINE_SIZE_1 on the FM-1
FM1_MAGIC, FM1_PLATFORM = 0x31504546, 0x20314D46


def xip(off):
    return 0x02000000 + off - 0x4000


def main(work, slot, out):
    work = Path(work)
    syms = {}
    for ln in (work / "syms.txt").read_text().splitlines():
        m = re.match(r"^([0-9a-f]{8})\s.*\s(\S+)$", ln)
        if m:
            syms[m.group(2)] = int(m.group(1), 16)
    text = (work / "text.bin").read_bytes()
    data = (work / "data.bin").read_bytes() if (work / "data.bin").exists() else b""
    base = xip(SLOT_FLASH[slot]) + HDR_SIZE
    for need in ("felucca_plugin", "fe_desc", "_fe_data", "_fe_edata", "_fe_data_load", "_fe_bss", "_fe_ebss"):
        if need not in syms:
            sys.exit(f"fep_pack: no symbol {need} (is it a plugin of felucca_plugin.h?)")

    def at(addr, n):
        o = addr - base
        if not 0 <= o <= len(text) - n:
            sys.exit(f"fep_pack: {addr:#x} is not in the plugin's constants")
        return text[o:o + n]

    d = struct.unpack("<17I", at(syms["fe_desc"], 68))   # fe_plugin_t on the FM-1 (32-bit pointers)
    magic, abi, size, pid, ver, fs, ctl, nvoice, npart, engine, eptr, e_size = d[:12]
    if (magic, abi, size, fs, ctl, nvoice, npart) != (FE_MAGIC, FE_ABI, 68, 44100, 32, 8, 4) or e_size < ENGINE_SIZE_1:
        sys.exit(f"fep_pack: the descriptor is not one the FM-1 takes ({d[:12]})")
    nptr = struct.unpack("<I", at(eptr, 4))[0]
    o = nptr - base
    name = text[o:text.index(b"\0", o)][:15]
    data_len = syms["_fe_edata"] - syms["_fe_data"]
    bss_len = syms["_fe_ebss"] - syms["_fe_bss"]
    data_off = syms["_fe_data_load"] - base
    if data_len and (data_off != len(text) or len(data) != data_len):
        sys.exit("fep_pack: .data is not right after the constants")
    if syms["_fe_ebss"] > SLOT_RAM[slot] + SLOT_RAMSIZE or syms["_fe_data"] < SLOT_RAM[slot]:
        sys.exit(f"fep_pack: .data + .bss ({data_len + bss_len} B) over the slot's {SLOT_RAMSIZE} B of RAM")
    image = text + data
    if HDR_SIZE + len(image) > SLOT_SIZE[slot]:
        sys.exit(f"fep_pack: {HDR_SIZE + len(image)} B over slot {slot}'s {SLOT_SIZE[slot]} B")
    hdr = struct.pack("<IHHIIIIIIIIIIIIII16sI12x", FM1_MAGIC, 1, HDR_SIZE, FM1_PLATFORM, abi, pid, ver, engine,
                      base, len(image), zlib.crc32(image), syms["felucca_plugin"], data_off if data_len else 0,
                      syms["_fe_data"], data_len, syms["_fe_bss"], bss_len, name, slot)
    assert len(hdr) == HDR_SIZE
    blob = hdr + image
    sec = SLOT_FLASH[slot]
    while sec < SLOT_FLASH[slot] + SLOT_SIZE[slot]:      # the update loader's record place of each sector
        o = sec + 0xF00 - SLOT_FLASH[slot]
        if blob[o + 6:o + 8] == b"AT":
            sys.exit(f"fep_pack: bytes {o + 6:#x}.. are 'AT' (an update record's mark): move the code")
        sec += 0x1000
    Path(out).write_bytes(blob)
    print(f"{out}: {name.decode()} slot {slot}, {len(blob)} B of {SLOT_SIZE[slot]}, RAM {data_len + bss_len} B, "
          f"engine {engine if engine != 0xFF else 'new'}, id {pid:08x}")


if __name__ == "__main__":
    if len(sys.argv) != 4 or sys.argv[2] not in ("0", "1"):
        sys.exit(__doc__)
    main(sys.argv[1], int(sys.argv[2]), sys.argv[3])
