/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca-Engines: the FM-1's plugin blobs (felucca_plugin.h, ABI 1). What tools/build_plugin_fm1.sh and
 * tools/fep_pack.py make and Felucca's loader (firmware/src/plugin_fm1.c) takes. */
#ifndef FELUCCA_PLUGIN_FM1_H
#define FELUCCA_PLUGIN_FM1_H
#include <stdint.h>

/* On the FM-1 a plugin is a blob in the flash after the firmware's app area (it survives a firmware update), linked
 * for its slot (tools/build_plugin_fm1.sh): its code runs in place (XIP), its .data / .bss in a RAM block of the slot.
 * A blob starts with fe_fm1_hdr_t; the image after it is the code and constants, then the .data's first values.
 * The engine numbers of plugins of their own are kept in an append-only list (FE_FM1_REG: 64 ids, entry k = number
 * ENGI_PLUG0 + k, 0xFFFFFFFF free): a number once given is never given to another plugin. */
#define FE_FM1_NSLOT 2
#define FE_FM1_SLOT_FLASH(k) ((k) ? 0x95000u : 0x93000u)    /* flash offsets */
#define FE_FM1_SLOT_SIZE(k) ((k) ? 0x1E00u : 0x2000u)
#define FE_FM1_SLOT_RAM(k) (0x01C72000u + (uint32_t)(k) * 0x1000u)
#define FE_FM1_SLOT_RAMSIZE 0x1000u
#define FE_FM1_REG 0x96E00u                                 /* 64 x uint32_t */
#define FE_FM1_REG_N 64u
#define FE_FM1_MAGIC 0x31504546u                            /* "FEP1" */
#define FE_FM1_PLATFORM 0x20314D46u                         /* "FM1 " */
typedef struct {
    uint32_t magic;              /* FE_FM1_MAGIC */
    uint16_t hdr_ver, hdr_size;  /* 1, sizeof (96) */
    uint32_t platform;           /* FE_FM1_PLATFORM */
    uint32_t abi, id, version, engine;   /* as its fe_plugin_t */
    uint32_t xip;                /* where the image is linked: the slot's flash in the XIP window + hdr_size (the window
                                  * maps flash offset f at 0x02000000 + f - 0x4000) */
    uint32_t image_len, image_crc;       /* the image after the header; zlib CRC-32 */
    uint32_t entry;              /* felucca_plugin() */
    uint32_t data_off, data_addr, data_len;  /* .data: its first values at image + data_off, copied to data_addr */
    uint32_t bss_addr, bss_len;  /* zeroed */
    char name[16];               /* the engine's name, for a list (0-padded) */
    uint32_t slot;               /* the slot it is linked for */
    uint32_t reserved[3];
} fe_fm1_hdr_t;

#endif /* FELUCCA_PLUGIN_FM1_H */
