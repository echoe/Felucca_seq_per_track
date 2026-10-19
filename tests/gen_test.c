/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The ARP's GEN mode (seq.c gen_*, 1.6_sequences), on the real sequencer and UI sources (with the stubs of tests/ui_test.c):
 *   pitch     every note in the track's SCALE over ROOT + OCT octaves (TRN added), re-pitched as they are turned
 *   density   DENS 0 nothing, 127 every step; a lower DENS plays a subset of what a higher one plays (the same loop)
 *   chord     CHORD 0 single notes; from 1 a fifth or a third, triads, sevenths, 9ths and quartals as it grows; all in the scale
 *   evolve    EVOLVE 0 the same loop every time; more changes more of the steps each loop
 *   play      only while the music plays, from the first step; the keys play as with the arp OFF; leaving GEN starts it over
 * Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define LOOP_N 16u
static track_t *gen_track(void)
{
    track_t *t;
    ui_power_on();
    song.sel = 0;
    t = &trk[0];
    track_defaults_steps(t);
    motion_clear(t);
    memset(gen, 0, sizeof gen);
    gen_defaults();
    t->p[P_AMODE] = AM_GEN; t->p[P_ARATE] = 2; t->p[P_AOCT] = 2; t->p[P_AGATE] = 64; t->p[P_APROB] = 127; t->p[P_ASWING] = 0;
    t->p[P_SLEN] = (int16_t)LOOP_N; t->p[P_ROOT] = 0; t->p[P_SCALE] = 1; t->p[P_TRANS] = 0; t->p[P_VOICE] = V_POLY;
    song.g[G_SWING] = 0; song.g[G_BPM] = 120;
    gen_set[0][GS_DENS] = 127; gen_set[0][GS_CHORD] = 0; gen_set[0][GS_EVOLVE] = 0;
    song.playing = 1;
    t->arp_pos = 0xFFFFFFF; t->arp_idx = 0xFFFFFFFFu; t->armp = AM_GEN;
    return t;
}
/* the note-ons of `loops` loops of the track: (arp step, note) pairs; returns how many */
typedef struct { uint8_t step, note; } hit_t;
static uint32_t listen(track_t *t, uint32_t loops, hit_t *h, uint32_t hmax)
{
    uint32_t blocks = loops * LOOP_N * div_samples(2) / CTL + 2u, b, n = 0, i;
    for (b = 0; b < blocks; b++) {
        uint32_t v0 = vage;
        arp_tick(t, CTL);
        for (; v0 < vage; v0++)
            for (i = 0; i < NVOICE; i++)
                if (t->v[i].age == v0 + 1u && n < hmax) {
                    h[n].step = (uint8_t)(t->arp_idx % LOOP_N);
                    h[n++].note = t->v[i].note;
                }
    }
    return n;
}
static uint32_t in_scale(const track_t *t, uint32_t note)
{
    return (scale_mask(t) >> (((int32_t)note - t->p[P_ROOT] - t->p[P_TRANS] + 120) % 12)) & 1u;
}
static uint32_t steps_mask(const hit_t *h, uint32_t n)
{
    uint32_t m = 0, i;
    for (i = 0; i < n; i++)
        m |= 1u << h[i].step;
    return m;
}

int main(void)
{
    int bad = 0, ok;
    static hit_t h[1024], h2[1024];
    track_t *t;
    uint32_t n, i, m0, m1, m2, m3;

    t = gen_track();
    n = listen(t, 1, h, 1024);
    ok = n == LOOP_N;
    for (i = 0; i < n; i++)
        ok &= in_scale(t, h[i].note) && h[i].note >= 48 && h[i].note < 48 + 24;
    bad += check("GEN: DENS 127 plays every step of the loop, each note in the scale, over OCT octaves from C3", ok);

    t = gen_track();
    t->p[P_ROOT] = 2; t->p[P_SCALE] = 5; t->p[P_TRANS] = 7; t->p[P_AOCT] = 3;      /* D, pentatonic, +7, 3 octaves */
    n = listen(t, 2, h, 1024);
    ok = n == 2u * LOOP_N;
    for (i = 0; i < n; i++)
        ok &= in_scale(t, h[i].note) && h[i].note >= 48 + 2 + 7 && h[i].note <= 48 + 2 + 7 + 36;
    bad += check("GEN: ROOT, SCALE, TRN and OCT set the pitches (D pentatonic up a fifth over three octaves)", ok);

    /* density: the same loop at several DENS; a lower DENS plays a subset of a higher one's steps */
    t = gen_track();
    gen_set[0][GS_DENS] = 0;
    n = listen(t, 1, h, 1024); ok = n == 0;
    t->arp_pos = 0xFFFFFFF; t->arp_idx = 0xFFFFFFFFu; gen_set[0][GS_DENS] = 40;
    n = listen(t, 1, h, 1024); m0 = steps_mask(h, n);
    t->arp_pos = 0xFFFFFFF; t->arp_idx = 0xFFFFFFFFu; gen_set[0][GS_DENS] = 80;
    n = listen(t, 1, h, 1024); m1 = steps_mask(h, n);
    t->arp_pos = 0xFFFFFFF; t->arp_idx = 0xFFFFFFFFu; gen_set[0][GS_DENS] = 120;
    n = listen(t, 1, h, 1024); m2 = steps_mask(h, n);
    ok &= (m0 & ~m1) == 0 && (m1 & ~m2) == 0 && __builtin_popcount(m0) < __builtin_popcount(m2) && __builtin_popcount(m2) >= 12;
    bad += check("GEN: DENS 0 plays nothing; more DENS adds steps to the same loop, never moves them", ok);

    /* chord */
    t = gen_track();
    n = listen(t, 1, h, 1024);
    ok = n == LOOP_N;                                       /* CHORD 0: one note a step */
    t->arp_pos = 0xFFFFFFF; t->arp_idx = 0xFFFFFFFFu; gen_set[0][GS_CHORD] = 127;
    n = listen(t, 1, h, 1024);
    {
        uint32_t per[LOOP_N] = {0}, mx = 0, mn = 99;
        for (i = 0; i < n; i++) { per[h[i].step]++; ok &= in_scale(t, h[i].note); }
        for (i = 0; i < LOOP_N; i++) { if (per[i] > mx) mx = per[i]; if (per[i] < mn) mn = per[i]; }
        ok &= n > 2u * LOOP_N && mn >= 3u && mx <= 4u;       /* every step a chord of 3 or 4 notes at CHORD 127 */
    }
    t->arp_pos = 0xFFFFFFF; t->arp_idx = 0xFFFFFFFFu; gen_set[0][GS_CHORD] = 40;
    n = listen(t, 1, h, 1024);
    {
        uint32_t per[LOOP_N] = {0}, mx = 0;
        for (i = 0; i < n; i++) per[h[i].step]++;
        for (i = 0; i < LOOP_N; i++) if (per[i] > mx) mx = per[i];
        ok &= mx == 3u;                                      /* triads at 32..63 */
    }
    t->arp_pos = 0xFFFFFFF; t->arp_idx = 0xFFFFFFFFu; gen_set[0][GS_CHORD] = 8;
    n = listen(t, 1, h, 1024);
    {
        uint32_t per[LOOP_N] = {0}, mx = 0;
        for (i = 0; i < n; i++) per[h[i].step]++;
        for (i = 0; i < LOOP_N; i++) if (per[i] > mx) mx = per[i];
        ok &= mx <= 2u;                                      /* a fifth or a third below 32 */
    }
    bad += check("GEN: CHORD 0 single notes; low a fifth or third, then triads, sevenths and extended chords (3..4 notes), all in the scale", ok);

    /* evolve */
    t = gen_track();
    gen_set[0][GS_DENS] = 127; gen_set[0][GS_EVOLVE] = 0;
    n = listen(t, 3, h, 1024);
    ok = n == 3u * LOOP_N;
    for (i = 0; i < LOOP_N; i++)
        ok &= h[i].note == h[LOOP_N + i].note && h[i].note == h[2u * LOOP_N + i].note;      /* the same loop three times */
    {
        uint32_t chg_lo = 0, chg_hi = 0, loops = 16u, l, k;
        t = gen_track(); gen_set[0][GS_EVOLVE] = 24;
        n = listen(t, loops, h, 1024);
        for (l = 1; l < loops && n >= (l + 1u) * LOOP_N; l++)
            for (k = 0; k < LOOP_N; k++)
                chg_lo += h[l * LOOP_N + k].note != h[(l - 1u) * LOOP_N + k].note;
        t = gen_track(); gen_set[0][GS_EVOLVE] = 127;
        n = listen(t, loops, h2, 1024);
        for (l = 1; l < loops && n >= (l + 1u) * LOOP_N; l++)
            for (k = 0; k < LOOP_N; k++)
                chg_hi += h2[l * LOOP_N + k].note != h2[(l - 1u) * LOOP_N + k].note;
        ok &= chg_lo > 0u && chg_hi > chg_lo * 2u && chg_lo < (loops - 1u) * LOOP_N / 2u;    /* gentle at 24, much more at 127 */
        for (i = 0; i < n; i++) ok &= in_scale(t, h2[i].note);
    }
    bad += check("GEN: EVOLVE 0 repeats the loop; some steps change each loop at 24, many more at 127, always in the scale", ok);

    /* play: only while the music plays, from the first step; keys play as with the arp off */
    t = gen_track();
    song.playing = 0;
    n = listen(t, 1, h, 1024);
    ok = n == 0 && t->arp_idx == 0xFFFFFFFFu;
    song.playing = 1;
    n = listen(t, 1, h, 1024);
    ok &= n == LOOP_N && h[0].step == 0;
    {
        uint32_t v0 = vage, k, hit = 0;
        input_on(t, 61, 100);
        for (k = 0; k < NVOICE; k++)
            hit |= t->v[k].age > v0 && t->v[k].note == 61u;
        ok &= t->nheld == 0 && hit;                          /* a key plays itself (it is not fed to the arp) */
    }
    input_off(t, 61);
    t->p[P_AMODE] = AM_UP; input_on(t, 61, 100);
    ok &= t->nheld == 1;                                     /* another mode: as before */
    input_off(t, 61);
    bad += check("GEN: silent while stopped, starts on its first step at PLAY; the keys play as with the arp OFF", ok);

    printf(bad ? "gen test FAILED\n" : "gen tests passed\n");
    return bad != 0;
}
