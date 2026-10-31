/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from Mini-Tone (https://github.com/eldontyrellcorp/Mini-Tone, GPL-3.0-only); engine numbers changed for this tree (SWARM 15, FMTONE 16) */
/* FMTONE engine test (src/eng_fmtone.c) on the host, through hostsim.c as regress.c.
 *   build/host/fmtone_test
 * 1. ratios: C plays the key x RAT C (0.25 .. 16); A, B1 and B2 (B2 an octave above B1, at most 16) as carriers
 *    (algorithm 8) at their ratios.
 * 2. carriers: MIX changes the sound only in the algorithms that have carriers besides C (5..8: bit for bit the
 *    same in 1..4); with LEV 0 the modulators make no sidebands (a pure C).
 * 3. modulation: the sidebands grow with LEV A in algorithm 1.
 * 4. envelopes (algorithm 1, A inharmonic against C, the energy outside C's line against time): DEC with END 0
 *    takes the sidebands away, END 127 keeps them, DEL holds them back until it has run, a slow ATK brings them
 *    in over time.
 * 5. FB: B2's harmonics grow with FB (algorithm 8).
 * 6. HARM: + changes the waves of A and B1 only (C and the algorithm's silent group: bit for bit), - changes C only.
 * 7. TRK: key tracking takes the modulation off at the top of the keyboard and leaves notes below C4 bit for bit.
 * 8. FTO A / B1 / B2 shift their operator by their cents; DTN moves A up and B2 down.
 * 9. voices: at most 6 (POLY, UNISON); one voice's state is not another's.
 * 10. descriptors: the operator slots named and ranged by the engine (FTO -64..63), TP's for every other engine; a
 *     factory preset sets its slots.
 * 11. corners: sampled extremes of every parameter: headroom, DC, bounded state; the sum of all carriers of
 *     algorithm 8 stays within one carrier's level.
 * 12. level against ANALOG's SAW LEAD, determinism, cost. */
#define main hostsim_main
#include "hostsim.c"
#undef main
/* the level reference of the "level" checks (hostsim.c of Mini-Tone: HS_REF_SAW_*): ANALOG's SAW LEAD, C4, velocity 100, ATK 0, SUS 127, no sends, one
 * voice; the RMS of 16384 samples after 20 blocks, and the largest sample in the first 0.5 s (the voice sum: before the master) */
#ifndef HS_REF_SAW_RMS
#define HS_REF_SAW_RMS 0.401917
#define HS_REF_SAW_PEAK 30342.0
#endif
#include <time.h>

#define NFFT 16384

static int bad;
static void check(const char *what, int ok)
{
    printf("fmtone: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* a track of the FMTONE engine: e = the 8 engine values, slots = the operator slots (see eng_fmtone.c: A ATK DEC END
 * DEL LEV, B ATK DEC END DEL LEV, TRK A, TRK B, FTO A, FTO B1, FTO B2); a flat envelope, no sends, POLY */
typedef struct { int16_t e[8]; int16_t a[5], b[5], k[2], f[3]; } cfg_t;
static void cfg_init(cfg_t *c)
{
    static const cfg_t D = {{0, 3, 3, 3, 0, 0, 0, 127}, {0, 0, 127, 0, 127}, {0, 0, 127, 0, 127}, {0, 0}, {0, 0, 0}};
    *c = D;
}
static void setup(const cfg_t *c)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], ENGI_FMTONE, 0);
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = c->e[i];
    for (i = 0; i < 5u; i++) {
        trk[0].p[P_FM1_ATK + i] = c->a[i];
        trk[0].p[P_FM2_ATK + i] = c->b[i];
    }
    trk[0].p[P_FM3_ATK] = c->k[0];
    trk[0].p[P_FM3_DEC] = c->k[1];
    trk[0].p[P_FM3_REL] = c->f[0];
    trk[0].p[P_FM4_ATK] = c->f[1];
    trk[0].p[P_FM4_DEC] = c->f[2];
    trk[0].p[P_ATK] = 0;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_REL] = 10;
    trk[0].p[P_ED_FLT] = 0;
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
}

/* the voices' sum of note: skip samples dropped, then n samples (track_render, before the mix) */
static void render(const cfg_t *c, uint32_t note, uint32_t skip, double *y, uint32_t n)
{
    static int32_t b[CTL];
    uint32_t i, k;
    setup(c);
    trk_note_on(&trk[0], note, 100);
    for (i = 0; i < skip / CTL; i++)
        track_render(&trk[0], b, CTL);
    for (i = 0; i < n; i += CTL) {
        track_render(&trk[0], b, CTL);
        for (k = 0; k < CTL && i + k < n; k++)
            y[i + k] = b[k] / 32768.0;
    }
}

/* ---------------------------------------------------------------- spectra --- */
static void fft(double *re, double *im, uint32_t n)
{
    uint32_t i, j, len;
    for (i = 1, j = 0; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double a = -2 * M_PI / len;
        for (i = 0; i < n; i += len)
            for (j = 0; j < len / 2; j++) {
                double wr = cos(a * j), wi = sin(a * j);
                double xr = re[i + j + len / 2] * wr - im[i + j + len / 2] * wi;
                double xi = re[i + j + len / 2] * wi + im[i + j + len / 2] * wr;
                re[i + j + len / 2] = re[i + j] - xr, im[i + j + len / 2] = im[i + j] - xi;
                re[i + j] += xr, im[i + j] += xi;
            }
    }
}
static void spec1(const double *y, uint32_t off, uint32_t nfft, double *ps)
{
    static double re[NFFT], im[NFFT];
    uint32_t i;
    for (i = 0; i < nfft; i++) {
        re[i] = y[off + i] * (0.5 - 0.5 * cos(2 * M_PI * i / nfft));
        im[i] = 0;
    }
    fft(re, im, nfft);
    for (i = 0; i < nfft / 2; i++)
        ps[i] = re[i] * re[i] + im[i] * im[i];
}
static double hz_of(uint32_t note) { return 440.0 * pow(2.0, ((int)note - 69) / 12.0); }
static uint32_t bin_of(double hz, uint32_t nfft) { return (uint32_t)(hz / FS * nfft + 0.5); }
static double peak_hz(const double *ps, double lo, double hi, uint32_t nfft)
{
    uint32_t a = bin_of(lo, nfft), b = bin_of(hi, nfft), i, best = a;
    double al, be, ga, p;
    for (i = a; i <= b; i++)
        if (ps[i] > ps[best])
            best = i;
    al = log(ps[best - 1] + 1e-30);
    be = log(ps[best] + 1e-30);
    ga = log(ps[best + 1] + 1e-30);
    p = 0.5 * (al - ga) / (al - 2 * be + ga);
    return (best + p) * (double)FS / nfft;
}
static double band_pow(const double *ps, double hz, double frac, uint32_t nfft)
{
    uint32_t a = bin_of(hz * (1 - frac), nfft), b = bin_of(hz * (1 + frac), nfft), i;
    double s = 0;
    for (i = a; i <= b; i++)
        s += ps[i];
    return s;
}
static double tot_pow(const double *ps, uint32_t nfft, double lo, double hi)
{
    uint32_t a = bin_of(lo, nfft), b = bin_of(hi, nfft), i;
    double s = 0;
    for (i = a; i <= b; i++)
        s += ps[i];
    return s;
}
static double db(double a, double b) { return 10 * log10((a + 1e-30) / (b + 1e-30)); }

/* the part of the power (40 Hz .. 18 kHz) outside C's line (+-12 % of f0: the Hann window's main lobe is inside) of the
 * 8192 samples (186 ms) at skip: the sidebands' share. A pure sine gives < 1 % */
static double sideband_share(const cfg_t *c, uint32_t note, uint32_t at)
{
    static double y[16384], ps[4096];
    double f0 = hz_of(note), tot, line;
    render(c, note, at, y, 8192);
    spec1(y, 0, 8192, ps);
    tot = tot_pow(ps, 8192, 40, 18000);
    line = band_pow(ps, f0, 0.12, 8192);
    return tot > 0 ? (tot - line) / tot : 0;
}

static int same(const double *a, const double *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

int main(void)
{
    static double y[NFFT + 4096], y2[NFFT + 4096], ps[NFFT / 2], ps2[NFFT / 2];
    cfg_t c, c0;
    uint32_t i, k, note = 57;                    /* A3: 220 Hz */
    double f0 = hz_of(note);

    check("the engine is number 16, 16 engines come before it (SWARM 15, SHDRUM 14)", ENGI_FMTONE == 16u && ENGINES[16] == &ENG_FMTONE && NENGINES == 17u);

    /* 1. ratios */
    {
        static const struct { int idx; double r; } R[] = {{0, 0.25}, {1, 0.5}, {3, 1.0}, {6, 2.0}, {10, 4.0}, {14, 8.0}, {22, 16.0}};
        int ok = 1;
        for (k = 0; k < NELEM(R); k++) {
            double want = f0 * R[k].r, got;
            cfg_init(&c);
            c.e[1] = (int16_t)R[k].idx;
            c.a[4] = c.b[4] = 0;
            render(&c, note, 3200, y, NFFT);
            spec1(y, 0, NFFT, ps);
            got = peak_hz(ps, want * 0.8, want * 1.2, NFFT);
            if (fabs(got / want - 1) > 0.01) {
                ok = 0;
                printf("fmtone:   RAT C %.2f: peak %.1f Hz, wanted %.1f\n", R[k].r, got, want);
            }
        }
        check("RAT C: C plays the key x 0.25, 0.5, 1, 2, 4, 8, 16 (within 1 %)", ok);
        /* A, B1, B2 as carriers: algorithm 8, C silent by RAT C ... C always sounds: look for the three other lines */
        cfg_init(&c);
        c.e[0] = 7;
        c.e[1] = 3;                          /* C = 1 */
        c.e[2] = 10;                         /* A = 4 */
        c.e[3] = 8;                          /* B1 = 3, B2 = 6 */
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        {
            double ref = band_pow(ps, f0, 0.03, NFFT);
            int ok2 = 1;
            static const double M[3] = {4.0, 3.0, 6.0};
            for (k = 0; k < 3u; k++)
                ok2 &= db(band_pow(ps, f0 * M[k], 0.03, NFFT), ref) > -12 && db(band_pow(ps, f0 * M[k], 0.03, NFFT), ref) < 6;
            check("algorithm 8: A at RAT A, B1 at RAT B, B2 an octave above it: each line within -12..+6 dB of C's", ok2);
        }
        cfg_init(&c);
        c.e[0] = 7;
        c.e[3] = 14;                         /* B1 = 8 -> B2 = 16 (capped) */
        render(&c, 36, 3200, y, NFFT);       /* 65 Hz: B2 at 1045 Hz */
        spec1(y, 0, NFFT, ps);
        printf("fmtone:   RAT B 8, note 36 (%.1f Hz): peak near 16x: %.1f Hz (want %.1f)\n", hz_of(36),
               peak_hz(ps, 16 * hz_of(36) * 0.9, 16 * hz_of(36) * 1.1, NFFT), 16 * hz_of(36));
        check("RAT B 8: B2 is an octave above B1 (16)", fabs(peak_hz(ps, 16 * hz_of(36) * 0.9, 16 * hz_of(36) * 1.1, NFFT) / (16 * hz_of(36)) - 1) < 0.01);
    }

    /* 2. carriers */
    {
        int ok14 = 1, ok58 = 1;
        for (k = 0; k < 8u; k++) {
            cfg_init(&c);
            c.e[0] = (int16_t)k;
            c.e[2] = 8;
            c.e[3] = 10;
            c.a[4] = 70;
            c.b[4] = 50;
            c.e[7] = 0;
            render(&c, note, 3200, y, 4096);
            c.e[7] = 127;
            render(&c, note, 3200, y2, 4096);
            if (k < 4u)
                ok14 &= same(y, y2, 4096);
            else
                ok58 &= !same(y, y2, 4096);
        }
        check("MIX changes nothing in algorithms 1..4 (C is their only carrier), bit for bit", ok14);
        check("MIX changes the sound in algorithms 5..8 (they have more carriers)", ok58);
        {
            int ok = 1;
            for (k = 0; k < 8u; k++) {
                cfg_init(&c);
                c.e[0] = (int16_t)k;
                c.e[2] = 8;
                c.e[3] = 10;
                c.e[7] = 0;                  /* MIX 0: C only (algorithms 5..8) */
                c.a[4] = 0;
                c.b[4] = 0;                  /* no modulation */
                render(&c, note, 3200, y, NFFT);
                spec1(y, 0, NFFT, ps);
                ok &= db(band_pow(ps, f0, 0.03, NFFT), tot_pow(ps, NFFT, 40, 12000)) > -0.5;
            }
            check("LEV A = LEV B = 0, MIX 0: every algorithm is C's pure sine (> 89 % of the power on its line)", ok);
        }
    }

    /* 3. modulation grows with LEV A */
    {
        double s[4];
        static const int16_t L[4] = {0, 4, 8, 16};        /* indices of 0, 0.4, 0.8, 1.6 rad: the share rises and stays below 1 */
        int mono = 1;
        for (k = 0; k < 4u; k++) {
            cfg_init(&c);
            c.e[2] = 9;                      /* A = 3.5: its sidebands do not fall on C's line */
            c.a[4] = L[k];
            s[k] = sideband_share(&c, note, 3200);
            if (k && s[k] <= s[k - 1])
                mono = 0;
        }
        printf("fmtone:   sideband share at LEV A 0 4 8 16: %.3f %.3f %.3f %.3f\n", s[0], s[1], s[2], s[3]);
        check("algorithm 1: the sidebands grow with LEV A (monotonic), none at 0", mono && s[0] < 0.01 && s[3] > 0.4);
    }

    /* 4. envelopes (A, algorithm 1: B silent). LEV 16 is 1.6 rad at full envelope: the share rises with the envelope */
    {
        double sh[3], start;
        cfg_init(&c0);
        c0.e[2] = 9;                         /* A = 3.5 */
        c0.a[4] = 16;
        c0.b[4] = 0;
        c = c0;
        c.a[1] = 100;                        /* DEC ~1.7 s */
        c.a[2] = 0;
        start = sideband_share(&c, note, 0);
        sh[1] = sideband_share(&c, note, 44100 * 2);
        sh[2] = sideband_share(&c, note, 44100 * 8);
        printf("fmtone:   A DEC 100, END 0: sideband share at 0, 2 s, 8 s: %.3f %.3f %.3f\n", start, sh[1], sh[2]);
        check("A DEC with END 0: the sidebands decay away (< 10 % of the start's after 8 s)", sh[2] < 0.1 * start && start > 0.3);
        check("A DEC: on the way at 2 s (between the start and the end)", sh[1] < start && sh[1] > sh[2]);
        c.a[2] = 127;
        sh[2] = sideband_share(&c, note, 44100 * 8);
        check("A DEC with END 127: the sidebands stay (> 90 % of the start's)", sh[2] > 0.9 * start);
        c.a[2] = 60;
        sh[2] = sideband_share(&c, note, 44100 * 8);
        printf("fmtone:   END 60 after 8 s: share %.3f against the start's %.3f\n", sh[2], start);
        check("A END 60: the sidebands settle between (15 % .. 70 % of the start's)", sh[2] > 0.15 * start && sh[2] < 0.7 * start);
        /* DEL */
        c = c0;
        c.a[3] = 80;                         /* DEL A: ~330 ms */
        sh[0] = sideband_share(&c, note, 0);
        sh[1] = sideband_share(&c, note, 44100 * 3 / 4);
        printf("fmtone:   DEL A 80 (~0.33 s): share at 0 s %.3f, at 0.75 s %.3f\n", sh[0], sh[1]);
        check("DEL A: no sidebands while the delay runs, then the full ones", sh[0] < 0.2 * sh[1] && sh[1] > 0.3);
        /* ATK */
        c = c0;
        c.a[0] = 95;                         /* a slow attack */
        sh[0] = sideband_share(&c, note, 0);
        sh[1] = sideband_share(&c, note, 44100 * 5);
        printf("fmtone:   ATK A 95: share at 0 s %.3f, at 5 s %.3f\n", sh[0], sh[1]);
        check("A ATK: the sidebands come in over time (start < 25 % of the end's)", sh[0] < 0.25 * sh[1] && sh[1] > 0.3);
        /* B has its own: algorithm 3 (B2 modulates C directly), A silent; B2 an octave above B1 */
        cfg_init(&c);
        c.e[0] = 2;
        c.e[3] = 8;                          /* B1 = 3, B2 = 6 */
        c.a[4] = 0;
        c.b[4] = 32;                         /* (this path halves B2's output: 1.6 rad) */
        c.b[1] = 100;
        c.b[2] = 0;
        start = sideband_share(&c, note, 0);
        sh[2] = sideband_share(&c, note, 44100 * 8);
        printf("fmtone:   B DEC 100, END 0 (algorithm 3): share at 0 %.3f, at 8 s %.3f\n", start, sh[2]);
        check("B ENV works on B2 (algorithm 3: it decays away, A off)", start > 0.2 && sh[2] < 0.1 * start);
        c.b[3] = 80;
        c.b[1] = 0;
        c.b[2] = 127;
        sh[0] = sideband_share(&c, note, 0);
        sh[1] = sideband_share(&c, note, 44100 * 3 / 4);
        check("DEL B delays B's modulation", sh[0] < 0.2 * sh[1] && sh[1] > 0.2);
    }

    /* 5. FB */
    {
        double r[2];
        for (k = 0; k < 2u; k++) {
            cfg_init(&c);
            c.e[0] = 7;
            c.e[3] = 6;                      /* B1 = 2, B2 = 4 */
            c.e[2] = 3;
            c.a[4] = 0;
            c.b[4] = 127;
            c.e[6] = k ? 90 : 0;
            render(&c, note, 3200, y, NFFT);
            spec1(y, 0, NFFT, ps);
            r[k] = db(band_pow(ps, f0 * 8.0, 0.02, NFFT), band_pow(ps, f0 * 4.0, 0.02, NFFT));   /* B2's 2nd harmonic */
        }
        printf("fmtone:   B2's 2nd harmonic against its line: FB 0 %.1f dB, FB 90 %.1f dB\n", r[0], r[1]);
        check("FB: B2's harmonics grow (FB 0: below -40 dB, FB 90: above -20 dB)", r[0] < -40 && r[1] > -20);
    }

    /* 6. HARM */
    {
        static double ya[4096], yb[4096];
        int ok;
        /* + changes A and B1 only: in algorithm 8 with LEV A = LEV B = 0 nothing of A / B sounds: bit for bit */
        cfg_init(&c);
        c.e[0] = 7;
        c.a[4] = c.b[4] = 0;
        render(&c, note, 3200, ya, 4096);
        c.e[4] = 40;
        render(&c, note, 3200, yb, 4096);
        check("HARM +40 with A and B silent: C untouched (bit for bit)", same(ya, yb, 4096));
        c.e[4] = -40;
        render(&c, note, 3200, yb, 4096);
        check("HARM -40: C changes", !same(ya, yb, 4096));
        /* the sound: C's 2nd harmonic with HARM -40 (A and B silent), A's with HARM +40 */
        cfg_init(&c);
        c.e[0] = 7;
        c.a[4] = c.b[4] = 0;
        c.e[4] = -40;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        {
            double h2 = db(band_pow(ps, 2 * f0, 0.02, NFFT), band_pow(ps, f0, 0.02, NFFT));
            double h3 = db(band_pow(ps, 3 * f0, 0.02, NFFT), band_pow(ps, f0, 0.02, NFFT));
            printf("fmtone:   HARM -40, C alone: 2nd harmonic %.1f dB, 3rd %.1f dB\n", h2, h3);
            ok = h2 > -25 && h3 > -35;
        }
        c.e[4] = 0;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        ok &= db(band_pow(ps, 2 * f0, 0.02, NFFT), band_pow(ps, f0, 0.02, NFFT)) < -60;
        check("HARM -40 gives C harmonics (2nd above -25 dB); HARM 0 a pure sine (below -60 dB)", ok);
        cfg_init(&c);
        c.e[0] = 7;
        c.e[2] = 8;                          /* A = 3 */
        c.a[4] = 127;
        c.b[4] = 0;
        c.e[7] = 127;
        c.e[4] = 0;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        ok = db(band_pow(ps, 6 * f0, 0.02, NFFT), band_pow(ps, 3 * f0, 0.02, NFFT)) < -60;
        c.e[4] = 40;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        {
            double h2 = db(band_pow(ps, 6 * f0, 0.02, NFFT), band_pow(ps, 3 * f0, 0.02, NFFT));
            printf("fmtone:   HARM +40, A alone (3 x): 2nd harmonic of A %.1f dB\n", h2);
            ok &= h2 > -25;
        }
        check("HARM +40 gives A harmonics (2nd above -25 dB), HARM 0 none (below -60 dB)", ok);
    }

    /* 7. TRK: the modulation falls from C4 (60) to C8 (108) by TRK; LEV 16 keeps the share in its rising range */
    {
        static double ya[4096], yb[4096];
        double lo, mid, hi;
        cfg_init(&c);
        c.e[2] = 9;
        c.a[4] = 16;
        c.b[4] = 0;
        c.k[0] = 0;
        render(&c, 48, 3200, ya, 4096);
        c.k[0] = 127;
        render(&c, 48, 3200, yb, 4096);
        check("TRK A 127 at C3 (below C4): bit for bit as TRK 0", same(ya, yb, 4096));
        render(&c, 60, 3200, yb, 4096);
        c.k[0] = 0;
        render(&c, 60, 3200, ya, 4096);
        check("TRK A 127 at C4: bit for bit as TRK 0 (the tracking starts there)", same(ya, yb, 4096));
        c.k[0] = 0;
        lo = sideband_share(&c, 96, 3200);
        c.k[0] = 64;
        mid = sideband_share(&c, 96, 3200);
        c.k[0] = 127;
        hi = sideband_share(&c, 96, 3200);
        printf("fmtone:   sideband share at C7: TRK A 0 %.3f, 64 %.3f, 127 %.3f\n", lo, mid, hi);
        check("TRK A at C7: 0 > 64 > 127 (the modulation falls with TRK)", lo > mid && mid > hi && lo > 0.1);
        c.k[0] = 0;
        lo = sideband_share(&c, 108, 3200);
        c.k[0] = 127;
        hi = sideband_share(&c, 108, 3200);
        printf("fmtone:   sideband share at C8: TRK A 0 %.3f, 127 %.3f\n", lo, hi);
        check("TRK A 127 at C8: no modulation left (< 2 % of TRK 0's, a pure sine)", lo > 0.1 && hi < 0.02 * lo + 0.005);
        /* B: algorithm 3, B2 modulates C */
        cfg_init(&c);
        c.e[0] = 2;
        c.e[3] = 8;
        c.a[4] = 0;
        c.b[4] = 32;
        c.k[1] = 0;
        lo = sideband_share(&c, 108, 3200);
        c.k[1] = 127;
        hi = sideband_share(&c, 108, 3200);
        c.k[1] = 0;
        c.k[0] = 127;                        /* TRK A does not touch B */
        {
            double other = sideband_share(&c, 108, 3200);
            check("TRK B takes B's modulation off at C8, TRK A does not", lo > 0.1 && hi < 0.02 * lo + 0.005 && fabs(other - lo) < 0.02);
        }
    }

    /* 8. fine tuning, DTN */
    {
        double fa, fb1, fb2, ratio;
        cfg_init(&c);
        c.e[0] = 7;
        c.e[1] = 3;
        c.e[2] = 10;                         /* A = 4 */
        c.e[3] = 8;                          /* B1 = 3, B2 = 6 */
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        fa = peak_hz(ps, 4 * f0 * 0.95, 4 * f0 * 1.05, NFFT);
        fb1 = peak_hz(ps, 3 * f0 * 0.95, 3 * f0 * 1.05, NFFT);
        fb2 = peak_hz(ps, 6 * f0 * 0.95, 6 * f0 * 1.05, NFFT);
        c.f[0] = 50;
        c.f[1] = -40;
        c.f[2] = 30;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps2);
        ratio = peak_hz(ps2, 4 * f0 * 0.95, 4 * f0 * 1.05, NFFT) / fa;
        check("FTO A +50 ct moves A up by 50 cents (+-3 ct)", fabs(1200 * log2(ratio) - 50) < 3);
        ratio = peak_hz(ps2, 3 * f0 * 0.95, 3 * f0 * 1.05, NFFT) / fb1;
        check("FTO B1 -40 ct moves B1 down by 40 cents (+-3 ct)", fabs(1200 * log2(ratio) + 40) < 3);
        ratio = peak_hz(ps2, 6 * f0 * 0.95, 6 * f0 * 1.05, NFFT) / fb2;
        check("FTO B2 +30 ct moves B2 up by 30 cents (+-3 ct)", fabs(1200 * log2(ratio) - 30) < 3);
        c.f[0] = c.f[1] = c.f[2] = 0;
        c.e[5] = 40;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps2);
        {
            double ca = 1200 * log2(peak_hz(ps2, 4 * f0 * 0.95, 4 * f0 * 1.05, NFFT) / fa);
            double cb = 1200 * log2(peak_hz(ps2, 6 * f0 * 0.95, 6 * f0 * 1.05, NFFT) / fb2);
            double cc = 1200 * log2(peak_hz(ps2, 3 * f0 * 0.95, 3 * f0 * 1.05, NFFT) / fb1);
            printf("fmtone:   DTN 40: A %+.1f ct, B2 %+.1f ct, B1 %+.1f ct\n", ca, cb, cc);
            check("DTN 40: A up 40 cents, B2 down 40 cents, B1 untouched (+-3 ct)",
                  fabs(ca - 40) < 3 && fabs(cb + 40) < 3 && fabs(cc) < 3);
        }
    }

    /* 9. voices */
    {
        int maxv = 0, maxu = 0;
        static int32_t b[CTL];
        static track_t snap;
        static uint8_t mem_snap[sizeof eng_mem], slot_alone[sizeof(fmt_slot_t)];
        int alone, started;
        uint32_t j;
        for (k = 0; k < 2u; k++) {
            uint32_t n;
            cfg_init(&c);
            setup(&c);
            trk[0].p[P_VOICE] = k ? V_UNISON : V_POLY;
            for (n = 0; n < 9u; n++) {
                trk_note_on(&trk[0], 40 + 3 * n, 100);
                track_render(&trk[0], b, CTL);
            }
            for (j = 0, n = 0; j < NVOICE; j++)
                n += trk[0].v[j].active != 0;
            if (k)
                maxu = (int)n;
            else
                maxv = (int)n;
        }
        check("POLY: 9 keys sound at most 6 voices (engine_t.poly)", maxv == 6);
        check("UNISON: at most 6 voices", maxu >= 1 && maxu <= 6);
        cfg_init(&c);
        c.a[4] = 90;
        c.e[6] = 40;
        setup(&c);
        trk_note_on(&trk[0], 60, 100);
        for (j = 0; j < 20u; j++)
            track_render(&trk[0], b, CTL);
        memcpy(&snap, &trk[0], sizeof snap);
        memcpy(mem_snap, eng_mem, sizeof eng_mem);
        track_render(&trk[0], b, CTL);
        memcpy(slot_alone, ENG_MEM(0), sizeof slot_alone);
        memcpy(&trk[0], &snap, sizeof snap);
        memcpy(eng_mem, mem_snap, sizeof eng_mem);
        trk_note_on(&trk[0], 67, 100);
        started = !memcmp(ENG_MEM(0), mem_snap, sizeof(fmt_slot_t));
        track_render(&trk[0], b, CTL);
        alone = !memcmp(ENG_MEM(0), slot_alone, sizeof slot_alone);
        check("a second note leaves the first voice's slot as it was (its note-on and its block)", started && alone);
        check("the six voices' state fits the part's engine memory", sizeof(fmt_slot_t) * FMT_POLY <= ENG_MEM_PART);
        setup(&c);
        trk_note_on(&trk[0], 60, 100);
        trk_note_off(&trk[0], 60);
        for (j = 0; j < FS * 3u / CTL; j++)
            track_render(&trk[0], b, CTL);
        for (j = 0, alone = 1; j < NVOICE; j++)
            alone &= !trk[0].v[j].active;
        check("the voices are free after the release", alone);
    }

    /* 10. descriptors and presets */
    {
        const param_desc_t *d = param_desc_of(ENGI_FMTONE, P_FM3_REL), *o = param_desc_of(0, P_FM3_REL);
        static const struct { uint32_t id; const char *lab; } L[] = {
            {P_FM1_ATK, "ATK"}, {P_FM1_DEC, "DEC"}, {P_FM1_SUS, "END"}, {P_FM1_REL, "DEL A"}, {P_FM1_LEVEL, "LEV"},
            {P_FM2_REL, "DEL B"}, {P_FM3_ATK, "TRK A"}, {P_FM3_DEC, "TRK B"}, {P_FM3_REL, "FTO A"}, {P_FM4_ATK, "FTO B1"},
            {P_FM4_DEC, "FTO B2"}};
        int ok = 1;
        memset(trk, 0, sizeof trk);
        host_tracks_init();
        host_preset(&trk[0], ENGI_FMTONE, 0);
        for (k = 0; k < NELEM(L); k++)
            ok &= str_eq(track_desc(&trk[0], L[k].id)->label, L[k].lab);
        check("the operator slots carry their FMTONE names (ATK DEC END DEL A / B LEV TRK A / B FTO A B1 B2)", ok);
        check("FTO is -64..63 for FMTONE (param_desc_of: what a project, a preset, motion and MIDI clamp to)",
              d->min == -64 && d->max == 63 && d->def == 0 && param_fit(d, 100) == 63 && param_fit(d, -100) == -64);
        check("every other engine keeps TP's descriptor of the slot (0..127)", o == &TP[P_FM3_REL] && o->min == 0 && o->max == 127);
        host_preset(&trk[0], 0, 0);
        check("an unused slot (P_FM3_SUS, P_FM3_LEVEL) is TP's even for FMTONE; the fourth group's SUS REL LEVEL are the filter's CUT RES KEY",
              param_desc_of(ENGI_FMTONE, P_FM3_SUS) == &TP[P_FM3_SUS] && param_desc_of(ENGI_FMTONE, P_FM3_LEVEL) == &TP[P_FM3_LEVEL] &&
                  str_eq(param_desc_of(ENGI_FMTONE, P_FM4_SUS)->label, "CUT") && str_eq(param_desc_of(ENGI_FMTONE, P_FM4_REL)->label, "RES") &&
                  str_eq(param_desc_of(ENGI_FMTONE, P_FM4_LEVEL)->label, "KEY"));
        ok = 1;
        for (k = 0; k < ENG_FMTONE.npresets; k++) {
            host_preset(&trk[0], ENGI_FMTONE, k);
            ok &= trk[0].p[P_FM1_LEVEL] == FMT_X[k].a[4] && trk[0].p[P_FM2_DEC] == FMT_X[k].b[1] &&
                  trk[0].p[P_FM3_ATK] == FMT_X[k].k[0] && trk[0].p[P_FM3_REL] == FMT_X[k].f[0] &&
                  trk[0].p[P_FM4_DEC] == FMT_X[k].f[2] && trk[0].p[P_E0] == FMT_PRESETS[k].e[0];
        }
        check("a factory preset sets the engine values and the operator slots", ok);
    }

    /* 11. corners: sampled extremes; headroom, DC, state */
    {
        uint32_t seed = 12345u, n = 0, over = 0, blow = 0;
        int32_t worst = 0;
        double dcmax = 0;
        static int32_t b[CTL];
        cfg_t worstc;
        for (n = 0; n < 1500u; n++) {
            uint32_t pass, j, note_i;
            static const int16_t RT[3] = {0, 11, 22};
            cfg_init(&c);
#define RND(m) (seed = seed * 1664525u + 1013904223u, (seed >> 8) % (m))
            c.e[0] = (int16_t)RND(8);
            c.e[1] = RT[RND(3)];
            c.e[2] = RT[RND(3)];
            c.e[3] = RT[RND(3)];
            c.e[4] = (int16_t)(RND(3) == 0 ? -64 : RND(2) ? 63 : 0);
            c.e[5] = (int16_t)(RND(2) ? 50 : 0);
            c.e[6] = (int16_t)(RND(2) ? 127 : 0);
            c.e[7] = (int16_t)(RND(2) ? 127 : 0);
            c.a[0] = (int16_t)(RND(3) == 0 ? 127 : 0);
            c.a[1] = (int16_t)(RND(2) ? 127 : 0);
            c.a[2] = (int16_t)(RND(2) ? 127 : 0);
            c.a[3] = (int16_t)(RND(4) == 0 ? 127 : 0);
            c.a[4] = (int16_t)(RND(2) ? 127 : 0);
            c.b[0] = c.a[1]; c.b[1] = c.a[0]; c.b[2] = (int16_t)(RND(2) ? 127 : 0); c.b[3] = 0;
            c.b[4] = (int16_t)(RND(2) ? 127 : 0);
            c.k[0] = (int16_t)(RND(2) ? 127 : 0);
            c.k[1] = (int16_t)(RND(2) ? 127 : 0);
            c.f[0] = (int16_t)(RND(3) - 1) * 63;
            c.f[1] = (int16_t)(RND(3) - 1) * 63;
            c.f[2] = (int16_t)(RND(3) - 1) * 63;
            for (pass = 0; pass < 2u; pass++) {
                int32_t vmax = 0;
                double sum = 0;
                uint32_t blk;
                setup(&c);
                trk[0].p[P_LEVEL] = 127;
                if (!pass) {
                    for (note_i = 0; note_i < 4u; note_i++)
                        trk_note_on(&trk[0], note_i == 0 ? 24 : note_i == 1 ? 60 : note_i == 2 ? 96 : 120, 127);
                } else {
                    trk_note_on(&trk[0], 60, 100);
                }
                for (blk = 0; blk < (pass ? FS * 3u / 2u : FS / 4u) / CTL; blk++) {
                    for (j = 0; j < CTL; j++)
                        b[j] = 0;
                    track_render(&trk[0], b, CTL);
                    if (pass && blk < FS / 2u / CTL)
                        continue;
                    for (j = 0; j < CTL; j++) {
                        int32_t a = b[j] < 0 ? -b[j] : b[j];
                        vmax = a > vmax ? a : vmax;
                        sum += b[j];
                    }
                }
                if (!pass) {
                    worst = vmax > worst ? vmax : worst;
                    over += vmax > 4 * 2 * VOICE_FS;
                    for (j = 0; j < FMT_POLY; j++) {
                        fmt_slot_t *S = (fmt_slot_t *)ENG_MEM(0) + j;
                        blow += S->env[0] < 0 || S->env[0] > (1 << 24) || S->env[1] < 0 || S->env[1] > (1 << 24) ||
                                S->fb1 > 32768 || S->fb1 < -32768 || S->ia < 0 || S->ia > 32512 || S->ib < 0 || S->ib > 32512;
                    }
                } else if (!c.e[5] && !c.f[0] && !c.f[1] && !c.f[2] && fabs(sum / FS) > dcmax) {
                    /* (detuned operators beat at a few Hz: that is signal, which a 1 s mean reads as an offset) */
                    dcmax = fabs(sum / FS);
                    worstc = c;
                }
            }
        }
        printf("fmtone:   %u corner renders, 4-voice peak %d (%.2f x VOICE_FS a voice), worst |DC| of one voice %.0f (%.2f %% of VOICE_FS)\n",
               n, worst, worst / (4.0 * VOICE_FS), dcmax, 100.0 * dcmax / VOICE_FS);
        printf("fmtone:   worst-DC config: ALGO %d RAT C/A/B %d/%d/%d HARM %d DTN %d FB %d MIX %d  LEV A/B %d/%d  FTO %d %d %d\n",
               worstc.e[0] + 1, worstc.e[1], worstc.e[2], worstc.e[3], worstc.e[4], worstc.e[5], worstc.e[6], worstc.e[7],
               worstc.a[4], worstc.b[4], worstc.f[0], worstc.f[1], worstc.f[2]);
        check("corners: no voice beyond 2 x VOICE_FS (ANALOG's nominal headroom)", over == 0);
        check("corners: the envelopes, indices and feedback stay inside their ranges", blow == 0);
        check("corners: DC of one voice held 1 s under 1 % of VOICE_FS (operators phase-locked: no DTN, no FTO)", dcmax < 0.01 * VOICE_FS);
        /* algorithm 8 with everything open: the carriers' sum within one carrier's level */
        {
            double pk = 0;
            cfg_init(&c);
            c.e[0] = 7;
            c.e[7] = 127;
            c.e[2] = 8;
            c.e[3] = 10;
            render(&c, 48, 3200, y, NFFT);
            for (i = 0; i < NFFT; i++)
                pk = fabs(y[i]) > pk ? fabs(y[i]) : pk;
            printf("fmtone:   algorithm 8, all four carriers open: peak %.0f (VOICE_FS %d x velocity)\n", pk * 32768, VOICE_FS);
            check("algorithm 8 with every carrier open: the peak within one carrier's level (+3 %)", pk * 32768 < 1.03 * VOICE_FS);
        }
    }

    /* 12. level, determinism, cost */
    {
        static double r[NFFT];
        double ref, rms;
        uint32_t pi;
        static int32_t b[CTL];
        uint32_t j, kk;
        (void)r; (void)kk; (void)j;
        ref = HS_REF_SAW_RMS;                              /* (ANALOG SAW LEAD of 1.5, measured: hostsim.c) */
        for (pi = 0; pi < ENG_FMTONE.npresets; pi++) {
            char m[120];
            double dB;
            memset(trk, 0, sizeof trk);
            host_tracks_init();
            host_preset(&trk[0], ENGI_FMTONE, pi);
            trk[0].p[P_ATK] = 0; trk[0].p[P_SUS] = 127; trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
            trk[0].p[P_ED_FLT] = 0; trk[0].p[P_VOICE] = V_POLY;
            trk_note_on(&trk[0], 60, 100);
            for (j = 0; j < 20u; j++)
                track_render(&trk[0], b, CTL);
            for (j = 0; j < NFFT / CTL; j++) {
                track_render(&trk[0], b, CTL);
                for (kk = 0; kk < CTL; kk++)
                    r[j * CTL + kk] = b[kk] / 32768.0;
            }
            for (i = 0, rms = 0; i < NFFT; i++)
                rms += r[i] * r[i];
            rms = sqrt(rms / NFFT);
            dB = 20 * log10(rms / ref);
            snprintf(m, sizeof m, "level: %-11s %+5.1f dB against ANALOG SAW LEAD, flat envelope (within -6 .. +4 dB)",
                     ENG_FMTONE.presets[pi].name, dB);
            check(m, dB > -6 && dB < 4);
        }
        cfg_init(&c);
        c.a[0] = 20; c.a[1] = 60; c.a[2] = 30; c.a[3] = 10; c.a[4] = 90;
        c.b[4] = 60; c.e[6] = 50; c.e[4] = 20; c.e[5] = 10;
        render(&c, 57, 0, y, NFFT);
        render(&c, 57, 0, y2, NFFT);
        check("a note from silence renders the same samples again (envelopes, FB, HARM, DTN on)", same(y, y2, NFFT));
    }
    {
        static int32_t b[CTL];
        const engine_t *E[4] = {&ENG_FMTONE, &ENG_ANALOG, &ENG_FM6, &ENG_SWARM};   /* (Mini-Tone compares with SID; ANALOG here) */
        double ns[4];
        uint32_t en;
        for (en = 0; en < 4u; en++) {
            struct timespec t0, t1;
            uint32_t j, blocks = FS * 4u / CTL, e = 0, q;
            for (q = 0; q < NENGINES; q++)
                if (ENGINES[q] == E[en])
                    e = q;
            memset(trk, 0, sizeof trk);
            host_tracks_init();
            host_preset(&trk[0], e, 0);
            trk[0].p[P_VOICE] = V_POLY;
            trk[0].p[P_ATK] = 0; trk[0].p[P_SUS] = 127;
            trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
            for (j = 0; j < 4u; j++)
                trk_note_on(&trk[0], 48 + 7 * j, 100);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (j = 0; j < blocks; j++)
                track_render(&trk[0], b, CTL);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            ns[en] = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / ((double)blocks * CTL * 4);
        }
        printf("fmtone:   host ns per sample and voice: FMTONE %.1f, ANALOG %.1f, FM6 %.1f, SWARM %.1f (FMTONE %.2f x FM6)\n",
               ns[0], ns[1], ns[2], ns[3], ns[0] / ns[2]);
        check("cost: FMTONE within 1.5 x FM6 per voice (four operators against six)", ns[0] < 1.5 * ns[2]);
    }

    printf("fmtone: %s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
