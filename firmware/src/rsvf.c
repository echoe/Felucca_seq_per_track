/* SPDX-License-Identifier: GPL-3.0-only */
/* Ported from Mini-Tone (https://github.com/eldontyrellcorp/Mini-Tone, GPL-3.0-only, a Felucca fork): the resonant voice filter
 * of its dsp.c, used here by SWARM (eng_swarm.c) and FMTONE (eng_fmtone.c). It lives in this tree and not in
 * firmware/engines/src/dsp.c because that is the Felucca-Engines submodule (MIT, pinned). It needs the tables RES_K and RSVF_G
 * (tools/gen_tables.py), clamp() and soft_knee() (dsp.c). */
/* The resonant filter of the Elektron-style engines (SWARM, WAVETONE, FMTONE, CHIP, DUALVCO, ANALOG4): Simper's trapezoidal SVF
 * again, with a resonance that runs on past the usual peak into self-oscillation. The state keeps 8 more bits than the
 * signal and the coefficients are Q24 (a cutoff of 30 Hz is g = 0.002: Q12 coefficients are 0 below ~700 Hz and the filter
 * would die there), the products are 64-bit (as FM6's). The low-pass state saturates (a soft knee at 6500, the signal's
 * full scale being 16384) and the band-pass state a good deal later: a high Q gets rounder and fatter, as an analog
 * filter's peak does, and at the top of RES (k below 0) the filter rings on its own, a near-sine at the cutoff whose level
 * the low-pass state's knee holds, at every pitch. Above RES 100 the input falls (to 0.4 at RES 127): the closer to the ring, the
 * more the filter is the sound's source, and a loud voice does not quench the ring (the way a driven analog oscillator does).
 * res 0..127 (RES_K: Q 0.5 .. 32, self-oscillating from 118), cut 0..127 << 8 as tsvf_coef (a semitone is 299 of the 1/256
 * steps, so a key-tracked cutoff can put the ring on the note: rsvf_key) */
typedef struct { int32_t a1, a2, a3, k, ig; } rsvf_t;
static inline void rsvf_coef(rsvf_t *c, int32_t cut, int32_t res)
{
    int32_t i, g, k = RES_K[clamp(res, 0, 127)];
    uint32_t den;
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = (int32_t)RSVF_G[i] + (int32_t)(((int64_t)((int32_t)RSVF_G[i + 1] - (int32_t)RSVF_G[i]) * (cut & 255)) >> 8);
    den = (1u << 24) + (uint32_t)(((int64_t)g * (g + (k << 12))) >> 24);     /* 1 + g (g + k), Q24 */
    c->a1 = (int32_t)(0xFFFFFFFFu / (den >> 8)) << 8;                        /* 1 / den, Q24 */
    c->a2 = (int32_t)(((int64_t)c->a1 * g) >> 24);
    c->a3 = (int32_t)(((int64_t)c->a2 * g) >> 24);
    c->k = k;
    c->ig = res <= 100 ? 4096 : 4096 - (clamp(res, 0, 127) - 100) * 91;          /* the input, Q12: from RES 100 it falls to 0.4 */
}
/* the cutoff of a note: cut (knob, 0..127) plus the modulation m (1/256), and key / 127 of the pitch above middle C (pitch16
 * 960) at 18.7 of 1/256 knob steps a 1/16 semitone (2411 / 16384 per unit of key): key 127 follows the note one to one, so
 * CUT 44 (262 Hz) rings on C4 */
static inline int32_t rsvf_key(int32_t cut, int32_t m, int32_t key, int32_t pitch16)
{
    return (cut << 8) + m + (((pitch16 - 960) * key * 2411) >> 14);
}
/* a note on a self-oscillating filter (RES from 119): the low-pass state starts off zero, so that a silent voice has something
 * to ring from (Q8; 2000 is an eighth of full scale, then the ring grows to its level within a few cycles of the cutoff) */
static inline int32_t rsvf_seed(int32_t res) { return res >= 119 ? 2000 << 8 : 0; }
/* a state (Q8) through the soft knee at K (Q0): exact below it */
static inline int32_t rsvf_sat(int32_t x, int32_t k)
{
    int32_t a = x >> 8;
    if (a < 0 ? -a <= k : a <= k)
        return x;
    return soft_knee(a, k) << 8;
}
/* one sample (in: +-16384 for a full-scale signal), type 0 LP, 1 BP (constant skirt: a gain of Q at the cutoff), 2 HP; the result in the same scale.
 * ic1 ic2: the state, Q8 */
static inline int32_t rsvf_run(const rsvf_t *c, int32_t in, int32_t *ic1, int32_t *ic2, uint32_t type)
{
    int32_t x = (in * c->ig) >> 4, v3 = x - *ic2, v1, v2;                   /* in << 8, times ig / 4096 */
    v1 = (int32_t)(((int64_t)c->a1 * *ic1 + (int64_t)c->a2 * v3) >> 24);
    v2 = *ic2 + (int32_t)(((int64_t)c->a2 * *ic1 + (int64_t)c->a3 * v3) >> 24);
    *ic1 = rsvf_sat(2 * v1 - *ic1, 120000);
    *ic2 = rsvf_sat(2 * v2 - *ic2, 6500);
    if (type == 0)
        return (v2 + 128) >> 8;
    if (type == 1)
        return (v1 + 128) >> 8;                           /* constant skirt: the peak rises with Q (1 / k), as the analog one does */
    return (x - (int32_t)(((int64_t)v1 * c->k) >> 12) - v2 + 128) >> 8;
}
