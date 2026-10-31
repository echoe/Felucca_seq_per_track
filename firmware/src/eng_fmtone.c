/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from Mini-Tone (https://github.com/eldontyrellcorp/Mini-Tone, GPL-3.0-only, a Felucca fork; written there by
 * eldontyrellcorp and Stephane Schmitz: the copyright line of the original file is a placeholder). Changed for this tree:
 * the engine number (16: after SHDRUM), the parameter descriptors of the operator slots as plain functions (fmt_xdesc: this tree's
 * engine_t, in the Felucca-Engines submodule, has no tdesc hook; params.c calls it) */
/* FMTONE: four-operator FM in the manner of the FM TONE machine of Elektron's Digitone II. Four sine operators in
 * the groups Elektron's manual names: C (the carrier), A, and B1 / B2 (the B group, whose ratios are linked); A and
 * B have an envelope each (delay, attack, decay to an end level) and a level, and a key tracking; the track's own
 * ENV is C's amplitude. Felucca's own implementation, no Elektron code or data; the structure is after Elektron's
 * manual (operators C A B1 B2, RATIO C / A / B, HARM, DTUN on A and B2, feedback, the envelopes' ATK DEC END LEV and
 * DEL, key tracking, fine tuning of A / B1 / B2).
 *
 * What is NOT Elektron's, and says so here: the eight algorithms are Felucca's (the routings of the retired DIGITAL
 * engine, mapped onto C A B1 B2: Elektron's diagrams are pictures this was not written from); HARM morphs a wave
 * from a sine towards a bright, saw-like spectrum by self-modulation (Elektron's is an additive harmonic set); the
 * envelopes are the trigged kind (attack, decay to the end level, held: no gated mode, no reset switch); MIX and the
 * ratio tables are this engine's own. Operators are all sine waves here.
 *
 * EDIT 1 (OPS):   ALGO 1..8 (below)   RAT C, RAT A: the frequency ratio of C, of A   RAT B: of B1 (B2 an octave above, at most 16)
 * EDIT 2 (TONE):  HARM -64..63: + the waves of A and B1, - the wave of C      DTN 0..50 ct: A up, B2 down
 *                 FB   B2's feedback     MIX  the level of the other carriers against C (algorithms 5..8)
 * A ENV / B ENV:  ATK DEC END (the level it decays to) LEV (the modulation level the group's operators make)
 * DEL TRK:        DEL A, DEL B (the time before an envelope starts), TRK A, TRK B (key tracking: 127 takes the
 *                 modulation off at C8; notes below C4 are unchanged)
 * FINE:           FTO A, FTO B1, FTO B2: -64..63 cents each
 * Everything past EDIT 2 is a track parameter in the operator slots P_FM1_ATK .. P_FM4_LEVEL (the ones DIGITAL used):
 * fmt_xdesc names them, and a factory preset sets them (fmt_track_loaded).
 *
 * Algorithms (C is always a carrier; a modulator's LEV sets how much it modulates, a carrier's how loud it is):
 *   1  B2 > B1 > A > C                       5  B2 > B1 > out, A > C > out (C + B1)
 *   2  (B1 + B2) > A > C                     6  B2 > C, A, B1, all out (C + A + B1)
 *   3  B1 > A > C, B2 > C                    7  B2 > B1 out, A out, C out (C + A + B1)
 *   4  B2 > B1, (A + B1) > C                 8  all four out
 * (the carriers other than C are scaled by MIX, and the sum is kept at one carrier's level)
 *
 * Each voice's sum passes a DC blocker (about 15 Hz): FM with feedback, or an envelope moving, has a mean.
 *
 * Phase modulation by x of index i is (x * i) * 8 as a 32-bit phase: i in Q8, a full-scale sine at i 127 reaches
 * +-4 pi, wrapping as the phase does. Feedback is the mean of B2's last two samples (stable at any FB).
 *
 * Voices: at most 6 a part (engine_t.poly). Each voice's state lives in the part's engine memory (engines.c eng_mem;
 * fmt_slot_of). A note from silence starts from phase 0 and a clean feedback; a retrigger of a sounding voice keeps
 * both (no click) and restarts the envelopes. */
#define FMT_POLY 6               /* engine_t.poly */
#define FMT_NRATIO 23
static const uint16_t FMT_RATIO_Q8[FMT_NRATIO] = {64, 128, 192, 256, 320, 384, 512, 640, 768, 896, 1024, 1280,
                                                  1536, 1792, 2048, 2304, 2560, 2816, 3072, 3328, 3584, 3840, 4096};
static const char *const N_FMT_RATIO[] = {"0.25", "0.5", "0.75", "1", "1.25", "1.5", "2", "2.5", "3", "3.5", "4", "5",
                                          "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"};
static const char *const N_FMT_ALGO[] = {"1", "2", "3", "4", "5", "6", "7", "8"};

typedef struct {
    uint32_t ph[4];              /* C, A, B1, B2 */
    int32_t fb1, fb2;            /* B2's last two samples */
    int32_t env[2];              /* the envelopes of A and B, Q24 */
    int32_t ia, ib;              /* the modulation index of A and B at the end of the last block, Q8 */
    uint16_t del[2];             /* the delay ticks left */
    uint8_t st[2];               /* 0 delay, 1 attack, 2 decay / end */
    int32_t dx1, dy1;            /* the DC blocker's last input, and its output as Q8 */
    int32_t fi1, fi2;            /* the voice filter (Q8) */
    uint8_t init;                /* 0 until the first block: ia, ib start at its values (no ramp from 0) */
    uint8_t rsv[3];
} fmt_slot_t;
ENG_MEM_FITS("FMTONE", sizeof(fmt_slot_t) * FMT_POLY);

/* the operator slots as FMTONE names them. Slot k of operator group g = P_FM{g}_ATK + 5 g + k: ATK DEC SUS REL LEVEL.
 * Same range and default as TP's where they are not bipolar (a sound load starts from TP's defaults). A (g 1), B (g 2):
 * ATK DEC END(SUS) DEL(REL) LEV; the third group's ATK / DEC are TRK A / TRK B, its REL FTO A; the fourth's ATK / DEC
 * FTO B1 / FTO B2. The fourth group's SUS, REL and LEVEL are the voice filter's CUT, RES and KEY (dsp.c rsvf, the FILTER
 * page: TYPE CUT RES KEY, P_FTYPE its TYPE); CUT 127, RES 0 and LP leave the voice as it was. P_FM3_SUS and P_FM3_LEVEL
 * are not used: TP's. */
static const param_desc_t FMT_TD[P_FM4_LEVEL - P_FM1_ATK + 1] = {
    [P_FM1_ATK - P_FM1_ATK] = {"ATK", F_TIME, 0, 127, 0, 0, 0},
    [P_FM1_DEC - P_FM1_ATK] = {"DEC", F_TIME, 0, 127, 0, 0, 0},
    [P_FM1_SUS - P_FM1_ATK] = {"END", F_PCT, 0, 127, 127, 0, 0},
    [P_FM1_REL - P_FM1_ATK] = {"DEL A", F_TIME, 0, 127, 0, 0, 0},
    [P_FM1_LEVEL - P_FM1_ATK] = {"LEV", F_PCT, 0, 127, 127, 0, 0},
    [P_FM2_ATK - P_FM1_ATK] = {"ATK", F_TIME, 0, 127, 0, 0, 0},
    [P_FM2_DEC - P_FM1_ATK] = {"DEC", F_TIME, 0, 127, 0, 0, 0},
    [P_FM2_SUS - P_FM1_ATK] = {"END", F_PCT, 0, 127, 127, 0, 0},
    [P_FM2_REL - P_FM1_ATK] = {"DEL B", F_TIME, 0, 127, 0, 0, 0},
    [P_FM2_LEVEL - P_FM1_ATK] = {"LEV", F_PCT, 0, 127, 127, 0, 0},
    [P_FM3_ATK - P_FM1_ATK] = {"TRK A", F_PCT, 0, 127, 0, 0, 0},
    [P_FM3_DEC - P_FM1_ATK] = {"TRK B", F_PCT, 0, 127, 0, 0, 0},
    [P_FM3_REL - P_FM1_ATK] = {"FTO A", F_INT, -64, 63, 0, 0, "ct"},
    [P_FM4_ATK - P_FM1_ATK] = {"FTO B1", F_INT, -64, 63, 0, 0, "ct"},
    [P_FM4_DEC - P_FM1_ATK] = {"FTO B2", F_INT, -64, 63, 0, 0, "ct"},
    [P_FM4_SUS - P_FM1_ATK] = {"CUT", F_CUTOFF, 0, 127, 127, 0, 0},
    [P_FM4_REL - P_FM1_ATK] = {"RES", F_PCT, 0, 127, 0, 0, 0},
    [P_FM4_LEVEL - P_FM1_ATK] = {"KEY", F_PCT, 0, 127, 127, 0, 0},
};
/* FMTONE's name for the FM operator slot id (params.c track_desc / param_desc_of) */
static const param_desc_t *fmt_xdesc(uint32_t id)
{
    const param_desc_t *d = &FMT_TD[id - P_FM1_ATK];
    return d->label ? d : 0;
}

static fmt_slot_t *fmt_slot_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < FMT_POLY ? (fmt_slot_t *)ENG_MEM(t - trk) + i : 0;
}

/* an envelope's delay in control ticks (32 samples): the DEL time (F_TIME: 1 ms .. 10 s), 0 = none */
static uint32_t fmt_del_ticks(int32_t del)
{
    uint32_t k;
    if (del <= 0)
        return 0;
    k = (TIME_MS_X10[del > 127 ? 127 : del] * 141u) >> 10;       /* ms x 10 x 44.1 / 32 / 10 */
    return k ? (k > 65535u ? 65535u : k) : 1u;
}

static void fmt_note_on(track_t *t, voice_t *v)
{
    fmt_slot_t *S = fmt_slot_of(t, v);
    uint32_t g;
    if (!S)
        return;
    if (!(v->env || v->env_out)) {                    /* from silence: phase 0, a clean feedback */
        S->ph[0] = S->ph[1] = S->ph[2] = S->ph[3] = 0;
        S->fb1 = S->fb2 = 0;
        S->dx1 = S->dy1 = 0;
        S->fi1 = 0;
        S->fi2 = rsvf_seed(t->p[P_FM4_REL]);
    }
    for (g = 0; g < 2u; g++) {                        /* the envelopes (re)start: after their delay */
        uint32_t d = fmt_del_ticks(t->p[P_FM1_REL + g * 5u]);
        S->env[g] = 0;
        S->del[g] = (uint16_t)d;
        S->st[g] = d ? 0 : 1;
    }
    S->init = 0;
}

/* one envelope for one control tick (32 samples): delay, attack to 1.0, decay to the end level, held. pr: ATK DEC END
 * DEL of the group (P_FMg_ATK ..); returns the envelope, Q24 */
static int32_t fmt_env_tick(const int16_t *pr, int32_t *e, uint16_t *del, uint8_t *st)
{
    if (*st == 0) {
        if (*del > 1u) {
            --*del;
            *e = 0;
            return 0;
        }
        *del = 0;
        *st = 1;
    }
    if (*st == 1) {
        if (!pr[0]) {
            *e = 1 << 24;
            *st = 2;
        } else {
            *e += (int32_t)ENV_LIN[pr[0] & 127];
            if (*e >= (1 << 24)) {
                *e = 1 << 24;
                *st = 2;
            }
        }
    } else {
        int32_t tg = pr[2] >= 127 ? 1 << 24 : (int32_t)pr[2] << 17;
        if (!pr[1])
            *e = tg;
        else
            *e += mulq16(tg - *e, ENV_EXP[pr[1] & 127]);
    }
    *e = clamp(*e, 0, 1 << 24);
    return *e;
}

/* an operator's phase increment: the voice's (base) x ratio r (Q8) x ct cents; kept below Nyquist */
static inline uint32_t fmt_inc(uint32_t base, uint32_t r, int32_t ct)
{
    uint32_t b = base >> 8, inc;
    if (b <= 0x73000u) {                               /* (r <= 4096: below 0x73000000, no divide) */
        inc = b * r;
    } else {
        uint32_t lim = 0x73000000u / r;
        inc = b > lim ? 0x73000000u : b * r;
    }
    return inc + (uint32_t)((int32_t)(inc >> 12) * (ct * 2367 / 1000));
}

#define FMT_PM(x, i) ((uint32_t)((x) * (i)) * 8u)         /* phase modulation: x Q15, index i Q8 */
/* the other carriers' sum x w (Q15): a sum of three carriers reaches ~98 000, which x 32 766 does not fit 32 bits: halve it
 * first (x >> 1, then x w >> 14) */
#define FMT_WT(x, w) ((((x) >> 1) * (w)) >> 14)
/* an operator's wave: a sine, or at HARM h (1..63) sine(ph + sine(ph) x h x 512 / 2^32 x 2 pi): the more, the more
 * harmonics, towards a saw-like spectrum (h 63: a self-modulation of ~1.55 rad) */
static inline int32_t fmt_w(uint32_t ph, int32_t h)
{
    return h ? sine_i(ph + ((uint32_t)(sine_i(ph) * h) << 9)) : sine_i(ph);
}

static void fmt_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    fmt_slot_t *S = fmt_slot_of(t, v);
    static const uint8_t NOTH[8] = {0, 0, 0, 0, 1, 2, 2, 3};      /* carriers other than C, per algorithm */
    uint32_t alg = (uint32_t)p[P_E0] & 7u, i, rc, ra, rb1, rb2;
    int32_t harm = p[P_E4], hp = harm > 0 ? harm : 0, hn = harm < 0 ? (-harm > 63 ? 63 : -harm) : 0;
    int32_t dtn = p[P_E5], fbi = p[P_E6] << 8, w = p[P_E7] * 258, la, lb, sa, sb, ia1, ib1, ia, ib, dia, dib;
    int32_t pr, norm, fb1, fb2, adj, dx1, dy1, fi1, fi2;
    int32_t fcut = clamp(p[P_FM4_SUS], 0, 127), fres = clamp(p[P_FM4_REL], 0, 127);
    uint32_t pc, pa, p1, p2, ic, iaa, i1, i2, fty = (uint32_t)p[P_FTYPE] % 3u, fon = fcut < 127 || fres > 0 || fty;
    rsvf_t rf;
    if (!S)
        return;
    if (n > CTL)
        n = CTL;
    rc = FMT_RATIO_Q8[clamp(p[P_E1], 0, FMT_NRATIO - 1)];
    ra = FMT_RATIO_Q8[clamp(p[P_E2], 0, FMT_NRATIO - 1)];
    rb1 = FMT_RATIO_Q8[clamp(p[P_E3], 0, FMT_NRATIO - 1)];
    rb2 = rb1 * 2u > 4096u ? 4096u : rb1 * 2u;                     /* B2 an octave above B1, at most 16 */
    ic = fmt_inc(m->inc, rc, 0);
    iaa = fmt_inc(m->inc, ra, p[P_FM3_REL] + dtn);                 /* FTO A and DTN: A up */
    i1 = fmt_inc(m->inc, rb1, p[P_FM4_ATK]);                       /* FTO B1 */
    i2 = fmt_inc(m->inc, rb2, p[P_FM4_DEC] - dtn);                 /* FTO B2 and DTN: B2 down */
    /* key tracking of the modulation: from C4 (pitch16 960) to C8 (1728) it falls linearly, to none at C8 for TRK 127; Q15 */
    pr = m->pitch16 > 960 ? m->pitch16 - 960 : 0;
    sa = 32767 - ((pr * p[P_FM3_ATK] * 344) >> 10);
    sb = 32767 - ((pr * p[P_FM3_DEC] * 344) >> 10);
    sa = sa < 0 ? 0 : sa;
    sb = sb < 0 ? 0 : sb;
    adj = (m->cutoff + m->shape - (64 << 8)) >> 8;                 /* the track's ENV / LFO -> FLT, SHP: the levels */
    la = clamp(p[P_FM1_LEVEL] + adj, 0, 127);
    lb = clamp(p[P_FM2_LEVEL] + adj, 0, 127);
    {   /* the envelopes' tick, and the indices (Q8) at the end of this block */
        int32_t ea = fmt_env_tick(&p[P_FM1_ATK], &S->env[0], &S->del[0], &S->st[0]) >> 9;     /* Q15 */
        int32_t eb = fmt_env_tick(&p[P_FM2_ATK], &S->env[1], &S->del[1], &S->st[1]) >> 9;
        ia1 = ((((la << 8) * sa) >> 15) * ea) >> 15;
        ib1 = ((((lb << 8) * sb) >> 15) * eb) >> 15;
    }
    if (!S->init) {
        S->ia = ia1;
        S->ib = ib1;
        S->init = 1;
    }
    ia = S->ia;
    ib = S->ib;
    dia = (ia1 - ia) >> CTL_LOG2;
    dib = (ib1 - ib) >> CTL_LOG2;
    norm = NOTH[alg] ? (int32_t)((32767u << 15) / (uint32_t)(32768 + w * (int32_t)NOTH[alg])) : 32767;
    pc = S->ph[0];
    pa = S->ph[1];
    p1 = S->ph[2];
    p2 = S->ph[3];
    fb1 = S->fb1;
    fb2 = S->fb2;
    dx1 = S->dx1;
    dy1 = S->dy1;
    fi1 = S->fi1;
    fi2 = S->fi2;
    if (fon)
        rsvf_coef(&rf, rsvf_key(fcut, m->cutoff, clamp(p[P_FM4_LEVEL], 0, 127), m->pitch16), fres);
    for (i = 0; i < n; i++) {
        int32_t o1, o2, o3, o4, s, ga, gb;
        ia += dia;
        ib += dib;
        o4 = sine_i(p2 + FMT_PM((fb1 + fb2) >> 1, fbi));           /* B2, with feedback */
        fb2 = fb1;
        fb1 = o4;
        ga = ia + (ia >> 7);                                       /* a carrier's level, Q15 */
        gb = ib + (ib >> 7);
        switch (alg) {
        case 0:
            o3 = fmt_w(p1 + FMT_PM(o4, ib), hp);
            o2 = fmt_w(pa + FMT_PM(o3, ib), hp);
            s = fmt_w(pc + FMT_PM(o2, ia), hn);
            break;
        case 1:
            o3 = fmt_w(p1, hp);
            o2 = fmt_w(pa + FMT_PM((o3 + o4) >> 1, ib), hp);
            s = fmt_w(pc + FMT_PM(o2, ia), hn);
            break;
        case 2:
            o3 = fmt_w(p1, hp);
            o2 = fmt_w(pa + FMT_PM(o3, ib), hp);
            s = fmt_w(pc + FMT_PM(o2 >> 1, ia) + FMT_PM(o4 >> 1, ib), hn);
            break;
        case 3:
            o3 = fmt_w(p1 + FMT_PM(o4, ib), hp);
            o2 = fmt_w(pa, hp);
            s = fmt_w(pc + FMT_PM(o2 >> 1, ia) + FMT_PM(o3 >> 1, ib), hn);
            break;
        case 4:
            o3 = fmt_w(p1 + FMT_PM(o4, ib), hp);
            o2 = fmt_w(pa, hp);
            o1 = fmt_w(pc + FMT_PM(o2, ia), hn);
            s = mulq15(o1 + FMT_WT(mulq15(o3, gb), w), norm);
            break;
        case 5:
            o3 = fmt_w(p1 + FMT_PM(o4, ib), hp);
            o2 = fmt_w(pa + FMT_PM(o4, ib), hp);
            o1 = fmt_w(pc + FMT_PM(o4, ib), hn);
            s = mulq15(o1 + FMT_WT(mulq15(o2, ga) + mulq15(o3, gb), w), norm);
            break;
        case 6:
            o3 = fmt_w(p1 + FMT_PM(o4, ib), hp);
            o2 = fmt_w(pa, hp);
            o1 = fmt_w(pc, hn);
            s = mulq15(o1 + FMT_WT(mulq15(o2, ga) + mulq15(o3, gb), w), norm);
            break;
        default:
            o3 = fmt_w(p1, hp);
            o2 = fmt_w(pa, hp);
            o1 = fmt_w(pc, hn);
            s = mulq15(o1 + FMT_WT(mulq15(o2, ga) + mulq15(o3, gb) + mulq15(o4, gb), w), norm);
            break;
        }
        pc += ic;
        pa += iaa;
        p1 += i1;
        p2 += i2;
        {   /* DC blocker (~15 Hz): FM with feedback, or an envelope moving, has a mean. y = x - x1 + 0.9979 y1, the state
             * with 8 fractional bits: at 15 Hz a bias of one LSB a sample would be integrated 470-fold into an offset */
            dy1 = mulq16(dy1, 65396u) + ((s - dx1) << 8);
            dx1 = s;
            s = (dy1 + 128) >> 8;
        }
        if (fon)
            s = soft_knee(rsvf_run(&rf, s >> 1, &fi1, &fi2, fty), 16000) << 1;
        out[i] += voice_amp(s, m, i);                              /* full-scale sines: as DIGITAL, no << 1 */
    }
    S->ia = ia1;
    S->ib = ib1;
    S->ph[0] = pc;
    S->ph[1] = pa;
    S->ph[2] = p1;
    S->ph[3] = p2;
    S->fb1 = fb1;
    S->fb2 = fb2;
    S->dx1 = dx1;
    S->dy1 = dy1;
    S->fi1 = fi1;
    S->fi2 = fi2;
}

/* factory presets: the engine values, and the operator slots the preset needs (fmt_track_loaded) */
typedef struct {
    int8_t a[5], b[5];           /* operator A / B: ATK DEC END DEL LEV */
    int8_t k[2], f[3];           /* TRK A, TRK B; FTO A, FTO B1, FTO B2 */
    int8_t fl[4];                /* the filter: RES, 127 - CUT, 127 - KEY, TYPE (zeros: no filter, 100 % key, LP) */
} fmt_x_t;
/* name, {ALGO, RAT C, RAT A, RAT B, HARM, DTN, FB, MIX}, {A D S R}, fenv, mono. The time knobs are 10000^(v/127) ms:
 * 40 18 ms, 60 77 ms, 80 0.33 s, 100 1.4 s, 110 2.9 s, 120 6 s; LEV 10 is ~1 rad of modulation, 25 ~2.5 rad */
static const preset_t FMT_PRESETS[] = {
    {"TINE EP", {1, 3, 3, 10, 0, 3, 0, 127}, {0, 104, 0, 70}, 0, 0, FX(0, 30, 25, 40), PAT(6)},
    {"BELL", {0, 3, 9, 11, 0, 0, 0, 127}, {0, 112, 0, 100}, 0, 0, FX(0, 15, 35, 60), PAT(7)},
    {"FM BASS", {0, 3, 3, 3, 0, 0, 20, 127}, {0, 70, 100, 25}, 0, 1, FX(10, 0, 10, 10), PAT(2)},
    {"BRASS", {1, 3, 3, 3, 10, 0, 20, 127}, {40, 70, 100, 55}, 0, 0, FX(0, 20, 25, 40), PAT(6)},
    {"GLASS PAD", {4, 3, 6, 6, 0, 6, 0, 80}, {85, 90, 110, 95}, 0, 0, FX(0, 55, 25, 70), PAT(5)},
    {"PLUCK LEAD", {2, 3, 6, 4, 0, 4, 15, 127}, {0, 75, 60, 40}, 0, 1, FX(0, 15, 40, 30), PAT(4)},
    /* the resonant filter: a plucked lead through a Q of 12 the envelope opens; a bell with the filter ringing a fifth above it */
    {"RESO PLUCK", {2, 3, 6, 4, 0, 4, 15, 127}, {0, 75, 60, 40}, 40, 1, FX(0, 15, 40, 30), PAT(4)},
    {"FIFTH RING", {0, 3, 3, 3, 0, 0, 0, 127}, {0, 90, 70, 60}, 0, 0, FX(0, 20, 30, 50), PAT(5)},
};
static const fmt_x_t FMT_X[NELEM(FMT_PRESETS)] = {
    {{0, 85, 12, 0, 42}, {0, 55, 0, 0, 30}, {25, 0}, {0, 0, 0}},       /* TINE EP: a bright start that mellows */
    {{0, 108, 10, 0, 42}, {0, 100, 0, 0, 26}, {35, 35}, {0, 0, 0}},    /* BELL: inharmonic (A 3.5, B 5 and 10) */
    {{0, 58, 50, 0, 42}, {0, 50, 25, 0, 24}, {30, 0}, {0, 0, 0}},      /* FM BASS: a punch that settles */
    {{70, 0, 100, 0, 46}, {62, 0, 100, 0, 16}, {10, 0}, {0, 0, 0}},   /* BRASS: the modulation swells in */
    {{80, 85, 70, 20, 30}, {85, 90, 60, 0, 22}, {0, 0}, {0, 5, -5}},   /* GLASS PAD: slow, B1 / B2 a little apart */
    {{0, 55, 30, 0, 32}, {0, 45, 0, 0, 40}, {25, 0}, {0, 0, 0}},       /* PLUCK LEAD */
    {{0, 55, 30, 0, 32}, {0, 45, 0, 0, 40}, {25, 0}, {0, 0, 0}, {90, 77, 27, 0}},    /* RESO PLUCK: CUT 50, KEY 100 % */
    {{0, 90, 20, 0, 24}, {0, 80, 0, 0, 14}, {0, 0}, {0, 0, 0}, {127, 75, 0, 0}},     /* FIFTH RING: CUT 52, KEY 127: G over the C */
};

static const engine_t ENG_FMTONE = {
    .name = "FMTONE",
    .page_title = {"OPS", "TONE"},
    .edit = {
        {"ALGO", F_ENUM, 0, 7, 0, N_FMT_ALGO, 0},
        {"RAT C", F_ENUM, 0, FMT_NRATIO - 1, 3, N_FMT_RATIO, 0},
        {"RAT A", F_ENUM, 0, FMT_NRATIO - 1, 3, N_FMT_RATIO, 0},
        {"RAT B", F_ENUM, 0, FMT_NRATIO - 1, 3, N_FMT_RATIO, 0},
        {"HARM", F_BIPCT, -64, 63, 0, 0, 0},
        {"DTN", F_INT, 0, 50, 0, 0, "ct"},
        {"FB", F_PCT, 0, 127, 0, 0, 0},
        {"MIX", F_PCT, 0, 127, 127, 0, 0},
    },
    .presets = FMT_PRESETS,
    .npresets = NELEM(FMT_PRESETS),
    .note_on = fmt_note_on,
    .render = fmt_render,
    .knob = {P_FM1_LEVEL, P_FM2_LEVEL, P_E6, P_E4},
    .poly = FMT_POLY,
    .mem = sizeof(fmt_slot_t) * FMT_POLY,
};

/* the preset a track just loaded (ui.c apply_preset_to, tests/hostsim.c): its operator slots (the common part of a
 * sound load has set them to TP's defaults) */
static void fmt_track_loaded(track_t *t)
{
    const fmt_x_t *x;
    uint32_t k;
    if (t->eng_req != ENGI_FMTONE)
        return;
    x = &FMT_X[(uint32_t)t->preset % NELEM(FMT_X)];
    for (k = 0; k < 5u; k++) {
        t->p[P_FM1_ATK + k] = x->a[k];
        t->p[P_FM2_ATK + k] = x->b[k];
    }
    t->p[P_FM4_REL] = x->fl[0];                        /* the filter */
    t->p[P_FM4_SUS] = 127 - x->fl[1];
    t->p[P_FM4_LEVEL] = 127 - x->fl[2];
    t->p[P_FTYPE] = x->fl[3];
    t->p[P_FM3_ATK] = x->k[0];
    t->p[P_FM3_DEC] = x->k[1];
    t->p[P_FM3_REL] = x->f[0];
    t->p[P_FM4_ATK] = x->f[1];
    t->p[P_FM4_DEC] = x->f[2];
}
