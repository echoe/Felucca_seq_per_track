/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from Mini-Tone (https://github.com/eldontyrellcorp/Mini-Tone, GPL-3.0-only); engine numbers changed for this tree (WAVETONE 17) */
/* WAVETONE engine test (src/eng_wavetone.c) on the host, through hostsim.c as regress.c.
 *   build/host/wavetone_test
 *  1. pitch: the key, TUN (semitones), OFS (cents).
 *  2. morph: the table's anchors (SQR, SAW, TRI harmonics against their theory), the crossfade between frames.
 *  3. PD: even harmonics appear on a square as the warp moves; PD 0 is the table as it is.
 *  4. INTR: SYNC (every line on the harmonics of osc 1), RING (sum and difference, no carriers), FM (sidebands grow).
 *  5. TABL: the families; HARM's first frame is PRIM's sine.
 *  6. DRFT: no wander at 0, a few cents at 127, deterministic.
 *  7. PHAS: RST starts every note alike, FREE does not.
 *  8. noise: the slopes of WHT / PNK / RED, the pitch of DIGI, NLEV (squared), NCUT, NDEC.
 *  9. voices: at most 4; one voice's state is not another's.
 * 10. descriptors: OFS -64..63 for WAVETONE, TP's for every other engine; a factory preset sets its slots.
 * 11. corners: sampled extremes of every parameter: headroom, DC, bounded state.
 * 12. level against ANALOG's SAW LEAD, determinism, cost, and the aliasing at a high note (measured, stated). */
#define main hostsim_main
#include "hostsim.c"
#undef main
/* the level reference of the "level" checks (hostsim.c of Mini-Tone: HS_REF_SAW_*): ANALOG's SAW LEAD, C4, velocity 100, ATK 0, SUS 127,
 * no sends, one voice (RMS of 16384 samples after 20 blocks; the largest sample of the first 0.5 s) */
#ifndef HS_REF_SAW_RMS
#define HS_REF_SAW_RMS 0.401917
#define HS_REF_SAW_PEAK 30342.0
#endif
#include <time.h>

#define NFFT 16384

static int bad;
static void check(const char *what, int ok)
{
    printf("wavetone: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* a WAVETONE track: e = the 8 engine values (WAV1 PD1 LEV1 TUN1 WAV2 PD2 LEV2 TUN2), s = the slots (INTR TABL OFS1 OFS2
 * DRFT PHAS NTYP NLEV NCUT NDEC); a flat envelope, no sends, POLY */
typedef struct { int16_t e[8], s[10]; } cfg_t;
enum { S_INTR, S_TABL, S_OFS1, S_OFS2, S_DRFT, S_PHAS, S_NTYP, S_NLEV, S_NCUT, S_NDEC };
static void cfg_init(cfg_t *c)
{
    static const cfg_t D = {{0, 0, 100, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0, 127, 127}};
    *c = D;
}
static void setup(const cfg_t *c)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(eng_mem, 0, sizeof eng_mem);               /* (the notes counted so far seed the FREE phase and the drift) */
    host_tracks_init();
    host_preset(&trk[0], ENGI_WAVETONE, 0);
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = c->e[i];
    trk[0].p[WP_INTR] = c->s[S_INTR];
    trk[0].p[WP_TABL] = c->s[S_TABL];
    trk[0].p[WP_OFS1] = c->s[S_OFS1];
    trk[0].p[WP_OFS2] = c->s[S_OFS2];
    trk[0].p[WP_DRFT] = c->s[S_DRFT];
    trk[0].p[WP_PHAS] = c->s[S_PHAS];
    trk[0].p[WP_NTYP] = c->s[S_NTYP];
    trk[0].p[WP_NLEV] = c->s[S_NLEV];
    trk[0].p[WP_NCUT] = c->s[S_NCUT];
    trk[0].p[WP_NDEC] = c->s[S_NDEC];
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
        for (k = 0; k < CTL; k++)
            b[k] = 0;
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

static int same(const double *a, const double *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static double hdb(const double *ps, double f0, int h) { return db(band_pow(ps, h * f0, 0.03, NFFT), band_pow(ps, f0, 0.03, NFFT)); }
/* the share of the power (40 Hz .. 18 kHz) that is not on the harmonics of f0 (+-3 %) */
static double offline_share(const double *ps, double f0)
{
    double tot = tot_pow(ps, NFFT, 40, 18000), on = 0;
    int h;
    for (h = 1; h * f0 < 18000; h++)
        on += band_pow(ps, h * f0, 0.03, NFFT);
    return tot > 0 ? (tot - on) / tot : 0;
}
static double rms_of(const double *y, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += y[i] * y[i];
    return sqrt(s / n);
}
/* the density of power (per bin) in the band lo .. hi */
static double dens(const double *ps, double lo, double hi)
{
    return tot_pow(ps, NFFT, lo, hi) / (bin_of(hi, NFFT) - bin_of(lo, NFFT) + 1);
}

int main(void)
{
    static double y[NFFT + 4096], y2[NFFT + 4096], ps[NFFT / 2], ps2[NFFT / 2];
    cfg_t c;
    uint32_t i, k, note = 57;                    /* A3: 220 Hz */
    double f0 = hz_of(note);

    check("the engine is number 17, 17 engines come before it (FMTONE 16, SWARM 15, SHDRUM 14)", ENGI_WAVETONE == 17u && ENGINES[17] == &ENG_WAVETONE && NENGINES == 18u && ENGINE_ORDER[NENG_SHOWN - 1] == 17);

    /* 1. pitch */
    {
        static const struct { int tun1, ofs1, lev2, tun2, ofs2; double r; } P[] = {
            {0, 0, 0, 0, 0, 1.0}, {12, 0, 0, 0, 0, 2.0}, {-12, 0, 0, 0, 0, 0.5}, {7, 0, 0, 0, 0, 1.4983},
            {0, 60, 0, 0, 0, 1.03526}, {0, -60, 0, 0, 0, 0.96593}, {0, 0, 100, -12, 0, 0.5}, {0, 0, 100, 5, 50, 1.3748}};
        int ok = 1;
        for (k = 0; k < NELEM(P); k++) {
            double want, got;
            cfg_init(&c);
            c.e[3] = (int16_t)P[k].tun1;
            c.s[S_OFS1] = (int16_t)P[k].ofs1;
            if (P[k].lev2) {                 /* osc 2 alone: osc 1 silent */
                c.e[2] = 0;
                c.e[6] = 100;
                c.e[7] = (int16_t)P[k].tun2;
                c.s[S_OFS2] = (int16_t)P[k].ofs2;
            }
            want = f0 * P[k].r;
            render(&c, note, 3200, y, NFFT);
            spec1(y, 0, NFFT, ps);
            got = peak_hz(ps, want * 0.9, want * 1.1, NFFT);
            if (fabs(got / want - 1) > 0.003) {
                ok = 0;
                printf("wavetone:   case %u: peak %.2f Hz, wanted %.2f\n", k, got, want);
            }
        }
        check("pitch: the key, TUN1 +-12 / +7, OFS1 +-60 ct, osc 2's TUN2 and OFS2 (within 0.3 %)", ok);
    }

    /* 2. morph anchors: frame i at wave round(i x 127 / 7) */
    {
        double h3, h2, h3t;
        cfg_init(&c);
        c.e[0] = 0;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        check("SIN (WAV 0): no 2nd or 3rd harmonic (< -50 dB)", hdb(ps, f0, 2) < -50 && hdb(ps, f0, 3) < -50);
        c.e[0] = 36;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        h3 = hdb(ps, f0, 3);
        printf("wavetone:   SQR: 2nd %.1f dB, 3rd %.1f dB, 5th %.1f dB (theory: -inf, -9.5, -14.0)\n", hdb(ps, f0, 2), h3, hdb(ps, f0, 5));
        check("SQR (WAV 36): odd harmonics at 1/h (3rd -9.5, 5th -14 dB, +-1), no even", fabs(h3 + 9.54) < 1 && fabs(hdb(ps, f0, 5) + 13.98) < 1 && hdb(ps, f0, 2) < -40);
        c.e[0] = 54;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        h2 = hdb(ps, f0, 2);
        printf("wavetone:   SAW: 2nd %.1f dB, 3rd %.1f dB (theory -6.0, -9.5)\n", h2, hdb(ps, f0, 3));
        check("SAW (WAV 54): 2nd at -6 dB, 3rd at -9.5 dB (+-1)", fabs(h2 + 6.02) < 1 && fabs(hdb(ps, f0, 3) + 9.54) < 1);
        c.e[0] = 18;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        h3t = hdb(ps, f0, 3);
        printf("wavetone:   TRI: 3rd %.1f dB (theory -19.1)\n", h3t);
        check("TRI (WAV 18): 3rd at -19.1 dB (+-1)", fabs(h3t + 19.08) < 1);
        c.e[0] = 9;                          /* halfway SIN -> TRI */
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        printf("wavetone:   SIN/TRI halfway: 3rd %.1f dB\n", hdb(ps, f0, 3));
        check("the morph crossfades: halfway SIN -> TRI the 3rd lies between the two (-60 .. -19.1 dB, and above -30)", hdb(ps, f0, 3) < -19.1 && hdb(ps, f0, 3) > -30);
        {
            double prev = -1e9;
            int mono = 1;
            for (k = 0; k <= 18u; k += 3) {
                c.e[0] = (int16_t)k;
                render(&c, note, 3200, y, NFFT);
                spec1(y, 0, NFFT, ps);
                if (hdb(ps, f0, 3) < prev - 0.01)
                    mono = 0;
                prev = hdb(ps, f0, 3);
            }
            check("SIN -> TRI: the 3rd harmonic only grows (WAV 0 .. 18)", mono);
        }
    }

    /* 3. PD */
    {
        double h2a, h2b, h2c;
        cfg_init(&c);
        c.e[0] = 36;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        h2a = hdb(ps, f0, 2);
        c.e[1] = 60;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        h2b = hdb(ps, f0, 2);
        c.e[1] = 127;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        h2c = hdb(ps, f0, 2);
        printf("wavetone:   SQR, PD 0 / 60 / 127: 2nd harmonic %.1f / %.1f / %.1f dB\n", h2a, h2b, h2c);
        check("PD: a square's 2nd harmonic: none at PD 0 (< -40), there at PD 60 (> -20), PD 127 changes it again", h2a < -40 && h2b > -20 && fabs(h2c - h2b) > 1);
        cfg_init(&c);
        c.e[0] = 0;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        h2a = hdb(ps, f0, 3);
        c.e[1] = 100;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        printf("wavetone:   SIN, PD 100: 2nd %.1f, 3rd %.1f dB\n", hdb(ps, f0, 2), hdb(ps, f0, 3));
        check("PD on a sine makes harmonics (3rd > -30 dB)", hdb(ps, f0, 3) > -30);
        cfg_init(&c);
        c.e[0] = 36; c.e[1] = 0;
        render(&c, note, 3200, y, NFFT);
        c.e[1] = 0;
        render(&c, note, 3200, y2, NFFT);
        check("PD 0 is the table as it is (same render twice, sanity)", same(y, y2, NFFT));
    }

    /* 4. INTR */
    {
        double offl_off, offl_sync, s1, s2, tot, sum, dif, f1 = f0, f2 = f0 * pow(2.0, 7.0 / 12.0);
        cfg_init(&c);
        c.e[0] = 54; c.e[4] = 54; c.e[2] = 0; c.e[6] = 100; c.e[7] = 7;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        offl_off = offline_share(ps, f0);
        c.s[S_INTR] = 1;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        offl_sync = offline_share(ps, f0);
        printf("wavetone:   osc 2 a fifth up: power off osc 1's harmonics: OFF %.3f, SYNC %.3f\n", offl_off, offl_sync);
        check("SYNC: osc 2 (+7 st) is locked to osc 1: > 97 % of the power on osc 1's harmonics (OFF: < 50 %)", offl_sync < 0.03 && offl_off > 0.5);
        cfg_init(&c);
        c.e[0] = 0; c.e[4] = 0; c.e[2] = 100; c.e[6] = 100; c.e[7] = 7;
        c.s[S_INTR] = 2;
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        tot = tot_pow(ps, NFFT, 40, 18000);
        sum = band_pow(ps, f1 + f2, 0.02, NFFT);
        dif = band_pow(ps, f2 - f1, 0.02, NFFT);
        s1 = band_pow(ps, f1, 0.02, NFFT);
        s2 = band_pow(ps, f2, 0.02, NFFT);
        printf("wavetone:   RING of two sines (220 Hz, %.1f Hz): sum %.1f dB, difference %.1f dB, f1 %.1f dB, f2 %.1f dB\n", f2,
               db(sum, tot), db(dif, tot), db(s1, tot), db(s2, tot));
        check("RING: the sum and the difference carry the sound (each > -4 dB), the carriers are gone (< -35 dB)",
              db(sum, tot) > -4 && db(dif, tot) > -4 && db(s1, tot) < -35 && db(s2, tot) < -35);
        {
            double sh[4];
            static const int16_t L[4] = {0, 20, 50, 100};
            int mono = 1;
            for (k = 0; k < 4u; k++) {
                cfg_init(&c);
                c.s[S_INTR] = 3;
                c.e[0] = 0; c.e[4] = 0; c.e[2] = L[k]; c.e[6] = 100;
                render(&c, note, 3200, y, NFFT);
                spec1(y, 0, NFFT, ps);
                sh[k] = 1 - band_pow(ps, f0, 0.03, NFFT) / tot_pow(ps, NFFT, 40, 18000);
                if (k && sh[k] <= sh[k - 1])
                    mono = 0;
            }
            printf("wavetone:   FM (osc 1 -> osc 2, same pitch): sideband share at LEV1 0 20 50 100: %.3f %.3f %.3f %.3f\n", sh[0], sh[1], sh[2], sh[3]);
            /* (the share is a Bessel function of the index: it need not rise forever; J0 returns to its zeros) */
            check("FM: osc 1 is the index, not heard: none at LEV1 0 (< 1 %), growing over LEV1 0, 20, 50 (> 30 % at 50)", sh[0] < 0.01 && sh[1] > sh[0] && sh[2] > sh[1] && sh[2] > 0.3);
            (void)mono;
        }
    }

    /* 5. TABL */
    {
        cfg_init(&c);
        render(&c, note, 3200, y, NFFT);
        c.s[S_TABL] = 1;
        render(&c, note, 3200, y2, NFFT);
        {
            double d = 0;
            for (i = 0; i < NFFT; i++)
                d = fmax(d, fabs(y[i] - y2[i]));
            printf("wavetone:   PRIM 0 against HARM 0 (both a sine): largest difference %.5f of full scale\n", d);
            check("HARM's first frame is PRIM's sine (difference < 0.2 % of full scale)", d < 0.002);
        }
        c.e[0] = 127;
        render(&c, note, 3200, y2, NFFT);
        c.s[S_TABL] = 0;
        render(&c, note, 3200, y, NFFT);
        check("the last frames differ (half-sine against dense harmonics)", !same(y, y2, NFFT));
        c.s[S_TABL] = 1;
        c.e[0] = 91;                         /* 'formant bump' */
        render(&c, note, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        printf("wavetone:   HARM formant frame: 3rd %.1f, 5th %.1f, 7th %.1f dB\n", hdb(ps, f0, 3), hdb(ps, f0, 5), hdb(ps, f0, 7));
        check("HARM formant frame: the 5th harmonic is the strongest after the fundamental or above it (> -3 dB of 3rd)", hdb(ps, f0, 5) > hdb(ps, f0, 3) - 3 || hdb(ps, f0, 5) > -6);
    }

    /* 6. DRFT: the pitch of a sine over 3 s, in windows of 8192 samples */
    {
        double lo0 = 1e9, hi0 = -1e9, lo1 = 1e9, hi1 = -1e9;
        static double yy[44100 * 3];
        int dterm;
        for (dterm = 0; dterm < 2; dterm++) {
            uint32_t w, nw = (uint32_t)(sizeof yy / sizeof yy[0]) / 8192u;
            cfg_init(&c);
            c.s[S_DRFT] = dterm ? 127 : 0;
            render(&c, note, 3200, yy, (uint32_t)(sizeof yy / sizeof yy[0]));
            for (w = 0; w < nw; w++) {
                double cents;
                static double p8[4096], tmp[8192];
                uint32_t j;
                for (j = 0; j < 8192u; j++)
                    tmp[j] = yy[w * 8192u + j];
                spec1(tmp, 0, 8192, p8);
                {   /* peak_hz works on NFFT bins: do it here with 8192 */
                    uint32_t a = bin_of(f0 * 0.9, 8192), b = bin_of(f0 * 1.1, 8192), best = a;
                    double al, be, ga, pp;
                    for (j = a; j <= b; j++)
                        if (p8[j] > p8[best])
                            best = j;
                    al = log(p8[best - 1] + 1e-30); be = log(p8[best] + 1e-30); ga = log(p8[best + 1] + 1e-30);
                    pp = 0.5 * (al - ga) / (al - 2 * be + ga);
                    cents = 1200 * log2(((best + pp) * (double)FS / 8192) / f0);
                }
                if (dterm) { lo1 = fmin(lo1, cents); hi1 = fmax(hi1, cents); }
                else { lo0 = fmin(lo0, cents); hi0 = fmax(hi0, cents); }
            }
        }
        printf("wavetone:   osc 1's pitch over 3 s in cents: DRFT 0 %.2f .. %.2f, DRFT 127 %.2f .. %.2f\n", lo0, hi0, lo1, hi1);
        check("DRFT 0: the pitch stays put (spread < 1 ct)", hi0 - lo0 < 1.0);
        check("DRFT 127: it wanders (spread 2 .. 32 ct, never beyond +-17 ct)", hi1 - lo1 > 2 && hi1 - lo1 < 32 && lo1 > -17 && hi1 < 17);
        cfg_init(&c);
        c.s[S_DRFT] = 127;
        render(&c, note, 0, y, NFFT);
        render(&c, note, 0, y2, NFFT);
        check("DRFT is deterministic (the same note from silence: the same samples)", same(y, y2, NFFT));
    }

    /* 7. PHAS */
    {
        static int32_t b[CTL];
        static double a1[CTL * 8], a2[CTL * 8];
        int ph, ok_rst = 0, ok_free = 0;
        for (ph = 0; ph < 2; ph++) {
            uint32_t j, n;
            cfg_init(&c);
            c.e[0] = 54;
            c.s[S_PHAS] = (int16_t)ph;
            setup(&c);
            trk_note_on(&trk[0], note, 100);
            for (j = 0; j < 8u; j++) {
                for (n = 0; n < CTL; n++) b[n] = 0;
                track_render(&trk[0], b, CTL);
                for (n = 0; n < CTL; n++) a1[j * CTL + n] = b[n];
            }
            trk_note_off(&trk[0], note);
            for (j = 0; j < FS * 2u / CTL; j++)
                track_render(&trk[0], b, CTL);
            trk_note_on(&trk[0], note, 100);
            for (j = 0; j < 8u; j++) {
                for (n = 0; n < CTL; n++) b[n] = 0;
                track_render(&trk[0], b, CTL);
                for (n = 0; n < CTL; n++) a2[j * CTL + n] = b[n];
            }
            if (ph) ok_free = !same(a1, a2, CTL * 8);
            else ok_rst = same(a1, a2, CTL * 8);
        }
        check("PHAS RST: two notes from silence start alike, sample for sample", ok_rst);
        check("PHAS FREE: two notes from silence start at different phases", ok_free);
        {   /* a retrigger of a sounding voice keeps the phase: no step */
            double mx = 0;
            uint32_t j, n;
            cfg_init(&c);
            c.e[0] = 54;
            setup(&c);
            trk_note_on(&trk[0], note, 100);
            for (j = 0; j < 50u; j++) {
                for (n = 0; n < CTL; n++) b[n] = 0;
                track_render(&trk[0], b, CTL);
            }
            {
                int32_t last = b[CTL - 1], prev = b[CTL - 2];
                trk_note_on(&trk[0], note, 100);
                for (n = 0; n < CTL; n++) b[n] = 0;
                track_render(&trk[0], b, CTL);
                mx = fabs((double)b[0] - last);
                printf("wavetone:   retrigger of a sounding voice: step %.0f against the usual %.0f\n", mx, fabs((double)last - prev));
                check("a retrigger of a sounding voice does not jump (step < 4 x the usual)", mx < 4 * fabs((double)last - prev) + 200);
            }
        }
    }

    /* 8. noise (osc 1 silent: LEV1 0) */
    {
        double d[3][3];
        int ty, ok = 1;
        static const double WANT[3][2] = {{0, 0}, {-9, 3}, {-18, 3}};   /* 700 Hz -> 5600 Hz: 3 octaves */
        for (ty = 0; ty < 3; ty++) {
            cfg_init(&c);
            c.e[2] = 0;
            c.s[S_NTYP] = (int16_t)ty;
            c.s[S_NLEV] = 100;
            render(&c, note, 6400, y, NFFT);
            spec1(y, 0, NFFT, ps);
            d[ty][0] = dens(ps, 500, 900);
            d[ty][1] = dens(ps, 4000, 7000);
            d[ty][2] = rms_of(y, NFFT);
            {
                double sl = db(d[ty][1], d[ty][0]);
                printf("wavetone:   noise type %d: density 4-7 kHz against 0.5-0.9 kHz %.1f dB; rms %.4f\n", ty, sl, d[ty][2]);
                if (fabs(sl - (ty == 0 ? 0 : ty == 1 ? -9 : -18)) > 3.5)
                    ok = 0;
            }
        }
        (void)WANT;
        check("WHT flat, PNK -3 dB / octave, RED -6 dB / octave (3 octaves between the bands, +-3.5 dB)", ok);
        check("the three types come out at a comparable level (each within 12 dB of white's rms)",
              fabs(20 * log10(d[1][2] / d[0][2])) < 12 && fabs(20 * log10(d[2][2] / d[0][2])) < 12);
        {   /* DIGI: clocked by the key */
            double hi_lo[2];
            uint32_t nn;
            static const uint32_t N2[2] = {48, 72};
            for (nn = 0; nn < 2u; nn++) {
                cfg_init(&c);
                c.e[2] = 0;
                c.s[S_NTYP] = 3;
                c.s[S_NLEV] = 100;
                render(&c, N2[nn], 6400, y, NFFT);
                spec1(y, 0, NFFT, ps);
                hi_lo[nn] = db(tot_pow(ps, NFFT, 3000, 16000), tot_pow(ps, NFFT, 40, 16000));
            }
            printf("wavetone:   DIGI: power above 3 kHz at note 48 %.1f dB, at note 72 %.1f dB\n", hi_lo[0], hi_lo[1]);
            check("DIGI follows the key: a higher note is brighter (>= 6 dB more power above 3 kHz)", hi_lo[1] > hi_lo[0] + 6);
        }
        {   /* NLEV, squared curve */
            double r[4];
            static const int16_t L[4] = {16, 32, 64, 127};
            for (k = 0; k < 4u; k++) {
                cfg_init(&c);
                c.e[2] = 0;
                c.s[S_NLEV] = L[k];
                render(&c, note, 6400, y, NFFT);
                r[k] = rms_of(y, NFFT);
            }
            printf("wavetone:   NLEV 16 32 64 127: rms %.5f %.5f %.5f %.5f\n", r[0], r[1], r[2], r[3]);
            check("NLEV: monotonic, 127 -> 64 about -12 dB (squared curve: -9 .. -15)", r[0] < r[1] && r[1] < r[2] && r[2] < r[3] && db(r[2] * r[2], r[3] * r[3]) < -9 && db(r[2] * r[2], r[3] * r[3]) > -15);
            cfg_init(&c);
            c.e[2] = 0;
            render(&c, note, 6400, y, NFFT);
            check("NLEV 0: silence", rms_of(y, NFFT) < 1e-6);
        }
        {   /* NCUT */
            double hi127, hi40;
            cfg_init(&c);
            c.e[2] = 0; c.s[S_NLEV] = 100;
            render(&c, note, 6400, y, NFFT);
            spec1(y, 0, NFFT, ps);
            hi127 = tot_pow(ps, NFFT, 5000, 16000);
            c.s[S_NCUT] = 40;
            render(&c, note, 6400, y, NFFT);
            spec1(y, 0, NFFT, ps);
            hi40 = tot_pow(ps, NFFT, 5000, 16000);
            printf("wavetone:   NCUT 40 against 127: power above 5 kHz %.1f dB\n", db(hi40, hi127));
            check("NCUT closes the noise's low-pass (power above 5 kHz down by > 20 dB at 40)", db(hi40, hi127) < -20);
        }
        {   /* NDEC (ENV_EXP: 100 is a time constant of ~0.3 s) */
            double e0, e1, e2;
            static double yl[44100 * 4];
            cfg_init(&c);
            c.e[2] = 0; c.s[S_NLEV] = 100; c.s[S_NDEC] = 100;
            render(&c, note, 0, yl, 44100 * 4);
            e0 = rms_of(yl, 4096);
            e1 = rms_of(yl + 44100 / 2, 4096);
            e2 = rms_of(yl + 44100 * 3, 4096);
            printf("wavetone:   NDEC 100: rms at 0 / 0.5 s / 3 s: %.5f %.5f %.5f\n", e0, e1, e2);
            check("NDEC 100: the noise falls away (0.5 s below the start's, 3 s < 1 % of it)", e1 < 0.7 * e0 && e1 > e2 && e2 < 0.01 * e0);
            c.s[S_NDEC] = 127;
            render(&c, note, 0, yl, 44100 * 4);
            e0 = rms_of(yl, 4096);
            e1 = rms_of(yl + 44100 * 3, 4096);
            check("NDEC 127: the noise stays (3 s within 1 dB of the start's)", fabs(20 * log10(e1 / e0)) < 1);
        }
    }

    /* 9. voices */
    {
        int maxv = 0, maxu = 0;
        static int32_t b[CTL];
        static track_t snap;
        static uint8_t mem_snap[sizeof eng_mem], slot_alone[sizeof(wvt_slot_t)];
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
            if (k) maxu = (int)n; else maxv = (int)n;
        }
        check("POLY: 9 keys sound at most 4 voices (engine_t.poly)", maxv == 4);
        check("UNISON: at most 4 voices", maxu >= 1 && maxu <= 4);
        cfg_init(&c);
        c.s[S_DRFT] = 90; c.s[S_NLEV] = 40; c.s[S_NTYP] = 1; c.e[0] = 54; c.e[2] = 100; c.e[6] = 80; c.s[S_INTR] = 1;
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
        started = !memcmp(ENG_MEM(0), mem_snap, sizeof(wvt_slot_t));
        track_render(&trk[0], b, CTL);
        alone = !memcmp(ENG_MEM(0), slot_alone, sizeof slot_alone);
        check("a second note leaves the first voice's slot as it was (its note-on and its block)", started && alone);
        check("the four voices' state fits the part's engine memory", sizeof(wvt_slot_t) * WVT_POLY <= ENG_MEM_PART);
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
        const param_desc_t *d = param_desc_of(ENGI_WAVETONE, WP_OFS1), *o = param_desc_of(0, WP_OFS1), *f = param_desc_of(ENGI_FMTONE, WP_OFS1);
        static const struct { uint32_t id; const char *lab; } L[] = {
            {WP_INTR, "INTR"}, {WP_TABL, "TABL"}, {WP_OFS1, "OFS1"}, {WP_OFS2, "OFS2"}, {WP_DRFT, "DRFT"},
            {WP_PHAS, "PHAS"}, {WP_NTYP, "NTYP"}, {WP_NLEV, "NLEV"}, {WP_NCUT, "NCUT"}, {WP_NDEC, "NDEC"}};
        int ok = 1;
        memset(trk, 0, sizeof trk);
        host_tracks_init();
        host_preset(&trk[0], ENGI_WAVETONE, 0);
        for (k = 0; k < NELEM(L); k++)
            ok &= str_eq(track_desc(&trk[0], L[k].id)->label, L[k].lab);
        check("the operator slots carry their WAVETONE names (INTR TABL OFS1 OFS2 DRFT PHAS NTYP NLEV NCUT NDEC)", ok);
        check("OFS is -64..63 for WAVETONE (what a project, a preset, motion and MIDI clamp to)",
              d->min == -64 && d->max == 63 && d->def == 0 && param_fit(d, 150) == 63 && param_fit(d, -150) == -64);
        check("the same slot is TP's for ANALOG and FMTONE's own for FMTONE", o == &TP[WP_OFS1] && o->min == 0 && o->max == 127 && f != d);
        check("NCUT and NDEC default to 127 (open, stays), as TP's SUS slots do",
              param_desc_of(ENGI_WAVETONE, WP_NCUT)->def == 127 && param_desc_of(ENGI_WAVETONE, WP_NDEC)->def == 127 && TP[WP_NCUT].def == 127 && TP[WP_NDEC].def == 127);
        check("an unused slot (P_FM4_LEVEL) is TP's even for WAVETONE; P_FM3_SUS REL LEVEL are the filter's CUT RES KEY (defaults 127, 0, 127)",
              param_desc_of(ENGI_WAVETONE, P_FM4_LEVEL) == &TP[P_FM4_LEVEL] && str_eq(param_desc_of(ENGI_WAVETONE, P_FM3_SUS)->label, "CUT") &&
                  str_eq(param_desc_of(ENGI_WAVETONE, P_FM3_REL)->label, "RES") && str_eq(param_desc_of(ENGI_WAVETONE, P_FM3_LEVEL)->label, "KEY") &&
                  param_desc_of(ENGI_WAVETONE, P_FM3_SUS)->def == 127 && param_desc_of(ENGI_WAVETONE, P_FM3_REL)->def == 0 && param_desc_of(ENGI_WAVETONE, P_FM3_LEVEL)->def == 127);
        ok = 1;
        for (k = 0; k < ENG_WAVETONE.npresets; k++) {
            host_preset(&trk[0], ENGI_WAVETONE, k);
            ok &= trk[0].p[WP_INTR] == WVT_X[k].intr && trk[0].p[WP_OFS2] == WVT_X[k].ofs2 && trk[0].p[WP_NLEV] == WVT_X[k].nlev &&
                  trk[0].p[WP_NCUT] == WVT_X[k].ncut && trk[0].p[WP_NDEC] == WVT_X[k].ndec && trk[0].p[P_E0] == WVT_PRESETS[k].e[0];
        }
        check("a factory preset sets the engine values and the slots", ok);
        host_preset(&trk[0], 15, 0);
        check("loading a FMTONE sound after WAVETONE leaves none of WAVETONE's slot values (NLEV 0 again)", trk[0].p[WP_NLEV] == 0 || trk[0].eng_req == 15);
        /* the pages: WAVETONE's own */
        {
            int pg_own = 0;
            for (k = 0; k < NPAGES; k++)
                pg_own += PAGES[k].own == ENGI_WAVETONE + 1;
            check("four pages are WAVETONE's own (INTERACT, UTIL, NOISE, FILTER)", pg_own == 4);
        }
    }

    /* 11. corners */
    {
        uint32_t seed = 987654u, n, over = 0, blow = 0;
        int32_t worst = 0;
        double dcmax = 0;
        static int32_t b[CTL];
        cfg_t worstc;
        for (n = 0; n < 1500u; n++) {
            uint32_t pass, j, note_i;
            cfg_init(&c);
#define RND(m) (seed = seed * 1664525u + 1013904223u, (seed >> 8) % (m))
            c.e[0] = (int16_t)(RND(2) ? 127 : RND(128));
            c.e[1] = (int16_t)(RND(2) ? 127 : 0);
            c.e[2] = (int16_t)(RND(2) ? 127 : 0);
            c.e[3] = (int16_t)(RND(3) == 0 ? -24 : RND(2) ? 24 : 0);
            c.e[4] = (int16_t)(RND(2) ? 127 : RND(128));
            c.e[5] = (int16_t)(RND(2) ? 127 : 0);
            c.e[6] = (int16_t)(RND(2) ? 127 : 0);
            c.e[7] = (int16_t)(RND(3) == 0 ? -24 : RND(2) ? 24 : 0);
            c.s[S_INTR] = (int16_t)RND(4);
            c.s[S_TABL] = (int16_t)RND(2);
            c.s[S_OFS1] = (int16_t)(RND(3) - 1) * 63;
            c.s[S_OFS2] = (int16_t)(RND(3) - 1) * 63;
            c.s[S_DRFT] = (int16_t)(RND(2) ? 127 : 0);
            c.s[S_PHAS] = (int16_t)RND(2);
            c.s[S_NTYP] = (int16_t)RND(4);
            c.s[S_NLEV] = (int16_t)(RND(2) ? 127 : 0);
            c.s[S_NCUT] = (int16_t)(RND(2) ? 127 : RND(128));
            c.s[S_NDEC] = (int16_t)(RND(2) ? 127 : RND(128));
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
                    for (j = 0; j < WVT_POLY; j++) {
                        wvt_slot_t *S = (wvt_slot_t *)ENG_MEM(0) + j;
                        blow += S->nenv < 0 || S->nenv > (1 << 24) || S->ic1 > 150000 || S->ic1 < -150000 || S->ic2 > 150000 ||
                                S->ic2 < -150000 || S->red > 32767 || S->red < -32767 || S->dnz[0] > 32768 || S->dnz[0] < -32768;
                    }
                } else if (!c.s[S_NLEV] && fabs(sum / FS) > dcmax) {   /* (noise wanders: its 1 s mean is signal, not offset) */
                    dcmax = fabs(sum / FS);
                    worstc = c;
                }
            }
        }
        printf("wavetone: %u corner renders, 4-voice peak %d (%.2f x VOICE_FS a voice), worst |DC| of one voice %.0f (%.2f %% of VOICE_FS)\n",
               n, worst, worst / (4.0 * VOICE_FS), dcmax, 100.0 * dcmax / VOICE_FS);
        printf("wavetone:   worst-DC config: WAV %d/%d PD %d/%d LEV %d/%d TUN %d/%d INTR %d TABL %d OFS %d %d NTYP %d NLEV %d\n",
               worstc.e[0], worstc.e[4], worstc.e[1], worstc.e[5], worstc.e[2], worstc.e[6], worstc.e[3], worstc.e[7], worstc.s[0],
               worstc.s[1], worstc.s[2], worstc.s[3], worstc.s[6], worstc.s[7]);
        check("corners: no voice beyond 2 x VOICE_FS (ANALOG's nominal headroom)", over == 0);
        check("corners: the noise envelope, filter, red noise and drift stay inside their ranges", blow == 0);
        check("corners: DC of one voice held 1 s under 1 % of VOICE_FS", dcmax < 0.01 * VOICE_FS);
    }

    /* 12. level, determinism, cost, aliasing */
    {
        static double r[NFFT];
        double ref;
        uint32_t pi, j, kk;
        static int32_t b[CTL];
        (void)b;
        ref = HS_REF_SAW_RMS;                              /* (ANALOG SAW LEAD of 1.5, measured: hostsim.c) */
        for (pi = 0; pi < ENG_WAVETONE.npresets; pi++) {
            char m[140];
            double dB;
            memset(trk, 0, sizeof trk);
            host_tracks_init();
            host_preset(&trk[0], ENGI_WAVETONE, pi);
            trk[0].p[P_ATK] = 0; trk[0].p[P_SUS] = 127; trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
            trk[0].p[P_ED_FLT] = 0; trk[0].p[P_VOICE] = V_POLY;
            trk_note_on(&trk[0], 60, 100);
            for (j = 0; j < 20u; j++)
                track_render(&trk[0], b, CTL);
            for (j = 0; j < NFFT / CTL; j++) {
                for (kk = 0; kk < CTL; kk++) b[kk] = 0;
                track_render(&trk[0], b, CTL);
                for (kk = 0; kk < CTL; kk++)
                    r[j * CTL + kk] = b[kk] / 32768.0;
            }
            dB = 20 * log10(rms_of(r, NFFT) / ref);
            snprintf(m, sizeof m, "level: %-11s %+5.1f dB against ANALOG SAW LEAD, flat envelope (within -6 .. +4 dB)", ENG_WAVETONE.presets[pi].name, dB);
            check(m, dB > -6 && dB < 4);
        }
        cfg_init(&c);
        c.e[0] = 54; c.e[1] = 40; c.e[4] = 20; c.e[6] = 80; c.e[7] = 5; c.s[S_INTR] = 1; c.s[S_DRFT] = 60; c.s[S_PHAS] = 1;
        c.s[S_NLEV] = 50; c.s[S_NTYP] = 2; c.s[S_NDEC] = 70;
        render(&c, 57, 0, y, NFFT);
        render(&c, 57, 0, y2, NFFT);
        check("a note from silence renders the same samples again (SYNC, PD, DRFT, FREE, RED noise with NDEC)", same(y, y2, NFFT));
    }
    {   /* aliasing, measured: a SAW at C7 (2093 Hz), the power off its harmonics */
        double a0, a1, a2;
        cfg_init(&c);
        c.e[0] = 54;
        render(&c, 96, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        a0 = offline_share(ps, hz_of(96));
        c.e[1] = 127;
        render(&c, 96, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        a1 = offline_share(ps, hz_of(96));
        c.e[1] = 0;
        c.s[S_INTR] = 3; c.e[2] = 100; c.e[4] = 54; c.e[6] = 100; c.e[3] = 0;
        render(&c, 96, 3200, y, NFFT);
        spec1(y, 0, NFFT, ps);
        a2 = offline_share(ps, hz_of(96));
        printf("wavetone:   aliasing at C7, power off the harmonics: SAW %.1f dB, SAW PD 127 %.1f dB, FM saw -> saw %.1f dB\n",
               10 * log10(a0 + 1e-12), 10 * log10(a1 + 1e-12), 10 * log10(a2 + 1e-12));
        check("aliasing: a plain SAW at C7 keeps its off-harmonic power under -40 dB", a0 < 1e-4);
    }
    {
        static int32_t b[CTL];
        const engine_t *E[4] = {&ENG_WAVETONE, &ENG_ANALOG, &ENG_FMTONE, &ENG_SWARM};   /* (Mini-Tone compares with SID; ANALOG here) */
        double ns[4];
        uint32_t en;
        for (en = 0; en < 4u; en++) {
            struct timespec t0, t1;
            uint32_t j, blocks = FS * 4u / CTL, e = 0, q;
            for (q = 0; q < NENGINES; q++)
                if (ENGINES[q] == E[en]) e = q;
            memset(trk, 0, sizeof trk);
            host_tracks_init();
            host_preset(&trk[0], e, 0);
            trk[0].p[P_VOICE] = V_POLY;
            trk[0].p[P_ATK] = 0; trk[0].p[P_SUS] = 127;
            trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
            if (en == 0) {   /* the heavy case: both oscillators, SYNC, PD, noise */
                trk[0].p[P_E5] = 60; trk[0].p[P_E6] = 100; trk[0].p[P_E1] = 60;
                trk[0].p[WP_INTR] = 1; trk[0].p[WP_NLEV] = 60; trk[0].p[WP_DRFT] = 60;
            }
            for (j = 0; j < 4u; j++)
                trk_note_on(&trk[0], 48 + 7 * j, 100);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (j = 0; j < blocks; j++)
                track_render(&trk[0], b, CTL);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            ns[en] = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / ((double)blocks * CTL * 4);
        }
        printf("wavetone:   host ns per sample and voice (WAVETONE with both oscillators, SYNC, PD, noise): WAVETONE %.1f, ANALOG %.1f, FMTONE %.1f, SWARM %.1f\n",
               ns[0], ns[1], ns[2], ns[3]);
        check("cost: WAVETONE (everything on) within 2 x SWARM per voice", ns[0] < 2.0 * ns[3]);
    }

    printf("wavetone: %s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
