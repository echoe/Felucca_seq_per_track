/* SPDX-License-Identifier: GPL-3.0-only */
/* SHDRUM: a two-layer drum synth after the rhythm part of Roland's SH-4D, where each drum is two waveforms, each
 * with its own envelope and level, a pitch envelope, and an effect section at the end of the hit.
 *
 * One SHDRUM track plays one drum sound: a key (or a step's note) is the drum's pitch, so a kick sits low and a
 * tom higher, and the same sound walks up the keyboard as a tuned drum (build a kit from several tracks).
 * A note-on is a hit: the engine is a one-shot (engine_t.oneshot, as DRUM): always POLY, no glide, and the voice ends
 * when the hit has died away, never on key release. LEVEL, PAN, the sends, DIST, INSERT, velocity, LFO DEST PIT / AMP
 * and the matrix's PITCH / AMP / E1..E8 apply; the ADSR, ENV DEST and LFO DEST FLT / SHP do not (the hit is the
 * envelope; the filter has its own).
 *
 * THE HIT, per voice and sample
 *
 *     WAVE A --- ENV A --- LEVEL A --\                           .- DRIVE -- FOLD ... (see EFFECTS)
 *                    ^ XMOD (FM: B -> A's phase; RING: A x B) --- + --- CLICK --- FOLD --- DRIVE --- CRUSH --- FILTER
 *     WAVE B --- ENV B --- LEVEL B --/
 *     both oscillators follow the PITCH ENVELOPE: SWEP semitones above (below, if negative) the key at the hit's
 *     start, falling to the key over SWP T. WAVE B sits TUN B semitones from WAVE A.
 *
 * WAVE A / WAVE B   SIN TRI SAW SQR NOIS. Each wave is an oscillator with an envelope and a level, a layer.
 * XMOD              how the layers meet: MIX adds them. FM: B modulates A's phase (index 0..16 radians over DPTH,
 *                   B's envelope shapes it: a bright attack that dulls as B dies away), A's envelope and level shape
 *                   the carrier, and B is still heard at its own LEVEL B (0 for a pure modulator). RING: DPTH
 *                   crossfades from the two layers added to their product, the ring modulation of A by B (both
 *                   enveloped, so each layer's decay is the metal's decay): the inharmonic, metallic part of a hat, a
 *                   cowbell or a clap with TUN B on a non-octave interval (square x square, +7 semitones).
 * DPTH              the XMOD amount (not used with MIX).
 * A ATK DEC LVL     LAYER A: attack and decay times (the F_TIME table: ATK linear, DEC exponential, the time DEC shows
 *                   is the time to -40 dB) and level. LAYER B the same. The hit ends when both layers have died away.
 * TUN B             wave B's tuning from A, -24..+24 semitones (12 / 19 / 24 are the octave, fifth and double octave
 *                   above: harmonic FM).
 * SWEP / SWP T      the pitch envelope: the depth in semitones (-24..+48) and its decay time.
 *
 * EFFECTS at the end of the hit, in this order, each off at 0:
 * CLIK              a burst of noise (about 3 ms) added at the hit's start: the beater of a kick, the snap of a snare.
 * FOLD              a wave folder: the signal is scaled up to x4 and reflected back at full scale. Adds odd and even harmonics.
 * DRV               a soft clip, x1..x4 into tanh, the level kept.
 * CRSH              bit reduction (up to 12 bits dropped) and a sample hold (down to a quarter of the sample rate).
 * FLT TYP/CUT/RES   an optional 12 dB multimode filter (OFF LP BP HP), the cutoff following wave A's envelope by FENV
 *                   (-64..+63 index steps at full envelope: a falling envelope closes a low-pass over the hit).
 * Beyond these, every track has its own DIST, INSERT, SLICER and the sends; the delay and reverb sit after the engine.
 *
 * Parameters: the eight an engine owns (P_E0..P_E7: WAV A, WAV B, XMOD, DPTH, TUN B, SWEP, SWP T, DRV) are the EDIT 1
 * and EDIT 2 pages and the matrix's E1..E8. The rest are the track's FM operator slots (P_FM1_ATK ..: the ids, stored
 * with every project and user preset, that DIGITAL (retired, FELUCCA_FM4=0) used for its operator envelopes and no
 * engine reads otherwise); SHDRUM gives them its own labels, ranges and defaults (shd_xdesc: params.c track_desc,
 * param_desc_of) and four pages of its own (LAYER A, LAYER B, FX 1, FX 2). This keeps P_COUNT, so projects, user
 * presets and the editor protocol are laid out as before. A factory preset sets all of them (shd_load, ui.c
 * apply_preset_to); on any other engine they are not read.
 *
 * State: all in the voice (voice_t): ph[0] / ph[1] the phases of A and B, ph[2] the crusher's counter; s[0] / s[1] the
 * layers' envelopes (Q24, SHD_AT in bit 30 while the attack runs), s[2] the pitch envelope, s[3] the noise state, s[4] /
 * s[5] the filter, s[6] the click's envelope, s[7] the crusher's held sample. No engine memory (engine_t.mem). */

#include "shd_voice.c"

/* the track's FM operator slots (core.h P_FM1_ATK .. P_FM4_LEVEL, 20 consecutive ids) as SHDRUM names them */
enum {
    SHP_AATK = P_FM1_ATK, SHP_ADEC, SHP_ALVL,                  /* LAYER A */
    SHP_BATK, SHP_BDEC, SHP_BLVL,                              /* LAYER B */
    SHP_CLIK, SHP_FOLD, SHP_CRSH,                              /* FX 1 (with DRV, P_E7) */
    SHP_FTYP, SHP_CUT, SHP_RES, SHP_FENV,                      /* FX 2 */
    SHP_END
};
#define SHP_N (SHP_END - SHP_AATK)
_Static_assert(SHP_END <= P_FM4_LEVEL + 1, "SHDRUM: its extra parameters live in the FM operator slots");

static const char *const N_SHD_WAVE[] = {"SIN", "TRI", "SAW", "SQR", "NOIS"};
static const char *const N_SHD_XMOD[] = {"MIX", "FM", "RING"};
static const char *const N_SHD_FTYP[] = {"OFF", "LP", "BP", "HP"};

/* the descriptors of the slots above, in id order (SHP_AATK + i); range 0..127 as the slots' own (stored values fit) */
static const param_desc_t SHD_X[SHP_N] = {
    {"A ATK", F_TIME, 0, 127, 0, 0, 0},    {"A DEC", F_TIME, 0, 127, 60, 0, 0},   {"A LVL", F_PCT, 0, 127, 127, 0, 0},
    {"B ATK", F_TIME, 0, 127, 0, 0, 0},    {"B DEC", F_TIME, 0, 127, 50, 0, 0},   {"B LVL", F_PCT, 0, 127, 0, 0, 0},
    {"CLIK", F_PCT, 0, 127, 0, 0, 0},      {"FOLD", F_PCT, 0, 127, 0, 0, 0},      {"CRSH", F_PCT, 0, 127, 0, 0, 0},
    {"FLT", F_ENUM, 0, SHF_COUNT - 1, SHF_OFF, N_SHD_FTYP, 0},
    {"CUT", F_CUTOFF, 0, 127, 100, 0, 0},  {"RES", F_PCT, 0, 127, 20, 0, 0},     {"FENV", F_BIPCT, -64, 63, 0, 0, 0},
};
/* the descriptor of id for this engine: SHD_X inside its slots, else 0 */
static const param_desc_t *shd_xdesc(uint32_t id)
{
    return id >= SHP_AATK && id < SHP_AATK + SHP_N ? &SHD_X[id - SHP_AATK] : 0;
}


/* the track's parameters as a sound: the clamped E and slot values */
static void shd_vp_load(shd_vp_t *q, const int16_t *p)
{
    q->wa = (uint8_t)clamp(p[P_E0], 0, SHW_COUNT - 1);
    q->wb = (uint8_t)clamp(p[P_E1], 0, SHW_COUNT - 1);
    q->xm = (uint8_t)clamp(p[P_E2], 0, SHM_COUNT - 1);
    q->depth = (uint8_t)clamp(p[P_E3], 0, 127);
    q->tunb = (int8_t)clamp(p[P_E4], -24, 24);
    q->swep = (int8_t)clamp(p[P_E5], -24, 48);
    q->swpt = (uint8_t)clamp(p[P_E6], 0, 127);
    q->drv = (uint8_t)clamp(p[P_E7], 0, 127);
    q->aatk = (uint8_t)clamp(p[SHP_AATK], 0, 127);
    q->adec = (uint8_t)clamp(p[SHP_ADEC], 0, 127);
    q->alvl = (uint8_t)clamp(p[SHP_ALVL], 0, 127);
    q->batk = (uint8_t)clamp(p[SHP_BATK], 0, 127);
    q->bdec = (uint8_t)clamp(p[SHP_BDEC], 0, 127);
    q->blvl = (uint8_t)clamp(p[SHP_BLVL], 0, 127);
    q->clik = (uint8_t)clamp(p[SHP_CLIK], 0, 127);
    q->fold = (uint8_t)clamp(p[SHP_FOLD], 0, 127);
    q->crsh = (uint8_t)clamp(p[SHP_CRSH], 0, 127);
    q->ftyp = (uint8_t)clamp(p[SHP_FTYP], 0, SHF_COUNT - 1);
    q->cut = (uint8_t)clamp(p[SHP_CUT], 0, 127);
    q->res = (uint8_t)clamp(p[SHP_RES], 0, 127);
    q->fenv = (int8_t)clamp(p[SHP_FENV], -64, 63);
}

static void shd_note_on(track_t *t, voice_t *v)
{
    shd_vp_t q;
    shd_vp_load(&q, t->p);
    shd_start(v->ph, v->s, &q, v->age);
    v->env_out = v->vel * 258;                            /* the hit starts at its level (as DRUM) */
    v->env = 1 << 24;
}

/* the hit is the envelope (as DRUM: drum_amp): the ADSR is held at full so a key-off never ends it; a voice whose
 * layers have died away ends here */
static int32_t shd_amp(track_t *t, voice_t *v, int32_t adsr)
{
    shd_vp_t q;
    (void)adsr;
    if (!v->active)
        return 0;
    shd_vp_load(&q, t->p);
    if (!shd_alive(&q, v->s)) {
        v->active = v->gate = 0;
        v->stage = 0;
        v->env = 0;
        return 0;
    }
    v->env = 1 << 24;
    return 32767;
}

static void shd_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    shd_vp_t q;
    int32_t y[CTL];
    uint32_t i;
    if (n > CTL)
        n = CTL;
    shd_vp_load(&q, t->p);
    shd_run(&q, v->ph, v->s, m->pitch16, y, n);
    for (i = 0; i < n; i++)
        out[i] += voice_amp(soft_knee(y[i], 24000), m, i) << 1;
}

/* ------------------------------------------------------------ presets --- */
/* {WAV A, WAV B, XMOD, DPTH, TUN B, SWEP, SWP T, DRV}, the sends; the slots (SHD_PRESET_X, one row per preset, in
 * SHP_AATK order: A ATK DEC LVL, B ATK DEC LVL, CLIK, FOLD, CRSH, FLT, CUT, RES, FENV). The ENV page (ATK DEC SUS
 * REL) does nothing here: any values. Every kit piece is meant to be played from a low key for the kick, mid for
 * the snare, high for a hat (the key is the pitch); the notes the sound suggests are in the names */
static const preset_t SHD_PRESETS[] = {
    /* A: a sine body with a long sweep; B: a short noisy click on top; both layers mixed */
    {"SH KICK", {SHW_SIN, SHW_SIN, SHM_FM, 20, 0, 30, 36, 20}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 8)},
    /* A: a triangle body that drops; B: noise, a band-passed rattle; CLIK for the stick */
    {"SH SNARE", {SHW_TRI, SHW_NOIS, SHM_MIX, 0, 0, 14, 22, 24}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 22)},
    /* RING of two squares a fifth-and-a-bit apart: the metal, short, high-passed */
    {"SH HAT", {SHW_SQR, SHW_SQR, SHM_RING, 127, 7, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 10)},
    {"SH OPEN HAT", {SHW_SQR, SHW_SQR, SHM_RING, 127, 7, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 24)},
    /* a tuned drum: FM sine with a sweep; play it across the keys */
    {"SH TOM", {SHW_SIN, SHW_SIN, SHM_FM, 36, 12, 12, 30, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 14)},
    /* noise bursts with the filter closing: a clap */
    {"SH CLAP", {SHW_NOIS, SHW_NOIS, SHM_MIX, 0, 0, 0, 0, 16}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 30)},
    /* FM against a non-octave ratio: a bell, a rim, a cowbell */
    {"SH BELL", {SHW_SIN, SHW_SIN, SHM_FM, 70, 19, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 34)},
    /* a laser: a square with a deep, slow sweep */
    {"SH ZAP", {SHW_SQR, SHW_SAW, SHM_RING, 64, 5, 36, 50, 48}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 6, 20)},
};
#define SHD_NPRESETS NELEM(SHD_PRESETS)
static const int8_t SHD_PRESET_X[SHD_NPRESETS][SHP_N] = {
    /*  A: ATK DEC LVL  B: ATK DEC LVL  CLIK FOLD CRSH FLT CUT RES FENV */
    {0, 78, 127, 0, 40, 40, 20, 0, 0, SHF_OFF, 100, 20, 0},     /* KICK */
    {0, 58, 100, 0, 62, 100, 40, 0, 0, SHF_LP, 108, 20, 0},     /* SNARE */
    {0, 40, 100, 0, 36, 70, 0, 0, 0, SHF_HP, 90, 30, 0},        /* HAT */
    {0, 66, 100, 0, 60, 70, 0, 0, 0, SHF_HP, 90, 30, 0},        /* OPEN HAT */
    {0, 76, 100, 0, 40, 50, 10, 0, 0, SHF_OFF, 100, 20, 0},     /* TOM */
    {0, 62, 100, 0, 56, 100, 0, 30, 0, SHF_BP, 80, 50, 0},      /* CLAP */
    {0, 90, 90, 0, 76, 40, 0, 0, 0, SHF_OFF, 100, 20, 0},       /* BELL */
    {0, 70, 100, 0, 60, 80, 0, 0, 0, SHF_OFF, 100, 20, 0},      /* ZAP */
};

/* a factory preset's slots, and the slots' defaults (a clean init): called with the engine's sound loaded
 * (ui.c apply_preset_to, clean_engine_of) */
static void shd_load(track_t *t, uint32_t pi)
{
    uint32_t i;
    for (i = 0; i < SHP_N; i++)
        t->p[SHP_AATK + i] = pi < SHD_NPRESETS ? (int16_t)SHD_PRESET_X[pi][i] : SHD_X[i].def;
}
static void shd_defaults(track_t *t)
{
    uint32_t i;
    for (i = 0; i < SHP_N; i++)
        t->p[SHP_AATK + i] = SHD_X[i].def;
}

static const engine_t ENG_SHDRUM = {
    .name = "SHDRUM",
    .page_title = {"WAVES", "PITCH"},
    .edit = {
        {"WAV A", F_ENUM, 0, SHW_COUNT - 1, SHW_SIN, N_SHD_WAVE, 0},
        {"WAV B", F_ENUM, 0, SHW_COUNT - 1, SHW_SIN, N_SHD_WAVE, 0},
        {"XMOD", F_ENUM, 0, SHM_COUNT - 1, SHM_MIX, N_SHD_XMOD, 0},
        {"DPTH", F_PCT, 0, 127, 0, 0, 0},
        {"TUN B", F_SEMI, -24, 24, 0, 0, 0},
        {"SWEP", F_SEMI, -24, 48, 0, 0, 0},
        {"SWP T", F_TIME, 0, 127, 40, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = SHD_PRESETS,
    .npresets = SHD_NPRESETS,
    .note_on = shd_note_on,
    .render = shd_render,
    .amp = shd_amp,
    .knob = {P_E5, P_E3, SHP_ADEC, SHP_BDEC},      /* HOME: SWEP DPTH A DEC B DEC */
    .poly = 0,
    .sampled = 1,                                  /* a hit restarts its phases (no phases kept over a retrigger) */
    .oneshot = 1,
};
