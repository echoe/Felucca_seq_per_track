/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The user sample slots with the sample engines retired (core.h FELUCCA_SMPENG=0; engines.c includes this instead of eng_sample.c).
 * Only what the editor protocol, the backup and the project store still name: the slots' flash map and header, and no slot
 * ever holding a sample (usr_nz 0: the editor's SMP commands answer "no slots", backup finds none; the flash those slots had
 * is the sequence banks', storage.c). The layout is eng_sample.c's, byte for byte. */
#include "../bsp/chip/fm1_xip.h"
typedef struct {
    uint32_t off, n, ls, le;
    uint32_t rate;
    int16_t root16;
    int16_t pred;
    uint8_t idx, lo, hi, looped;
} smp_zone_t;
#define SMP_USER_SLOTS 3
#define SMP_USER_BASE 0xA0000u
#define SMP_USER_SIZE 0x14000u
#define SMP_USER_DATA 512u
#define SMP_USER_MAGIC 0x504D5346u                  /* "FSMP" */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t nz, rsv;
    char name[8];
    uint32_t data_len, crc, rsv2[2];
    smp_zone_t zone[16];
} smp_user_hdr_t;
static smp_zone_t usr_zone[SMP_USER_SLOTS][16];
static uint8_t usr_nz[SMP_USER_SLOTS];
#ifndef SMP_USER_XIP
#define SMP_USER_XIP(k) fm1_xip_ptr(SMP_USER_BASE + (k) * SMP_USER_SIZE)
#endif
static const uint8_t *smp_user_xip(uint32_t k) { return SMP_USER_XIP(k); }
static void smp_user_scan(uint32_t k) { if (k < SMP_USER_SLOTS) usr_nz[k] = 0; }   /* no sample engine reads a slot: none is in use */
