/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca-Engines: the plugin interface, ABI 1. An engine built on its own (a shared library on a desktop, a blob
 * in the FM-1's flash: felucca_plugin_fm1.h) that a Felucca host loads at run time: no firmware build for a new
 * engine. docs/PLUGINS.md is the guide; this header is the contract.
 *
 * The engine's source is the same as for a built-in engine (eng_*.c, felucca_engine.h); a plugin file compiles it
 * with this header instead of a host:
 *   #include "fe_tables.h"          python3 tools/gen_engine_tables.py fe_tables.h (the host's rate: FS is checked);
 *                                   on a small target --shared: the host's tables (fe_host_t.tables), one copy
 *   #include "felucca_plugin.h"     NVOICE, NPART, the parameter numbers, track_t and trk (below)
 *   #include "dsp.c"
 *   #include "eng_lofi.c"
 *   FE_PLUGIN(ENG_LOFI, FE_ID('L', 'O', 'F', 'I'), 1, 3, 0)
 * and tools/build_plugin.sh makes the library. FE_PLUGIN defines the one exported function, felucca_plugin().
 *
 * What a plugin sees of a part (fe_track_t, the struct track of a plugin): the parameters an engine reads (FE_P_*:
 * the amplitude envelope's attack and release, the filter type, the engine's eight), its voices (the host's own,
 * voice_t as in felucca_engine.h) and its engine memory. The host copies the parameters in before each call, so a
 * plugin reads them as a built-in engine reads t->p[]; it never writes them. trk is the host's array of NPART views
 * (WHEEL finds a part's number as t - trk; t->part is the same).
 *
 * The host checks, before it uses a plugin: magic, abi, FS and CTL (the tables' rate and block), NVOICE, NPART,
 * mem (at most FE_MEM_MAX), the engine's functions (note_on, render) and its names. A plugin that fails is not
 * loaded.
 *
 * engine: the number the plugin plays as (the stores and the editor protocol hold these numbers). A number of
 * one of Felucca's own engines (0 .. 14, not DRUM) takes its place: a sound saved with that engine plays through
 * the plugin (an engine moved out of the firmware keeps its number, and saved work sounds as it did). FE_NEW asks
 * for a number of the host's plugin range (32 and up), which the host keeps for the plugin's id.
 *
 * id: FE_ID of four printable characters, one per engine whoever made it. Ids of upper-case letters and digits
 * only are Hügelton's (Felucca's own engines); pick yours with a lower-case letter in it (FE_ID('k', 'R', 'O', '1')),
 * and list it in docs/PLUGIN_IDS.md by a pull request so that no two engines share one.
 *
 * ---------------------------------------------------------------------------------------------------------------
 * Stability. ABI 1 is frozen: every struct below and in felucca_engine.h (param_desc_t, voice_t, vmod_t, preset_t,
 * engine_t) keeps its layout for as long as FE_ABI is 1. It grows in one way only: a field added at the end of
 * fe_plugin_t, fe_host_t or engine_t, whose 0 means what the interface did without it. Each side says how much it
 * has: fe_plugin_t.size and .e_size (the plugin's sizeof of fe_plugin_t and of engine_t), fe_host_t.size (the
 * host's); a reader takes the fields the other side has and reads the rest as 0. A new FE_P_* goes into one of
 * fe_track_t's spare parameters (FE_NP_MAX), a new optional flag into fe_plugin_t.flags. Anything else (a field
 * moved or changed, a function's meaning) is FE_ABI 2, and a host of 2 loads no plugin of 1 unless it says so. */
#ifndef FELUCCA_PLUGIN_H
#define FELUCCA_PLUGIN_H
#include <stddef.h>
#include <stdint.h>

#define FE_MAGIC 0x46455031u     /* "FEP1" */
#define FE_ABI 1u                /* this header's layout (see Stability above) */
#define FE_NVOICE 8              /* Felucca's: voices per part */
#define FE_NPART 4               /* parts */
#define FE_MEM_MAX 12888u        /* bytes of engine memory a part has (engine_t.mem) */
#define FE_NEW 0xFFu             /* engine: a new number from the host */
#define FE_ID(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

/* the parameters of fe_track_t.p[]: FE_NP of them now, room for FE_NP_MAX (the spare ones read 0) */
enum { FE_P_ATK, FE_P_REL, FE_P_FTYPE, FE_P_E0, FE_P_E1, FE_P_E2, FE_P_E3, FE_P_E4, FE_P_E5, FE_P_E6, FE_P_E7,
       FE_NP };
#define FE_NP_MAX 16

/* fe_host_t.platform: where the plugin runs (for a plugin that would tune itself; most never read it) */
#define FE_PLATFORM_FM1 FE_ID('F', 'M', '1', ' ')
#define FE_PLATFORM_DESKTOP FE_ID('D', 'E', 'S', 'K')     /* the SDL2 build */
#define FE_PLATFORM_LIBRETRO FE_ID('L', 'R', 'T', 'O')
#define FE_PLATFORM_WEB FE_ID('W', 'E', 'B', ' ')

#ifndef NVOICE
#define NVOICE FE_NVOICE
#endif
#ifndef NPART
#define NPART FE_NPART
#endif
#include "felucca_engine.h"

#define FE_TRACK_FIELDS                                                                                       \
    int16_t p[FE_NP_MAX];        /* FE_P_* (the spare ones 0) */                                              \
    uint16_t part;               /* 0 .. NPART - 1 */                                                         \
    uint16_t rsv0;                                                                                            \
    voice_t *v;                  /* the part's NVOICE voices */                                               \
    void *mem;                   /* its engine memory (engine_t.mem bytes, zero on a switch to the engine) */ \
    uint32_t rsv[2]
typedef struct fe_track {        /* a part as a plugin sees it */
    FE_TRACK_FIELDS;
} fe_track_t;

#ifndef FE_TABLES_STRUCT
#define FE_TABLES_STRUCT
struct fe_tables {               /* a host's tables, for its plugins (as tools/gen_engine_tables.py --shared) */
    const int16_t *sine;         /* SINE[1025] */
    const uint32_t *pitch_oct;   /* PITCH_OCT[192] */
    const uint16_t *svf_g;       /* SVF_G[128] */
    const int16_t *tanh_q15;     /* TANH_Q15[257] */
};
#endif

typedef struct {                 /* what the host gives a plugin */
    uint32_t abi;                /* the host's FE_ABI */
    uint32_t size;               /* the host's sizeof (fe_host_t) */
    uint32_t platform;           /* FE_PLATFORM_* */
    fe_track_t *views;           /* NPART of them, for the plugin's whole life */
    const struct fe_tables *tables;   /* its tables (a plugin built with --shared needs them; 0 = none) */
    uint32_t rsv[4];             /* 0 */
} fe_host_t;

typedef struct {
    uint32_t magic, abi;         /* FE_MAGIC, FE_ABI */
    uint32_t size;               /* sizeof (fe_plugin_t) as the plugin was built */
    uint32_t id;                 /* FE_ID: one per engine, whoever made it */
    uint32_t version;            /* the plugin's own, 1 and up: a list shows it */
    uint32_t fs, ctl, nvoice, npart;
    uint32_t engine;             /* the number it plays as, or FE_NEW */
    const engine_t *e;           /* knob[] in FE_P_* numbers */
    uint32_t e_size;             /* sizeof (engine_t) as the plugin was built */
    void (*reset)(void);         /* optional: the engine's own state as at power-on (0 = none) */
    uint32_t flags;              /* 0 (none defined in ABI 1) */
    uint32_t rsv[3];             /* 0 */
} fe_plugin_t;

/* the function a plugin exports: what the host gives it -> its descriptor (0: not for this host) */
typedef const fe_plugin_t *(*fe_entry_t)(const fe_host_t *h);
#define FE_ENTRY "felucca_plugin"

/* the smallest descriptor and engine a host of ABI 1 takes (the layouts as ABI 1 froze them) */
#define FE_PLUGIN_SIZE_1 ((uint32_t)offsetof(fe_plugin_t, rsv))
#define FE_ENGINE_SIZE_1 ((uint32_t)(offsetof(engine_t, post) + sizeof(void (*)(void))))

#ifndef FE_HOST                  /* ------------------------------------------- the plugin's side --- */
enum { P_ATK = FE_P_ATK, P_REL = FE_P_REL, P_FTYPE = FE_P_FTYPE, P_E0 = FE_P_E0, P_E1, P_E2, P_E3, P_E4, P_E5,
       P_E6, P_E7 };
struct track {
    FE_TRACK_FIELDS;
};
typedef struct track track_t;
static track_t *fe_trk;
#define trk fe_trk
#define ENG_MEM(part) (fe_trk[part].mem)
#define ENG_MEM_FITS(name, size) _Static_assert((size) <= FE_MEM_MAX, name " state over FE_MEM_MAX")

#if defined(_WIN32)
#define FE_EXPORT __declspec(dllexport)
#else
#define FE_EXPORT __attribute__((visibility("default")))
#endif

#ifdef FE_SHARED_TABLES          /* (the tables header of --shared) */
#define FE_SHARES 1
#define FE_TAB_SET(t) (fe_tab = (t))
#else
#define FE_SHARES 0
#define FE_TAB_SET(t) ((void)(t))
#endif

/* eng: the engine_t; id: FE_ID(..); ver: its version; no: the number it plays as (or FE_NEW); reset: 0 or a
 * function that puts the engine's own state back to power-on */
#define FE_PLUGIN(eng, id, ver, no, reset)                                                                     \
    static const fe_plugin_t fe_desc = {FE_MAGIC, FE_ABI, sizeof(fe_plugin_t), (id), (ver), FS, CTL, NVOICE,  \
                                        NPART, (no), &(eng), sizeof(engine_t), (reset), 0, {0, 0, 0}};       \
    FE_EXPORT const fe_plugin_t *felucca_plugin(const fe_host_t *h);                                       \
    FE_EXPORT const fe_plugin_t *felucca_plugin(const fe_host_t *h)                                        \
    {                                                                                                      \
        if (!h || h->abi != FE_ABI || !h->views || (FE_SHARES && !h->tables))                              \
            return 0;                                                                                      \
        fe_trk = (track_t *)h->views;                                                                      \
        FE_TAB_SET(h->tables);                                                                             \
        return &fe_desc;                                                                                   \
    }
#endif
#endif /* FELUCCA_PLUGIN_H */
