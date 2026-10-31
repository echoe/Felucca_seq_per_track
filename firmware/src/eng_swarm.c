/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from Mini-Tone (https://github.com/eldontyrellcorp/Mini-Tone, GPL-3.0-only, a Felucca fork; written there by
 * eldontyrellcorp and Stephane Schmitz: the copyright line of the original file is a placeholder). Changed for this tree:
 * the engine number (15: after SHDRUM), the parameter descriptors of the operator slots as plain functions (swm_xdesc: this tree's
 * engine_t, in the Felucca-Engines submodule, has no tdesc hook; params.c calls it) */
/* SWARM: a super-saw cluster in the manner of the Swarmer machine of Elektron's Digitone II: one main oscillator
 * and six detuned swarm oscillators, the main and the swarm able to have different waveshapes, the main
 * oscillator able to sit one or two octaves down, and two sources of movement for the swarm: ANIMATION (slow,
 * free-running drift of each swarm oscillator's detune) and NOISE modulation (a random wander of their pitch).
 * Felucca's own implementation (no Elektron code or data); the structure is after what Elektron's manual and
 * reviews describe (seven oscillators; SWRM wave, DETUNE and MIX of the swarm against the main; animation; noise
 * modulation). What the manual names but this engine does not have: the noise's own amplitude envelope (its
 * attack and hold, the Swarmer's third page), a continuous wave morph.
 *
 * EDIT 1 (OSC):    WAVE  main waveform   SAW SQR TRI SIN
 *                  SWRM  swarm waveform  SAW SQR TRI SIN
 *                  OCT   main octave     0, -1, -2
 *                  DTN   swarm detune    0 = all seven in unison, 127 = the outer pair about +-1 semitone
 * EDIT 2 (SWARM):  MIX   the swarm's level against the main: 0 = main only, 127 = swarm full (the main then at
 *                        half level, as a super-saw's centre voice)
 *                  ANIM  animation depth: each swarm oscillator's detune moves by up to ~+-38 cents at its own
 *                        slow rate (0.17 .. 0.71 Hz)
 *                  NOIS  noise modulation: smoothed random pitch wander of each swarm oscillator, about 25 cents rms at 127 (peaks ~60)
 *                  CUT   the filter's cutoff (dsp.c rsvf, below): the track's ENV / LFO -> FLT move it
 * FILTER:          TYPE  LP (as it was), BP, HP      CUT   (EDIT 2's)      RES   0 = ~Q 0.5, up to Q 32 and, from ~118,
 *                  self-oscillation (a sine at the cutoff over the swarm)      KEY   the key tracking: 0 = as it was
 *                  (a fifth of the way: the cutoff follows the key at 21 %), 127 = one to one (CUT 44 rings on C4)
 *                  RES and KEY are track parameters in the operator slots P_FM1_REL and P_FM1_ATK (swm_xdesc names
 *                  them, the FILTER page shows for SWARM only: page_t.own)
 * The track's ENV / LFO -> SHP (and the matrix's SHP) do nothing here: there is no pulse width.
 *
 * The detune curve is squared (fine control below the middle) and the six offsets are the ratios of the well
 * known super-saw (slightly asymmetric: the beating never settles into a pattern).
 *
 * Voices: at most 4 per part (engine_t.poly): 28 oscillators at most. Each voice's swarm state lives in the
 * part's engine memory (engines.c eng_mem; swm_slot_of): the six swarm phases, the six animation phases, the six
 * wander values, the filter. The main phase is v->ph[0] (a retrigger keeps it, as every engine). A note from
 * silence starts from fixed phases (it sounds the same each time), a retrigger of a sounding voice keeps the swarm
 * and the filter running (no click).
 *
 * Band limiting: SAW and SQR use the polyBLEP of dsp.c (osc_saw, osc_pulse); TRI and SIN have none to need. */
#define SWM_POLY 4               /* engine_t.poly */
#define SWM_N 6                  /* swarm oscillators */
static const char *const N_SWM_WAVE[] = {"SAW", "SQR", "TRI", "SIN"};
static const char *const N_SWM_OCT[] = {"0", "-1", "-2"};

typedef struct {
    uint32_t ph[SWM_N];          /* swarm oscillator phases */
    uint32_t lph[SWM_N];         /* animation phases */
    int32_t nz[SWM_N];           /* wander values, Q10 */
    int32_t ic1, ic2;            /* the filter */
    int32_t rng;
    int32_t rsv;
} swm_slot_t;
ENG_MEM_FITS("SWARM", sizeof(swm_slot_t) * SWM_POLY);

/* detune offsets at DTN 127, in 1/4096 of the frequency: the super-saw's ratios (-0.110 -0.0629 -0.0195 +0.0199
 * +0.0622 +0.1075) scaled so the outer ones are ~1 semitone (243 = 5.9 %) */
static const int16_t SWM_OFF[SWM_N] = {-243, -139, -43, 44, 137, 237};
/* animation: each swarm oscillator's own LFO, phase increment a sample (rate * 2^32 / 44100): 0.17 .. 0.71 Hz */
static const uint32_t SWM_LRATE[SWM_N] = {16556u, 22400u, 30200u, 39900u, 51600u, 69100u};
/* start phases of the swarm and of the animation: spread, never equal (equal saws stack into one) */
static const uint32_t SWM_START[SWM_N] = {0x1B873593u, 0x9E3779B9u, 0x517CC1B7u, 0xC2B2AE35u, 0x7F4A7C15u, 0xE6546B64u};
#define SWM_ANIM_MAX 90          /* 1/4096 of the frequency at ANIM 127 (~38 cents) */
#define SWM_NOIS_MAX 360         /* NOIS 127: ~25 cents rms, peaks ~60 */
#define SWP_RES P_FM1_REL        /* the filter's resonance (default 0) */
#define SWP_KEY P_FM1_ATK        /* its key tracking, on top of the fifth it always had (default 0) */
#define SWM_KEY0 27              /* the key tracking before RES and KEY: 4 of 18.7 (1/256 of a knob step a 1/16 semitone) */

static const param_desc_t SWM_TD[P_FM4_LEVEL - P_FM1_ATK + 1] = {
    [SWP_KEY - P_FM1_ATK] = {"KEY", F_PCT, 0, 127, 0, 0, 0},
    [SWP_RES - P_FM1_ATK] = {"RES", F_PCT, 0, 127, 0, 0, 0},
};
/* SWARM's name for the FM operator slot id (params.c track_desc / param_desc_of), 0 = not one of its own */
static const param_desc_t *swm_xdesc(uint32_t id)
{
    const param_desc_t *d = &SWM_TD[id - P_FM1_ATK];
    return d->label ? d : 0;
}

static swm_slot_t *swm_slot_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < SWM_POLY ? (swm_slot_t *)ENG_MEM(t - trk) + i : 0;
}

static void swm_note_on(track_t *t, voice_t *v)
{
    swm_slot_t *S = swm_slot_of(t, v);
    uint32_t i, vi = (uint32_t)(v - t->v);
    if (!S)
        return;
    if (v->env || v->env_out)                         /* a retrigger of a sounding voice: swarm and filter run on */
        return;
    for (i = 0; i < SWM_N; i++) {
        S->ph[i] = SWM_START[i] + vi * 0x3C6EF372u;
        S->lph[i] = SWM_START[(i + 3u) % SWM_N] + vi * 0x1F83D9ABu;
        S->nz[i] = 0;
    }
    S->ic1 = 0;
    S->ic2 = rsvf_seed(t->p[SWP_RES]);
    S->rng = (int32_t)(((v->note + 1u) * 0x9E3779B9u ^ (uint32_t)v->fine * 0x85EBCA6Bu) | 1u);
    v->ph[0] = 0;
}

/* one oscillator over the block: mix[i] += its sample at level g (Q15). One loop per wave: the wave is not looked at
 * per sample (trio_pass) */
static void swm_pass(uint32_t w, uint32_t *php, uint32_t inc, int32_t g, int32_t *mix, uint32_t n)
{
    uint32_t ph = *php, i;
    switch (w) {
    case 1:
        for (i = 0; i < n; i++) {
            mix[i] += mulq15(osc_pulse(ph, inc, 0x80000000u), g);
            ph += inc;
        }
        break;
    case 2:
        for (i = 0; i < n; i++) {
            mix[i] += mulq15(osc_tri(ph), g);
            ph += inc;
        }
        break;
    case 3:
        for (i = 0; i < n; i++) {
            mix[i] += mulq15(osc_sine(ph), g);
            ph += inc;
        }
        break;
    default:
        for (i = 0; i < n; i++) {
            mix[i] += mulq15(osc_saw(ph, inc), g);
            ph += inc;
        }
        break;
    }
    *php = ph;
}

static void swm_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    swm_slot_t *S = swm_slot_of(t, v);
    uint32_t wm = (uint32_t)p[P_E0] & 3u, ws = (uint32_t)p[P_E1] & 3u, oct = (uint32_t)p[P_E2], i, k;
    int32_t dtn = p[P_E3], sw = p[P_E4] * 258, anim = p[P_E5], nmod = p[P_E6];
    int32_t d, g0, gs, mix[CTL];
    uint32_t ph0 = v->ph[0], inc0, inc, fty = (uint32_t)p[P_FTYPE] % 3u;
    rsvf_t flt;
    int32_t ic1, ic2, rng;
    if (!S)
        return;
    if (n > CTL)
        n = CTL;
    if (oct > 2u)
        oct = 2u;
    d = (dtn * dtn) / 127;                                  /* the squared detune curve, 0..127 */
    g0 = ((32767 - (sw >> 1)) * 27500) >> 15;               /* main: 0.84 .. 0.42 */
    gs = (sw * 9900) >> 15;                                 /* each swarm oscillator: up to 0.30 */
    inc = m->inc;                                           /* the swarm plays the note, the main may be below it */
    inc0 = inc >> oct;
    rng = S->rng;
    for (i = 0; i < n; i++)
        mix[i] = 0;
    swm_pass(wm, &ph0, inc0, g0, mix, n);
    if (gs) {                                               /* MIX 0: the swarm is silent, its state stands still */
        uint32_t inc_fine = inc >> 12;
        for (k = 0; k < SWM_N; k++) {
            int32_t f = (SWM_OFF[k] * d) / 127, a, w;
            if (anim) {                                     /* the animation: a slow sine on this oscillator's detune */
                S->lph[k] += SWM_LRATE[k] * n;
                a = (sine_i(S->lph[k]) * anim) >> 12;       /* +-1016 at ANIM 127 */
                f += (a * SWM_ANIM_MAX) >> 10;
            }
            if (nmod) {                                     /* noise modulation: a smoothed random wander */
                int32_t r = (int32_t)(noise32(&rng) >> 22) - 512;   /* +-512 */
                S->nz[k] += (r - S->nz[k]) >> 2;
                f += ((S->nz[k] * nmod) >> 7) * SWM_NOIS_MAX >> 9;  /* +-SWM_NOIS_MAX at NOIS 127 */
            }
            w = f;
            swm_pass(ws, &S->ph[k], inc + (uint32_t)((int32_t)inc_fine * w), gs, mix, n);
        }
    }
    /* the filter (dsp.c rsvf): key-tracked a fifth of the way as it always was, more with KEY */
    rsvf_coef(&flt, rsvf_key(p[P_E7], m->cutoff, SWM_KEY0 + (clamp(p[SWP_KEY], 0, 127) * (127 - SWM_KEY0) >> 7), v->pitch16),
              clamp(p[SWP_RES], 0, 127));
    ic1 = S->ic1;
    ic2 = S->ic2;
    {   /* SWARM's output stage is twice the others' (a cluster of saws is quiet): the ring would be 6 dB over a note, so from RES 100
         * the output falls too, to 0.63 at RES 127 (the input already falls to 0.4: dsp.c rsvf_coef) */
        int32_t og = 4096 - (((4096 - flt.ig) * 5) >> 3);
        for (i = 0; i < n; i++) {
            int32_t y = soft_knee(rsvf_run(&flt, mix[i] >> 1, &ic1, &ic2, fty), 16000) << 1;
            out[i] += voice_amp((y * og) >> 12, m, i) << 1;
        }
    }
    S->ic1 = ic1;
    S->ic2 = ic2;
    S->rng = rng;
    v->ph[0] = ph0;
}

/* name, {WAVE, SWRM, OCT, DTN, MIX, ANIM, NOIS, CUT}, {A D S R}, fenv, mono */
static const preset_t SWARM_PRESETS[] = {
    {"SUPER LEAD", {0, 0, 0, 45, 90, 30, 0, 100}, {2, 70, 100, 40}, 12, 1, FX(0, 15, 40, 30), PAT(4)},
    {"DRIFT PAD", {0, 0, 0, 55, 100, 70, 20, 80}, {80, 90, 110, 95}, 10, 0, FX(0, 60, 25, 70), PAT(5)},
    {"SUB SWARM", {1, 0, 1, 30, 70, 20, 0, 70}, {0, 60, 100, 30}, 25, 1, FX(10, 10, 15, 15), PAT(2)},
    {"FIZZ ARP", {0, 1, 0, 40, 95, 10, 90, 105}, {0, 62, 0, 40}, 20, 0, FX(0, 20, 40, 30), PAT(13)},
    {"HOOVER", {0, 0, 2, 90, 110, 60, 30, 85}, {6, 85, 110, 60}, 30, 1, FX(25, 30, 20, 40), PAT(4)},
    {"GLASS SWARM", {3, 2, 0, 35, 80, 90, 10, 110}, {40, 90, 100, 90}, 5, 0, FX(0, 40, 30, 65), PAT(5)},
    /* the resonant filter: a Q of 17 swept by the envelope; the filter alone as a sine, tuned to the key (KEY 127, CUT 44 on C4);
     * and a zap, the ring falling as the envelope closes the cutoff */
    {"RESO SWEEP", {0, 0, 0, 40, 90, 30, 0, 35}, {0, 75, 60, 40}, 45, 1, FX(0, 15, 40, 30), PAT(2)},
    {"SINE RING", {3, 3, 0, 0, 0, 0, 0, 44}, {0, 70, 100, 40}, 0, 1, FX(0, 10, 35, 35), PAT(4)},
    {"SELF ZAP", {3, 3, 0, 0, 0, 0, 0, 30}, {0, 75, 0, 30}, 60, 0, FX(0, 0, 40, 35), PAT(0)},
};
/* the filter of each preset (swm_track_loaded): RES, KEY, TYPE; zeros are the defaults */
static const int8_t SWM_X[NELEM(SWARM_PRESETS)][3] = {
    [6] = {95, 64, 0},           /* RESO SWEEP */
    [7] = {127, 127, 0},         /* SINE RING */
    [8] = {127, 0, 0},           /* SELF ZAP */
};

static const engine_t ENG_SWARM = {
    .name = "SWARM",
    .page_title = {"OSC", "SWARM"},
    .edit = {
        {"WAVE", F_ENUM, 0, 3, 0, N_SWM_WAVE, 0},
        {"SWRM", F_ENUM, 0, 3, 0, N_SWM_WAVE, 0},
        {"OCT", F_ENUM, 0, 2, 0, N_SWM_OCT, 0},
        {"DTN", F_PCT, 0, 127, 45, 0, 0},
        {"MIX", F_PCT, 0, 127, 80, 0, 0},
        {"ANIM", F_PCT, 0, 127, 30, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 100, 0, 0},
    },
    .presets = SWARM_PRESETS,
    .npresets = NELEM(SWARM_PRESETS),
    .note_on = swm_note_on,
    .render = swm_render,
    .knob = {P_E3, P_E4, P_E5, P_E7},
    .poly = SWM_POLY,
    .mem = sizeof(swm_slot_t) * SWM_POLY,
};

/* the preset a track just loaded (ui.c apply_preset_to, tests/hostsim.c): its filter's RES, KEY and TYPE */
static void swm_track_loaded(track_t *t)
{
    const int8_t *x;
    if (t->eng_req != ENGI_SWARM)
        return;
    x = SWM_X[(uint32_t)t->preset % NELEM(SWM_X)];
    t->p[SWP_RES] = x[0];
    t->p[SWP_KEY] = x[1];
    t->p[P_FTYPE] = x[2];
}
