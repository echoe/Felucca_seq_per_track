/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FOLD: an example plugin engine of its own (FE_NEW), the template for yours (docs/PLUGINS.md). One sine through a
 * wavefolder, a second sine an interval above for ring modulation, and a decay of the fold amount per note: small
 * enough to read in one go, with every part an engine has (parameters, presets, HOME knobs, note_on, render, an
 * optional per-block hook). Copy this folder, rename it, change the id below, and build:
 *   sh ../../tools/build_plugin.sh fold.c              a desktop library (build/plugins/fold.{so,dylib,dll})
 *   sh ../../tools/build_plugin_fm1.sh fold.c 0        an FM-1 blob for slot 0 (build/fm1/fold.s0.fep)
 * Fixed point only: no float, no division in the render, no library calls, no 64-bit products (on the FM-1, inlined
 * 64-bit multiplies among 16-bit loads can crash it: felucca-bsp docs/CPU.md). */
#include "fe_tables.h"              /* FS, CTL, SINE[], pitch_inc() ... (tools/gen_engine_tables.py) */
#include "felucca_plugin.h"         /* the interface: track_t, voice_t, vmod_t, engine_t, FE_PLUGIN */
#include "dsp.c"                    /* the shared DSP helpers: sine_i, mulq15, voice_amp, softclip, clamp ... */

/* the voice's state: s[0] the fold amount now (Q15), s[1] its decay a block; ph[0] the carrier, ph[1] the ring sine */
static void fold_note_on(track_t *t, voice_t *v)
{
    v->s[0] = (int32_t)t->p[P_E0] << 8;                      /* FOLD 0..127 -> Q15 */
    v->s[1] = t->p[P_E1] ? (128 - t->p[P_E1]) : 0;           /* DECAY: 0 = held */
}

/* once a block, before the voices: nothing here (an example of the optional hook) */
static void fold_block(track_t *t) { (void)t; }

static inline int32_t fold_wave(int32_t x, int32_t amount)   /* x Q15, amount Q15: a sine wavefolder */
{
    int32_t g = 4096 + (amount * 7 >> 3);                     /* the drive, Q12: 1 .. 8 (32-bit products only) */
    int32_t y = x * g >> 12;                                  /* Q15, up to 8 times full scale */
    return sine_i((uint32_t)y << 14);                         /* the fold: a sine of the driven signal */
}

static void fold_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const uint8_t RATIO[4] = {1, 2, 3, 5};             /* RING's interval: x1 x2 x3 x5 */
    uint32_t i, inc2 = m->inc * RATIO[t->p[P_E3] & 3];
    int32_t ring = (int32_t)t->p[P_E2] << 8, amt = v->s[0];
    for (i = 0; i < n; i++) {
        int32_t a = fold_wave(sine_i(v->ph[0]), amt > 32767 ? 32767 : amt);
        int32_t b = sine_i(v->ph[1]);
        int32_t y = a + mulq15(mulq15(a, b) - a, ring);      /* RING 0 = the fold alone, 127 = ring-modulated */
        out[i] += voice_amp(y, m, i);
        v->ph[0] += m->inc;
        v->ph[1] += inc2;
    }
    if (v->s[1])                                              /* DECAY: the fold falls a block at a time */
        v->s[0] -= v->s[0] * v->s[1] >> 10;
}

static const char *const FOLD_RATIO[] = {"x1", "x2", "x3", "x5"};
static const preset_t FOLD_PRESETS[] = {
    /* name, {FOLD, DECAY, RING, RATIO, -, -, -, -}, {A D S R}, fenv, mono */
    {"SOFT", {30, 0, 0, 0, 0, 0, 0, 0}, {4, 80, 100, 40}, 0, 0, FX(0, 20, 10, 30), PAT(0)},
    {"PLUCK", {110, 90, 0, 0, 0, 0, 0, 0}, {0, 60, 0, 50}, 0, 0, FX(0, 0, 30, 30), PAT(0)},
    {"BELL", {70, 60, 90, 3, 0, 0, 0, 0}, {0, 90, 0, 80}, 0, 0, FX(0, 0, 20, 60), PAT(0)},
    {"GROWL", {127, 0, 40, 1, 0, 0, 0, 0}, {0, 127, 127, 20}, 0, 1, FX(20, 0, 0, 20), PAT(0)},
};

static const engine_t ENG_FOLD = {
    .name = "FOLD",                                           /* up to 6 characters on the screen */
    .page_title = {"FOLD", "RING"},                           /* EDIT 1 (P_E0..P_E3), EDIT 2 (P_E4..P_E7) */
    .edit = {
        {"FOLD", F_PCT, 0, 127, 40, 0, 0},
        {"DECAY", F_PCT, 0, 127, 0, 0, 0},
        {"RING", F_PCT, 0, 127, 0, 0, 0},
        {"RATIO", F_ENUM, 0, 3, 0, FOLD_RATIO, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},                          /* (unused parameters: a label and a range of 0) */
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
    },
    .presets = FOLD_PRESETS,
    .npresets = sizeof FOLD_PRESETS / sizeof FOLD_PRESETS[0],
    .knob = {P_E0, P_E1, P_E2, P_REL},                        /* HOME's KNOB 1..4 */
    .note_on = fold_note_on,
    .render = fold_render,
    .block = fold_block,
};

/* the engine, its id (yours: with a lower-case letter, listed in docs/PLUGIN_IDS.md), its version, FE_NEW: a number
 * of its own from the host, and no reset hook (no state outside the voices) */
FE_PLUGIN(ENG_FOLD, FE_ID('x', 'F', 'L', 'D'), 1, FE_NEW, 0)
