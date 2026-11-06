/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from Mini-Tone (https://github.com/eldontyrellcorp/Mini-Tone, GPL-3.0-only, a Felucca fork; written there by
 * eldontyrellcorp and Stephane Schmitz: the copyright line of the original file is a placeholder). Changed for this tree:
 * the engine number (17: after FMTONE), the parameter descriptors of the operator slots as a plain function (wvt_xdesc:
 * this tree's engine_t, in the Felucca-Engines submodule, has no tdesc hook; params.c calls it) */
/* WAVETONE: two wavetable oscillators with phase distortion, an interaction between them, and a noise generator, in
 * the manner of the WAVETONE machine of Elektron's Digitone II. Felucca's own implementation (no Elektron code or
 * data); the structure follows a description of the machine's three screens, not its manual: what is Elektron's
 * and what this engine had to decide is said below.
 *
 * Oscillators. Each has a wave (a position 0..127 morphing through the eight frames of the table family), a phase
 * distortion, a level and a coarse tuning. The tables (tools/gen_tables.py wavetone_tables) are two families:
 *   PRIM   SIN TRI SQR SAW PULSE-25 PULSE-12 SKEW-TRI HALF-SINE        (shapes)
 *   HARM   a sine, 1+2, a drawbar set 1..4, the odd harmonics, 1/h up to 12, a formant bump, even-heavy, dense 1/sqrt(h)
 * Each frame is stored at six band limits (64, 32, 16, 8, 4, 2 harmonics) and the pitch picks the one that stays
 * below Nyquist: a low note has at most 64 harmonics (to ~4 kHz at C2), a high one 2. The morph crossfades two
 * neighbouring frames; the 16.6 kB of tables are the engine's flash cost.
 * PHASE DISTORTION is the Casio CZ's: the oscillator's phase runs through a piecewise linear warp whose knee moves
 * from the middle (PD 0: no warp, the table as it is) to 5 % of the cycle (PD 127): half of the cycle is then read in
 * 5 % of it. On a square or pulse that is a pulse width; on a sine a resonant-like sweep; on a harmonic frame a
 * bright, digital fuzz. It is not band limited beyond the table's (it aliases at high PD and pitch). The track's
 * ENV / LFO -> SHP adds to both PDs (the matrix's SHP too).
 * Tuning: TUN is semitones (-24..24), OFS cents (-64..63: the range the project store holds, value + 64 in a byte), exact (the table of pitches, then the cents).
 * DRFT: each oscillator's pitch wanders by a smoothed random walk of up to +-16 cents (a new target every ~0.19 s).
 *
 * Interaction (INTR): OFF the two are summed; SYNC osc 1 resets osc 2's phase on every cycle (osc 2's own pitch sets
 * the timbre; the reset carries the sub-sample overshoot over); RING the two (at their levels) are multiplied;
 * FM osc 1 modulates osc 2's phase and is not heard: its LEV is the index (0..2 pi). NOT Elektron's, said plainly: the
 * description named "Offset" among the interactions; what that is was not clear, FM stands in its place.
 *
 * Noise (NTYP): WHT white, PNK pink (Voss-McCartney, 8 rows, ~ -3 dB / octave), RED a leaky integral of white (-6 dB / octave
 * above ~55 Hz), DIGI a 4-bit sample-and-hold of white clocked at 16 x the key (lo-fi, pitched). NLEV the level (squared
 * curve), NCUT a 12 dB low-pass (the track's ENV / LFO -> FLT move it), NDEC the time the noise takes to fall away after
 * a note on (127: it stays).
 * PHAS: RST starts both oscillators at phase 0 on a note from silence; FREE at a different pseudo-random phase each note
 * (not a literal free-running oscillator). A retrigger of a sounding voice keeps every phase (no click) in either.
 *
 * EDIT 1 (OSC 1):  WAV1 PD1 LEV1 TUN1       EDIT 2 (OSC 2):  WAV2 PD2 LEV2 TUN2
 * INTERACT:        INTR TABL OFS1 OFS2      UTIL:            DRFT PHAS
 * NOISE:           NTYP NLEV NCUT NDEC     FILTER:          TYPE CUT RES KEY
 * FILTER is a resonant multimode filter on the whole voice (dsp.c rsvf: LP BP HP, a Q of up to 32 and, from RES ~122, self-
 * oscillation: a sine at the cutoff, which KEY 127 and CUT 44 put on the played note), after the oscillators and the
 * noise; the track's ENV / LFO -> FLT move its CUT. CUT 127, RES 0 and LP leave the voice as it was.
 * The last pages are track parameters in the operator slots P_FM1_ATK .. P_FM4_LEVEL (the ones DIGITAL used and
 * FMTONE shares: wvt_xdesc names them; the pages show for WAVETONE only: page_t.own). A factory preset sets them
 * (wvt_track_loaded).
 *
 * Voices: at most 4 a part (engine_t.poly). A voice's state is in the part's engine memory (wvt_slot_of). */
#define WVT_POLY 4               /* engine_t.poly */

/* the track parameters (operator slots) WAVETONE uses: ids picked so the engine's default is TP's (0, or 127 where
 * the slot's default is 127) */
#define WP_INTR P_FM1_ATK        /* interaction: OFF SYNC RING FM */
#define WP_TABL P_FM1_DEC        /* table family: PRIM HARM */
#define WP_OFS1 P_FM1_REL        /* cents */
#define WP_OFS2 P_FM2_REL
#define WP_DRFT P_FM2_ATK        /* drift */
#define WP_PHAS P_FM2_DEC        /* RST FREE */
#define WP_NTYP P_FM3_ATK        /* noise type */
#define WP_NLEV P_FM3_DEC        /* noise level */
#define WP_NCUT P_FM1_SUS        /* noise low-pass (default 127: open) */
#define WP_NDEC P_FM2_SUS        /* noise decay (default 127: stays) */
#define WP_FCUT P_FM3_SUS        /* the voice filter (dsp.c rsvf), after the oscillators and the noise: CUT (default 127: open) */
#define WP_FRES P_FM3_REL        /*   RES (default 0; from ~118 it rings on its own) */
#define WP_FKEY P_FM3_LEVEL      /*   KEY, the key tracking (default 127: 100 %); P_FTYPE is its TYPE (LP BP HP). CUT 127, RES 0 and */
                                 /*   LP: no filter (a sound of before 1.x is untouched) */

static const char *const N_WVT_INTR[] = {"OFF", "SYNC", "RING", "FM"};
static const char *const N_WVT_TABL[] = {"PRIM", "HARM"};
static const char *const N_WVT_PHAS[] = {"RST", "FREE"};
static const char *const N_WVT_NTYP[] = {"WHT", "PNK", "RED", "DIGI"};

typedef struct {
    uint32_t ph[2];              /* the oscillators' phases */
    int32_t dnz[2], dtg[2];      /* drift: the value and its target, Q15 (+-1.0) */
    int32_t pk[8], psum;         /* pink noise: the rows and their sum */
    int32_t red, hold;           /* red noise's integral, the digital noise's held sample */
    uint32_t nph, pcnt;          /* the digital noise's clock, the pink noise's counter */
    int32_t nenv, ng;            /* the noise's decay envelope (Q24), its gain at the end of the last block (Q15) */
    int32_t ic1, ic2;            /* the noise filter */
    int32_t fi1, fi2;            /* the voice filter (Q8) */
    int32_t dx1, dy1;            /* the DC blocker: the last input, the output as Q8 */
    int32_t rng;
    uint32_t cnt;                /* notes started: the FREE phase's seed */
    uint16_t dcnt, rsv;
} wvt_slot_t;
ENG_MEM_FITS("WAVETONE", sizeof(wvt_slot_t) * WVT_POLY);

/* the operator slots as WAVETONE names them (see the top). Ranges and defaults as TP's where the label is not bipolar */
static const param_desc_t WVT_TD[P_FM4_LEVEL - P_FM1_ATK + 1] = {
    [WP_INTR - P_FM1_ATK] = {"INTR", F_ENUM, 0, 3, 0, N_WVT_INTR, 0},
    [WP_TABL - P_FM1_ATK] = {"TABL", F_ENUM, 0, 1, 0, N_WVT_TABL, 0},
    [WP_OFS1 - P_FM1_ATK] = {"OFS1", F_INT, -64, 63, 0, 0, "ct"},
    [WP_OFS2 - P_FM1_ATK] = {"OFS2", F_INT, -64, 63, 0, 0, "ct"},
    [WP_DRFT - P_FM1_ATK] = {"DRFT", F_PCT, 0, 127, 0, 0, 0},
    [WP_PHAS - P_FM1_ATK] = {"PHAS", F_ENUM, 0, 1, 0, N_WVT_PHAS, 0},
    [WP_NTYP - P_FM1_ATK] = {"NTYP", F_ENUM, 0, 3, 0, N_WVT_NTYP, 0},
    [WP_NLEV - P_FM1_ATK] = {"NLEV", F_PCT, 0, 127, 0, 0, 0},
    [WP_NCUT - P_FM1_ATK] = {"NCUT", F_CUTOFF, 0, 127, 127, 0, 0},
    [WP_NDEC - P_FM1_ATK] = {"NDEC", F_TIME, 0, 127, 127, 0, 0},
    [WP_FCUT - P_FM1_ATK] = {"CUT", F_CUTOFF, 0, 127, 127, 0, 0},
    [WP_FRES - P_FM1_ATK] = {"RES", F_PCT, 0, 127, 0, 0, 0},
    [WP_FKEY - P_FM1_ATK] = {"KEY", F_PCT, 0, 127, 127, 0, 0},
};
/* WAVETONE's name for the FM operator slot id (params.c eng_xdesc), 0 = not one of its own */
static const param_desc_t *wvt_xdesc(uint32_t id)
{
    const param_desc_t *d = &WVT_TD[id - P_FM1_ATK];
    return d->label ? d : 0;
}

static wvt_slot_t *wvt_slot_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < WVT_POLY ? (wvt_slot_t *)ENG_MEM(t - trk) + i : 0;
}

static void wvt_note_on(track_t *t, voice_t *v)
{
    wvt_slot_t *S = wvt_slot_of(t, v);
    uint32_t i, vi = (uint32_t)(v - t->v);
    if (!S)
        return;
    S->nenv = 1 << 24;                                    /* the noise's decay restarts at every note */
    if (v->env || v->env_out)                             /* a retrigger of a sounding voice: everything runs on */
        return;
    S->cnt++;
    S->rng = (int32_t)(((v->note + 1u) * 0x9E3779B9u ^ (vi + 1u) * 0x85EBCA6Bu ^ S->cnt * 0xC2B2AE35u) | 1u);
    if (t->p[WP_PHAS]) {                                  /* FREE: a different phase each note */
        S->ph[0] = (uint32_t)noise32(&S->rng);
        S->ph[1] = (uint32_t)noise32(&S->rng);
    } else {
        S->ph[0] = S->ph[1] = 0;
    }
    for (i = 0; i < 2u; i++)
        S->dnz[i] = S->dtg[i] = 0;
    for (i = 0; i < 8u; i++)
        S->pk[i] = 0;
    S->psum = S->red = S->hold = 0;
    S->nph = S->pcnt = 0;
    S->ng = 0;
    S->ic1 = S->ic2 = 0;
    S->fi1 = 0;
    S->fi2 = rsvf_seed(t->p[WP_FRES]);
    S->dx1 = S->dy1 = 0;
    S->dcnt = (uint16_t)(vi * 61u);
}

/* ------------------------------------------------------------------------------------------ the oscillator --- */
typedef struct {
    const int16_t *a, *b;        /* the frame and the next (b unused when ff is 0) at the oscillator's band limit */
    uint32_t lg, m16, k1, k2;    /* the table's log2 length; the warp's knee and its two slopes (k1 0: no warp) */
    int32_t ff;                  /* the crossfade to b, 0..255 */
} wvt_o_t;

/* the band limit (0 = the most harmonics) for pitch p16 (1/16 semitone) */
static uint32_t wvt_level(int32_t p16)
{
    uint32_t k = 0;
    while (k < WT_NLEV - 1u && p16 >= (int32_t)WT_LEVEL_P16[k])
        k++;
    return k;
}

static void wvt_setup(wvt_o_t *o, uint32_t fam, int32_t wave, int32_t pd, int32_t p16)
{
    uint32_t lv = wvt_level(p16), lg = WT_LEN_LOG2[lv], x, fi, flen = (1u << lg) + 1u;
    wave = clamp(wave, 0, 127);
    x = ((uint32_t)wave * 3613u) >> 8;                    /* 0 .. 1792: eight frames 256 apart */
    fi = x >> 8;
    o->a = WT_TAB[lv] + (fam * WT_NFRAME + fi) * flen;
    o->b = o->a + flen;
    o->lg = lg;
    o->ff = (int32_t)(x & 255u);
    pd = clamp(pd, 0, 127);
    if (pd) {                                             /* the knee from 0.5 to 0.05 of the cycle */
        o->m16 = 32768u - (uint32_t)pd * 232u;
        o->k1 = 0x80000000u / o->m16;
        o->k2 = 0x80000000u / (65536u - o->m16);
    } else {
        o->m16 = o->k1 = o->k2 = 0;
    }
}

static inline int32_t wvt_tab(const int16_t *t, uint32_t lg, uint32_t ph)
{
    uint32_t i = ph >> (32u - lg);
    int32_t a = t[i], b = t[i + 1u];
    return a + (((b - a) * (int32_t)((ph << lg) >> 17)) >> 15);
}

static inline int32_t wvt_osc(const wvt_o_t *o, uint32_t ph)
{
    int32_t x, y;
    if (o->k1) {                                          /* the CZ phase warp: knee at m, half of the cycle each side */
        uint32_t q = ph >> 16;
        ph = q < o->m16 ? q * o->k1 : 0x80000000u + (q - o->m16) * o->k2;
    }
    x = wvt_tab(o->a, o->lg, ph);
    if (o->ff) {
        y = wvt_tab(o->b, o->lg, ph);
        x += ((y - x) * o->ff) >> 8;
    }
    return x;
}

/* ---------------------------------------------------------------------------------------------- the noise --- */
#define WVT_RED_SHIFT 3
static inline int32_t wvt_noise(wvt_slot_t *S, uint32_t ty, uint32_t clk)
{
    int32_t w = (int32_t)noise32(&S->rng) >> 17;          /* +-16384: white */
    switch (ty) {
    case 1: {                                             /* pink: the row that changes is the counter's lowest set bit */
        uint32_t c = ++S->pcnt, k = 0;
        while (k < 7u && !(c & (1u << k)))
            k++;
        S->psum -= S->pk[k];
        S->pk[k] = w >> 2;
        S->psum += S->pk[k];
        return S->psum + (w >> 2);
    }
    case 2:                                               /* red: a leaky integral */
        S->red += (w >> WVT_RED_SHIFT) - (S->red >> 7);
        return clamp(S->red, -32767, 32767);
    case 3: {                                             /* digital: a 4-bit sample-and-hold */
        uint32_t n = S->nph + clk;
        if (n < S->nph)
            S->hold = (w >> 11) << 11;
        S->nph = n;
        return S->hold;
    }
    default:
        return w;
    }
}

/* ---------------------------------------------------------------------------------------------- the voice --- */
#define WVT_OUT 32000            /* the sum's gain, Q15 */
static void wvt_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    wvt_slot_t *S = wvt_slot_of(t, v);
    wvt_o_t o1, o2;
    uint32_t fam = (uint32_t)p[WP_TABL] & 1u, intr = (uint32_t)p[WP_INTR] & 3u, nty = (uint32_t)p[WP_NTYP] & 3u, i, k;
    int32_t shp = (m->shape - (64 << 8)) >> 8, drf = p[WP_DRFT], nlev = clamp(p[WP_NLEV], 0, 127), ndec = clamp(p[WP_NDEC], 0, 127);
    int32_t g1 = clamp(p[P_E2], 0, 127) * 258, g2 = clamp(p[P_E6], 0, 127) * 258, dc1 = 0, dc2 = 0;
    int32_t pt1 = m->pitch16 + clamp(p[P_E3], -24, 24) * 16, pt2 = m->pitch16 + clamp(p[P_E7], -24, 24) * 16;
    uint32_t inc1, inc2, ph1, ph2, clk = 0, r_q8 = 0;
    int32_t ng0, ng1, dng, dx1, dy1, ic1, ic2, fi1, fi2;
    int32_t fcut = clamp(p[WP_FCUT], 0, 127), fres = clamp(p[WP_FRES], 0, 127);
    uint32_t fty = (uint32_t)p[P_FTYPE] % 3u, fon = fcut < 127 || fres > 0 || fty;
    tsvf_t flt;
    rsvf_t rf;
    if (!S)
        return;
    if (n > CTL)
        n = CTL;
    /* drift: a new random target every 256 blocks, the value slides to it */
    if (drf) {
        if ((S->dcnt & 255u) == 0u)
            for (k = 0; k < 2u; k++)
                S->dtg[k] = ((int32_t)(noise32(&S->rng) >> 16)) - 32768;
        for (k = 0; k < 2u; k++)
            S->dnz[k] += (S->dtg[k] - S->dnz[k]) >> 8;
        dc1 = (S->dnz[0] * drf) >> 18;
        dc2 = (S->dnz[1] * drf) >> 18;
    }
    S->dcnt++;
    inc1 = cents_inc(pt1, p[WP_OFS1] + dc1, m->fine);
    inc2 = cents_inc(pt2, p[WP_OFS2] + dc2, m->fine);
    wvt_setup(&o1, fam, p[P_E0], p[P_E1] + shp, pt1);
    wvt_setup(&o2, fam, p[P_E4], p[P_E5] + shp, pt2);
    if (intr == 1u)                                       /* SYNC: osc 2's inc over osc 1's, Q8 (the reset's carry; a 32-bit divide: the target has no 64-bit one) */
        r_q8 = inc2 / ((inc1 >> 8) | 1u);
    /* the noise */
    ng0 = S->ng;
    if (nlev) {
        int32_t gn = (nlev * nlev) << 2, e;
        if (ndec < 127)
            S->nenv -= mulq16(S->nenv, ENV_EXP[ndec]);
        else
            S->nenv = 1 << 24;
        e = S->nenv >> 9;                                 /* Q15 */
        ng1 = (gn * e) >> 15;
        clk = inc1 > 0x07FFFFFFu ? 0x7FFFFFFFu : inc1 << 4;
        tsvf_coef(&flt, clamp((p[WP_NCUT] << 8) + m->cutoff, 0, 127 << 8), 20);
    } else {
        ng1 = 0;
        S->nenv = 1 << 24;
        flt.a1 = flt.a2 = flt.a3 = 0;
    }
    dng = (ng1 - ng0) >> CTL_LOG2;
    ph1 = S->ph[0];
    ph2 = S->ph[1];
    ic1 = S->ic1;
    ic2 = S->ic2;
    fi1 = S->fi1;
    fi2 = S->fi2;
    dx1 = S->dx1;
    dy1 = S->dy1;
    if (fon)
        rsvf_coef(&rf, rsvf_key(fcut, m->cutoff, clamp(p[WP_FKEY], 0, 127), m->pitch16), fres);
    for (i = 0; i < n; i++) {
        int32_t s1, s2, acc, nx = 0;
        uint32_t n1 = ph1 + inc1;
        s1 = wvt_osc(&o1, ph1);
        switch (intr) {
        case 1:                                           /* SYNC */
            s2 = wvt_osc(&o2, ph2);
            acc = mulq15(s1, g1) + mulq15(s2, g2);
            ph2 += inc2;
            if (n1 < ph1)
                ph2 = ((n1 >> 16) * r_q8) << 8;
            break;
        case 2:                                           /* RING */
            s2 = wvt_osc(&o2, ph2);
            acc = mulq15(mulq15(s1, g1), mulq15(s2, g2)) << 2;
            ph2 += inc2;
            break;
        case 3:                                           /* FM: osc 1 modulates osc 2's phase (index g1: 0 .. 2 pi) */
            s2 = wvt_osc(&o2, ph2 + ((uint32_t)(s1 * g1) << 2));
            acc = mulq15(s2, g2);
            ph2 += inc2;
            break;
        default:
            s2 = wvt_osc(&o2, ph2);
            acc = mulq15(s1, g1) + mulq15(s2, g2);
            ph2 += inc2;
            break;
        }
        ph1 = n1;
        if (nlev) {                                       /* the noise, low-passed (tsvf_lp, rounded: no DC at low cutoffs) */
            int32_t x = wvt_noise(S, nty, clk), v3, v1, v2;
            ng0 += dng;
            v3 = (x >> 1) - ic2;
            v1 = (flt.a1 * ic1 + flt.a2 * v3 + 4096) >> 13;
            v2 = ic2 + ((flt.a2 * ic1 + flt.a3 * v3 + 4096) >> 13);
            ic1 = clamp(2 * v1 - ic1, -150000, 150000);
            ic2 = clamp(2 * v2 - ic2, -150000, 150000);
            nx = mulq15(v2 << 1, ng0);
        }
        acc = mulq15(acc + nx, WVT_OUT);
        {   /* DC blocker (~15 Hz, the state with 8 fractional bits): a warped table has a mean, the red noise wanders */
            dy1 = mulq16(dy1, 65396u) + ((acc - dx1) << 8);
            dx1 = acc;
            acc = (dy1 + 128) >> 8;
        }
        if (fon)                                          /* the voice filter: the whole voice, noise too */
            acc = soft_knee(rsvf_run(&rf, acc >> 1, &fi1, &fi2, fty), 16000) << 1;
        out[i] += voice_amp(acc, m, i);
    }
    S->ph[0] = ph1;
    S->ph[1] = ph2;
    S->ng = ng1;
    S->ic1 = ic1;
    S->ic2 = ic2;
    S->fi1 = fi1;
    S->fi2 = fi2;
    S->dx1 = dx1;
    S->dy1 = dy1;
}

/* ---------------------------------------------------------------------------------------------- presets --- */
typedef struct {
    int8_t intr, tabl, ofs1, ofs2, drft, phas, ntyp, nlev, ncut, ndec;
    int8_t fres, fnotc, fnotk, ftype;   /* the filter: RES, 127 - CUT, 127 - KEY, TYPE (zeros: no filter, 100 % key, LP) */
} wvt_x_t;
/* wave positions: frame i is at round(i x 127 / 7): 0 18 36 54 73 91 109 127 */
/* name, {WAV1, PD1, LEV1, TUN1, WAV2, PD2, LEV2, TUN2}, {A D S R}, fenv, mono */
static const preset_t WVT_PRESETS[] = {
    {"SYNC LEAD", {54, 0, 100, 0, 54, 0, 90, 7}, {2, 70, 100, 40}, 0, 1, FX(0, 15, 40, 30), PAT(4)},
    {"PWM BASS", {36, 40, 100, 0, 36, 40, 70, -12}, {0, 60, 100, 25}, 0, 1, FX(10, 0, 10, 10), PAT(2)},
    {"RING BELL", {0, 0, 100, 0, 0, 0, 100, 7}, {0, 100, 0, 70}, 0, 0, FX(0, 15, 35, 60), PAT(7)},
    {"HARM ORGAN", {18, 0, 90, 0, 36, 0, 60, 12}, {0, 0, 127, 30}, 0, 0, FX(0, 20, 20, 30), PAT(6)},
    {"DRIFT PAD", {54, 12, 80, 0, 18, 0, 80, 0}, {80, 90, 110, 95}, 0, 0, FX(0, 55, 25, 70), PAT(5)},
    {"WIND", {0, 0, 0, 0, 0, 0, 0, 0}, {90, 90, 110, 100}, 30, 0, FX(0, 40, 30, 60), PAT(5)},
    {"NOISE HIT", {0, 0, 127, -12, 0, 0, 0, 0}, {0, 62, 0, 40}, 0, 0, FX(0, 20, 30, 40), PAT(2)},
    /* the resonant filter on the voice: a lead through a Q of 17; a sine from the filter alone, tuned to the key (CUT 44 on C4
     * at KEY 127) with a quiet oscillator under it; a band-pass wah on the PWM bass (the band-pass is constant skirt: its gain rises with Q) */
    {"RESO LEAD", {54, 20, 100, 0, 54, 0, 80, 12}, {0, 70, 100, 40}, 40, 1, FX(0, 15, 40, 30), PAT(4)},
    {"SELF SINE", {0, 0, 40, 0, 0, 0, 0, 0}, {0, 70, 100, 40}, 0, 1, FX(0, 10, 35, 35), PAT(4)},
    {"WAH BASS", {36, 40, 100, 0, 36, 40, 70, -12}, {0, 78, 30, 25}, 55, 1, FX(10, 0, 10, 10), PAT(2)},
#include "presets_wavetone.inc"          /* (tools/gen_presets.py: more factory presets) */
};
static const wvt_x_t WVT_X[NELEM(WVT_PRESETS)] = {
    {1, 0, 0, 0, 10, 0, 0, 0, 127, 127},        /* SYNC LEAD */
    {0, 0, 0, 8, 5, 0, 0, 0, 127, 127},         /* PWM BASS */
    {2, 0, 0, 0, 0, 0, 0, 0, 127, 127},         /* RING BELL */
    {0, 1, 0, 0, 0, 0, 0, 0, 127, 127},         /* HARM ORGAN */
    {0, 0, -9, 9, 70, 1, 1, 25, 90, 127},       /* DRIFT PAD */
    {0, 0, 0, 0, 40, 1, 2, 127, 70, 127},       /* WIND */
    {0, 0, 0, 0, 0, 0, 0, 110, 100, 80},        /* NOISE HIT */
    {0, 0, 0, 0, 10, 0, 0, 0, 127, 127, 95, 72, 27, 0},    /* RESO LEAD: CUT 55, KEY 100 % */
    {0, 0, 0, 0, 0, 0, 0, 0, 127, 127, 127, 83, 0, 0},     /* SELF SINE: CUT 44, KEY 127, RES 127 */
    {0, 0, 0, 8, 5, 0, 0, 0, 127, 127, 90, 91, 0, 1},      /* WAH BASS: BP, CUT 36, KEY 127, RES 90 (Q 8) */
#include "presets_wavetone_x.inc"          /* (tools/gen_presets.py: more factory presets) */
};

static const engine_t ENG_WAVETONE = {
    .name = "WAVETONE",
    .page_title = {"OSC 1", "OSC 2"},
    .edit = {
        {"WAV1", F_PCT, 0, 127, 0, 0, 0},
        {"PD1", F_PCT, 0, 127, 0, 0, 0},
        {"LEV1", F_PCT, 0, 127, 100, 0, 0},
        {"TUN1", F_INT, -24, 24, 0, 0, "st"},
        {"WAV2", F_PCT, 0, 127, 0, 0, 0},
        {"PD2", F_PCT, 0, 127, 0, 0, 0},
        {"LEV2", F_PCT, 0, 127, 0, 0, 0},
        {"TUN2", F_INT, -24, 24, 0, 0, "st"},
    },
    .presets = WVT_PRESETS,
    .npresets = NELEM(WVT_PRESETS),
    .note_on = wvt_note_on,
    .render = wvt_render,
    .knob = {P_E0, P_E1, P_E4, P_E5},
    .poly = WVT_POLY,
    .mem = sizeof(wvt_slot_t) * WVT_POLY,
};

/* the preset a track just loaded (ui.c apply_preset_to, tests/hostsim.c): its operator slots (the common part of a sound
 * load has set them to TP's defaults) */
static void wvt_track_loaded(track_t *t)
{
    const wvt_x_t *x;
    if (t->eng_req != ENGI_WAVETONE)
        return;
    x = &WVT_X[(uint32_t)t->preset % NELEM(WVT_X)];
    t->p[WP_INTR] = x->intr;
    t->p[WP_TABL] = x->tabl;
    t->p[WP_OFS1] = x->ofs1;
    t->p[WP_OFS2] = x->ofs2;
    t->p[WP_DRFT] = x->drft;
    t->p[WP_PHAS] = x->phas;
    t->p[WP_NTYP] = x->ntyp;
    t->p[WP_NLEV] = x->nlev;
    t->p[WP_NCUT] = x->ncut;
    t->p[WP_NDEC] = x->ndec;
    t->p[WP_FRES] = x->fres;
    t->p[WP_FCUT] = 127 - x->fnotc;
    t->p[WP_FKEY] = 127 - x->fnotk;
    t->p[P_FTYPE] = x->ftype;
}
