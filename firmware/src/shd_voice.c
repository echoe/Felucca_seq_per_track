/* SPDX-License-Identifier: GPL-3.0-only */
/* The SHDRUM voice: the two-layer drum sound (see eng_shdrum.c for what it is), as a function of a parameter set (shd_vp_t)
 * and a voice state (phases, envelopes, filter: 3 + 8 words), so two engines share it: SHDRUM (a track's parameters, the
 * voice's own state) and DRUM's KIT SH (eng_drum.c: a recipe of the library below per lane, the lane's state).
 * Included by both (guarded). */
#ifndef SHD_VOICE_C
#define SHD_VOICE_C

enum { SHW_SIN, SHW_TRI, SHW_SAW, SHW_SQR, SHW_NOIS, SHW_COUNT };
enum { SHM_MIX, SHM_FM, SHM_RING, SHM_COUNT };
enum { SHF_OFF, SHF_LP, SHF_BP, SHF_HP, SHF_COUNT };

/* one sound's parameters */
typedef struct {
    uint8_t wa, wb, xm, depth;          /* WAV A, WAV B, XMOD, DPTH */
    int8_t tunb, swep;                  /* TUN B (semitones), SWEP (semitones) */
    uint8_t swpt, drv;                  /* SWP T, DRV */
    uint8_t aatk, adec, alvl;           /* LAYER A: attack, decay (F_TIME), level */
    uint8_t batk, bdec, blvl;           /* LAYER B */
    uint8_t clik, fold, crsh;           /* CLIK, FOLD, CRSH */
    uint8_t ftyp, cut, res;             /* FLT, CUT, RES */
    int8_t fenv;                        /* FENV */
} shd_vp_t;

#define SHD_AT 0x40000000          /* in s[0], s[1]: the attack is running (the value is in the low 25 bits) */
#define SHD_EMASK 0x01FFFFFF
#define SHD_DEAD 200               /* a layer is gone when (env >> 10) * LEVEL falls under this (about -80 dB) */

static int32_t shd_env(int32_t *st, uint32_t atk, uint32_t dec)   /* one control tick; the value after it (Q24) */
{
    int32_t s = *st, e = s & SHD_EMASK;
    if (s & SHD_AT) {
        e += (int32_t)ENV_LIN[atk & 127u];
        if (e >= (1 << 24)) {
            e = 1 << 24;
            *st = e;
        } else {
            *st = SHD_AT | e;
        }
    } else {
        e -= mulq16(e, ENV_EXP[dec & 127u]);
        *st = e;
    }
    return e;
}

static inline int32_t shd_wave(uint32_t w, uint32_t ph, uint32_t inc, int32_t *nst)
{
    switch (w) {
    case SHW_SIN: return sine_i(ph);
    case SHW_TRI: return osc_tri(ph);
    case SHW_SAW: return osc_saw(ph, inc);
    case SHW_SQR: return osc_pulse(ph, inc, 0x80000000u);
    default: return (int32_t)(noise32(nst) >> 16) - 32768;
    }
}

/* a hit's start: ph[3] and s[8] (the state layout in the file header of eng_shdrum.c) */
static void shd_start(uint32_t *ph, int32_t *s, const shd_vp_t *q, uint32_t seed)
{
    ph[0] = ph[1] = ph[2] = 0;
    s[0] = SHD_AT;
    s[1] = SHD_AT;
    s[2] = 1 << 24;                                       /* the pitch envelope at its top */
    s[3] = (int32_t)((seed * 2654435761u) | 1u);          /* a noise seed per hit (xorshift: never 0) */
    s[4] = s[5] = 0;
    s[6] = q->clik ? 1 << 24 : 0;
    s[7] = 0;
}

/* is any layer (or the click) still audible? */
static int shd_alive(const shd_vp_t *q, const int32_t *s)
{
    if ((s[0] | s[1]) & SHD_AT || s[6] > (1 << 14))
        return 1;
    return ((s[0] & SHD_EMASK) >> 10) * (int32_t)q->alvl > SHD_DEAD || ((s[1] & SHD_EMASK) >> 10) * (int32_t)q->blvl > SHD_DEAD;
}

/* one block (n <= CTL samples) of the hit at pitch16 (1/16 semitones, 0..2047): y[] gets the signal ahead of the voice's
 * knee and amplitude (Q15 scale, about full scale for a layer at 100 %) */
static void shd_run(const shd_vp_t *q, uint32_t *ph, int32_t *st, int32_t pitch16, int32_t *y, uint32_t n)
{
    uint32_t wa = q->wa, wb = q->wb, xm = q->xm, ft = q->ftyp;
    int32_t depth = q->depth, drv = q->drv, clk = q->clik, fold = q->fold, crsh = q->crsh;
    int32_t gA = q->alvl * 258, gB = q->blvl * 258;
    int32_t eA0 = st[0] & SHD_EMASK, eB0 = st[1] & SHD_EMASK, pe0 = st[2], eA1, eB1, pe1;
    int32_t sw = q->swep * 16, tb = q->tunb * 16;
    int32_t pA0, pA1, nst = st[3], ck = st[6], held = st[7], ic1 = st[4], ic2 = st[5];
    int32_t md = depth * 258, gr, fg = 4096, g = 0, mk = 0, fk = 0, sh = 0;
    uint32_t phA = ph[0], phB = ph[1], cnt = ph[2], iA, iB, hold = 1, dd = (uint32_t)depth * 2628u;
    int32_t sA, sB, i;
    tsvf_t flt;
    if (n > CTL)
        n = CTL;
    eA1 = shd_env(&st[0], q->aatk, q->adec);
    eB1 = shd_env(&st[1], q->batk, q->bdec);
    pe1 = pe0 - mulq16(pe0, ENV_EXP[q->swpt & 127u]);
    st[2] = pe1;
    /* pitch: the key plus the sweep; the phase increments run linearly across the block so a fast sweep does not step */
    pA0 = clamp(pitch16 + ((sw * (pe0 >> 9)) >> 15), 0, 2047);
    pA1 = clamp(pitch16 + ((sw * (pe1 >> 9)) >> 15), 0, 2047);
    iA = pitch_inc((uint32_t)pA0);
    iB = pitch_inc((uint32_t)clamp(pA0 + tb, 0, 2047));
    sA = ((int32_t)(pitch_inc((uint32_t)pA1) - iA)) >> 5;
    sB = ((int32_t)(pitch_inc((uint32_t)clamp(pA1 + tb, 0, 2047)) - iB)) >> 5;
    gr = (gA * gB) >> 14;                                 /* the product's gain: 1.0 with both layers at 100 % */
    gr = gr > 32767 ? 32767 : gr;
    if (fold)
        fg = 4096 + fold * 3 * 4096 / 127;                /* x1..x4 (Q12) */
    if (drv > 0) {                                        /* x1..x4 into the soft clip, the level kept (DRUM's DRV) */
        g = 4096 + drv * 3 * 4096 / 127;
        mk = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));
    }
    if (crsh) {
        sh = crsh * 12 / 127;
        hold = 1u + (uint32_t)(crsh >> 5);
    }
    if (ft) {                                             /* the cutoff follows A's envelope by FENV (Q8 index steps) */
        int32_t cut = (q->cut << 8) + ((q->fenv * (eA1 >> 9)) >> 7);
        tsvf_coef(&flt, cut, q->res);
        fk = ANALOG_K(q->res);
    }
    for (i = 0; i < (int32_t)n; i++) {
        int32_t eA = (eA0 + (((eA1 - eA0) * i) >> 5)) >> 9, eB = (eB0 + (((eB1 - eB0) * i) >> 5)) >> 9;   /* Q15 */
        int32_t bb = (shd_wave(wb, phB, iB, &nst) * eB) >> 15, aa, s;
        uint32_t pa = phA;
        if (xm == SHM_FM)
            pa += (uint32_t)bb * dd;                      /* B's enveloped wave into A's phase */
        aa = (shd_wave(wa, pa, iA, &nst) * eA) >> 15;
        s = ((aa * gA) >> 15) + ((bb * gB) >> 15);
        if (xm == SHM_RING)                               /* DPTH: the two layers added -> their product */
            s = (((s >> 1) * (32767 - md)) >> 14) + ((((((aa * bb) >> 15) * gr) >> 15) * md) >> 15);
        phA += iA;
        phB += iB;
        iA += (uint32_t)sA;
        iB += (uint32_t)sB;
        if (clk) {                                        /* CLIK: noise, a 3 ms decay, ahead of the effects */
            s += (((((int32_t)(noise32(&nst) >> 16) - 32768) * (ck >> 9)) >> 15) * (clk * 258)) >> 15;
            ck -= ck >> 7;
        }
        if (fold) {                                       /* FOLD: scale up, reflect back into full scale */
            int32_t z = (((s * fg) >> 12) + 32768) & 0x1FFFF;
            z = z > 65535 ? 131071 - z : z;
            s = z - 32768;
        }
        if (g)                                            /* DRV */
            s = (softclip((s * g) >> 12) * mk) >> 15;
        if (crsh) {                                       /* CRSH: sample hold, bits dropped (rounded: no DC) */
            if (!cnt)
                held = ((s + ((1 << sh) >> 1)) >> sh) << sh;
            cnt = cnt + 1u >= hold ? 0u : cnt + 1u;
            s = held;
        }
        if (ft)                                           /* the filter (analog_tap: LP 0 / BP 1 / HP 2 of the SVF) */
            s = analog_tap(&flt, fk, s >> 1, &ic1, &ic2, ft - 1u) << 1;
        y[i] = s;
    }
    ph[0] = phA;
    ph[1] = phB;
    ph[2] = cnt;
    st[3] = nst;
    st[4] = ic1;
    st[5] = ic2;
    st[6] = ck;
    st[7] = held;
}

/* ---------------------------------------------------------------- the library --- */
/* The sounds DRUM's KIT SH puts on its lanes (SOUND, EDIT > SK SND): a name (5 characters at most, the knob shows it), the
 * note the sound is designed at (a lane's GM notes play it that many semitones from it: a tom's 41..50 are a scale around
 * it) and the sound. The first eight are the SHDRUM engine's presets, the same numbers (shdrum_test). */
typedef struct {
    uint8_t base;
    shd_vp_t q;
} shd_recipe_t;
#define SHR(base, wa, wb, xm, dp, tb, sw, st, dr, aa, ad, al, ba, bd, bl, ck, fo, cr, ft, cu, rs, fe) \
    {base, {wa, wb, xm, dp, tb, sw, st, dr, aa, ad, al, ba, bd, bl, ck, fo, cr, ft, cu, rs, fe}}
#define SHD_RECIPES                                                                                                    \
    /*   note  WAV A    WAV B    XMOD      DPTH TUNB SWEP SWPT DRV | A: ATK DEC LVL | B: ATK DEC LVL | CLIK FOLD CRSH | FLT CUT RES FENV */ \
    SHR(36, SHW_SIN, SHW_SIN, SHM_FM, 20, 0, 30, 36, 20, 0, 78, 127, 0, 40, 40, 20, 0, 0, SHF_OFF, 100, 20, 0),   /* 0 KICK */ \
    SHR(50, SHW_TRI, SHW_NOIS, SHM_MIX, 0, 0, 14, 22, 24, 0, 58, 100, 0, 62, 100, 40, 0, 0, SHF_LP, 108, 20, 0),  /* 1 SNARE */ \
    SHR(80, SHW_SQR, SHW_SQR, SHM_RING, 127, 7, 0, 0, 0, 0, 40, 100, 0, 36, 70, 0, 0, 0, SHF_HP, 90, 30, 0),     /* 2 HAT */ \
    SHR(80, SHW_SQR, SHW_SQR, SHM_RING, 127, 7, 0, 0, 0, 0, 66, 100, 0, 60, 70, 0, 0, 0, SHF_HP, 90, 30, 0),     /* 3 OPEN */ \
    SHR(48, SHW_SIN, SHW_SIN, SHM_FM, 36, 12, 12, 30, 0, 0, 76, 100, 0, 40, 50, 10, 0, 0, SHF_OFF, 100, 20, 0),  /* 4 TOM */ \
    SHR(60, SHW_NOIS, SHW_NOIS, SHM_MIX, 0, 0, 0, 0, 16, 0, 62, 100, 0, 56, 100, 0, 30, 0, SHF_BP, 80, 50, 0),   /* 5 CLAP */ \
    SHR(72, SHW_SIN, SHW_SIN, SHM_FM, 70, 19, 0, 0, 0, 0, 90, 90, 0, 76, 40, 0, 0, 0, SHF_OFF, 100, 20, 0),      /* 6 BELL */ \
    SHR(72, SHW_SQR, SHW_SAW, SHM_RING, 64, 5, 36, 50, 48, 0, 70, 100, 0, 60, 80, 0, 0, 0, SHF_OFF, 100, 20, 0), /* 7 ZAP */ \
    SHR(36, SHW_SIN, SHW_TRI, SHM_MIX, 0, 12, 24, 24, 30, 0, 60, 127, 0, 26, 50, 40, 0, 0, SHF_OFF, 100, 20, 0), /* 8 KIK2 */ \
    SHR(33, SHW_SIN, SHW_SIN, SHM_MIX, 0, 0, 12, 50, 0, 0, 95, 127, 0, 70, 0, 0, 0, 0, SHF_OFF, 100, 20, 0),     /* 9 DEEP */ \
    SHR(50, SHW_TRI, SHW_NOIS, SHM_MIX, 0, 0, 8, 16, 30, 0, 40, 90, 0, 48, 100, 60, 0, 0, SHF_HP, 60, 20, 0),    /* 10 SNR2 */ \
    SHR(70, SHW_SQR, SHW_TRI, SHM_MIX, 0, 7, 0, 0, 0, 0, 20, 100, 0, 14, 60, 80, 0, 0, SHF_BP, 100, 30, 0),      /* 11 RIM */ \
    SHR(80, SHW_SQR, SHW_SQR, SHM_RING, 127, 7, 0, 0, 0, 0, 26, 100, 0, 22, 70, 0, 0, 0, SHF_HP, 90, 30, 0),     /* 12 HATP */ \
    SHR(48, SHW_TRI, SHW_SIN, SHM_FM, 50, 19, 18, 24, 10, 0, 70, 100, 0, 34, 40, 20, 0, 0, SHF_OFF, 100, 20, 0), /* 13 TOM2 */ \
    SHR(68, SHW_SQR, SHW_SQR, SHM_MIX, 0, 7, 0, 0, 0, 0, 60, 100, 0, 60, 90, 0, 0, 0, SHF_BP, 95, 40, 0),        /* 14 COWBL */ \
    SHR(80, SHW_SQR, SHW_NOIS, SHM_RING, 100, 5, 0, 0, 0, 0, 100, 80, 0, 96, 100, 0, 0, 0, SHF_HP, 70, 20, 0),   /* 15 CYM */ \
    SHR(55, SHW_SIN, SHW_SIN, SHM_MIX, 0, 0, 6, 12, 0, 0, 50, 127, 0, 20, 40, 30, 0, 0, SHF_OFF, 100, 20, 0),    /* 16 CONGA */ \
    SHR(80, SHW_SIN, SHW_SIN, SHM_MIX, 0, 12, 0, 0, 0, 0, 25, 127, 0, 18, 40, 60, 0, 0, SHF_OFF, 100, 20, 0),    /* 17 CLAVE */ \
    SHR(64, SHW_SIN, SHW_SIN, SHM_FM, 100, 24, 20, 14, 0, 0, 44, 110, 0, 30, 0, 0, 0, 0, SHF_OFF, 100, 20, 0),   /* 18 PERC */ \
    SHR(60, SHW_NOIS, SHW_NOIS, SHM_MIX, 0, 0, 0, 0, 0, 0, 50, 127, 0, 40, 0, 0, 0, 0, SHF_BP, 60, 60, 0)        /* 19 NOISE */
static const shd_recipe_t SHD_REC[] = {SHD_RECIPES};
#define SHD_NREC ((uint32_t)(sizeof SHD_REC / sizeof SHD_REC[0]))
static const char *const N_SHK_SND[] = {"KICK", "SNARE", "HAT",  "OPEN",  "TOM",   "CLAP", "BELL",  "ZAP",   "KIK2", "DEEP",
                                        "SNR2", "RIM",   "HATP", "TOM2",  "COWBL", "CYM",  "CONGA", "CLAVE", "PERC", "NOISE"};
_Static_assert(sizeof N_SHK_SND / sizeof N_SHK_SND[0] == 20, "SHD: a name per recipe");

/* a lane's voice state in DRUM's lane memory (the 48 bytes of the metal source, which a SH lane does not use): the layout
 * shd_run takes (ph[3], s[8]), a choke flag (a closed hat ends the open one), and whether the hit still sounds */
typedef struct {
    uint32_t ph[3];
    int32_t s[8];
    uint8_t choke, live, pad[2];
} shk_state_t;

#endif
