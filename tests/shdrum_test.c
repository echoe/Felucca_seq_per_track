/* SPDX-License-Identifier: GPL-3.0-only */
/* SHDRUM test (src/eng_shdrum.c) through hostsim.c, as drum_test.c.
 *   build/host/shdrum_test [DEMODIR]
 * 1. every factory preset: one hit, peak under full scale, DC under 2 % of the peak, the voice frees itself, silent at
 *    the end, the same samples when struck again (hash), and it sounds (RMS over 0..0.2 s).
 * 2. controls: LEVEL A / LEVEL B 0 silence their layer; WAVE, XMOD, DPTH, TUN B, SWEP, each effect change the sound; the
 *    sweep lowers the pitch over the hit (SH KICK ends near its key); a longer A DEC rings longer; XMOD MIX with DPTH
 *    changes nothing; FM with LVL B 0 differs from MIX.
 * 3. corners: every parameter at its extremes, in every XMOD and filter type: bounded, no DC runaway, ends.
 * 4. SHDRUM keeps the extras to itself: another engine's render is unchanged by them; descriptors in range.
 * 5. demos into DEMODIR. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
#define NB 256                          /* blocks per hit: 0.19 s * ... 256 * 32 / 44100 = 0.186 s */
#define NMAXB 1400                      /* 1.0 s */

static uint64_t fnv(uint64_t h, int32_t v) { return (h ^ (uint32_t)v) * 1099511628211ull; }

typedef struct { double rms, peak, dc, t30; uint64_t hash; uint32_t live_end; int32_t tail; double zc; } stat_t;

static void fresh(uint32_t pi)
{
    uint32_t k;
    host_tracks_init();
    vage = 0;                            /* the voice clock (it seeds the noise): each case starts alike */
    for (k = 0; k < NTRK; k++) {
        trk[k].v[0].active = 0;
        memset(trk[k].v, 0, sizeof trk[k].v);
    }
    host_preset(&trk[0], ENGI_SHDRUM, pi);
}

/* one hit of note at vel from the current trk[0] state; nb blocks; buf (optional) gets the samples */
static stat_t hit(uint32_t note, uint32_t nb, int16_t *buf)
{
    stat_t s = {0};
    int32_t o[CTL];
    uint32_t i, k, n = 0, last = 0;
    double e = 0, sum = 0, env[NMAXB];
    int32_t prev = 0;
    s.hash = 1469598103934665603ull;
    for (k = 0; k < NTRK; k++) memset(trk[k].v, 0, sizeof trk[k].v);
    trk_note_on(&trk[0], note, 110);
    for (i = 0; i < nb && i < NMAXB; i++) {
        double be = 0;
        track_render(&trk[0], o, CTL);
        for (k = 0; k < CTL; k++) {
            int32_t a = o[k] < 0 ? -o[k] : o[k];
            e += (double)o[k] * o[k];
            be += (double)o[k] * o[k];
            sum += o[k];
            if (a > s.peak) s.peak = a;
            if ((o[k] < 0) != (prev < 0)) s.zc++;
            prev = o[k];
            s.hash = fnv(s.hash, o[k]);
            if (buf) buf[n] = (int16_t)clamp(o[k], -32768, 32767);
            n++;
        }
        env[i] = sqrt(be / CTL);
        if (trk[0].v[0].active) last = i;
        s.tail = o[CTL - 1];
    }
    s.live_end = trk[0].v[0].active;
    s.rms = sqrt(e / (double)(nb * CTL));
    s.dc = fabs(sum / (double)(nb * CTL));
    {   /* t-30: ms from the loudest block to the first one 30 dB below it */
        double mx = 0; uint32_t im = 0;
        for (i = 0; i < nb && i < NMAXB; i++) if (env[i] > mx) { mx = env[i]; im = i; }
        s.t30 = -1;
        for (i = im; i < nb && i < NMAXB; i++) if (env[i] < mx * 0.0316) { s.t30 = (double)(i - im) * CTL * 1000.0 / FS; break; }
    }
    (void)last;
    return s;
}
static double zc_hz(const stat_t *s, uint32_t nb) { return s->zc / 2.0 / ((double)nb * CTL / FS); }

static void chk(const char *what, int ok)
{
    if (!ok) { printf("shdrum_test: FAIL %s\n", what); fails++; }
}

static const char *PN[] = {"kick", "snare", "hat", "openhat", "tom", "clap", "bell", "zap"};
static const uint8_t PNOTE[] = {36, 50, 80, 80, 48, 60, 72, 72};

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : 0;
    uint32_t pi, np = ENGINES[ENGI_SHDRUM]->npresets;
    int bad;
    chk("engine table", ENGINES[ENGI_SHDRUM] == &ENG_SHDRUM && ENGI_SHDRUM == NENGINES - 1 && np == 8);

    /* 1. presets */
    bad = 0;
    for (pi = 0; pi < np; pi++) {
        stat_t a, b, c;
        fresh(pi);
        a = hit(PNOTE[pi], NMAXB, 0);
        fresh(pi);
        b = hit(PNOTE[pi], NMAXB, 0);
        c = hit(PNOTE[pi], 64, 0);
        printf("shdrum_test: %-8s peak %6.0f rms %7.0f dc %5.1f t30 %6.1f ms zc %7.0f Hz  ends %s\n", PN[pi], a.peak, a.rms,
               a.dc, a.t30, zc_hz(&a, NMAXB), a.live_end ? "NO" : "yes");
        if (a.peak >= 32767 * 1.5 || a.rms < 100 || a.dc > 0.02 * a.peak || a.live_end || a.hash != b.hash || (a.tail > 64 || a.tail < -64) || a.t30 < 0)
            printf("  -> peak %d rms %d dc %d end %d rep %d tail %d t30 %d\n", a.peak >= 32767 * 1.5, a.rms < 100, a.dc > 0.02 * a.peak,
                   a.live_end, a.hash != b.hash, (int)a.tail, a.t30 < 0), bad++;
        (void)c;
    }
    chk("presets: level, DC, end, repeatable", !bad);

    /* 2. controls */
    {
        stat_t r0, r1;
        uint64_t h0;
        int16_t save;
        fresh(0);
        r0 = hit(36, 200, 0); h0 = r0.hash;
        /* A level 0 and B level 0: silence */
        fresh(0); trk[0].p[SHP_ALVL] = 0; trk[0].p[SHP_BLVL] = 0; trk[0].p[SHP_CLIK] = 0;
        r1 = hit(36, 100, 0);
        chk("both levels 0 silent", r1.peak == 0);
        /* B level up changes it; A level 0 leaves B only */
        fresh(0); trk[0].p[SHP_BLVL] = 127;
        r1 = hit(36, 200, 0); chk("LVL B changes", r1.hash != h0);
        fresh(0); trk[0].p[SHP_ALVL] = 0; trk[0].p[SHP_BLVL] = 100;
        r1 = hit(36, 200, 0); chk("LVL A 0 leaves B", r1.peak > 100);
        { /* each parameter alone changes the hit */
            static const struct { int id; int v; const char *n; } T[] = {
                {P_E0, SHW_SQR, "WAV A"}, {P_E1, SHW_SAW, "WAV B"}, {P_E2, SHM_RING, "XMOD ring"}, {P_E3, 90, "DPTH"},
                {P_E4, 7, "TUN B"}, {P_E5, 0, "SWEP"}, {P_E6, 100, "SWP T"}, {P_E7, 100, "DRV"},
                {SHP_AATK, 60, "A ATK"}, {SHP_ADEC, 100, "A DEC"}, {SHP_BATK, 60, "B ATK"}, {SHP_BDEC, 100, "B DEC"},
                {SHP_CLIK, 100, "CLIK"}, {SHP_FOLD, 100, "FOLD"}, {SHP_CRSH, 100, "CRSH"}, {SHP_FTYP, SHF_LP, "FLT"},
            };
            uint32_t i;
            for (i = 0; i < NELEM(T); i++) {
                fresh(0);
                trk[0].p[SHP_BLVL] = 60; trk[0].p[P_E3] = 40;
                r0 = hit(36, 200, 0);
                trk[0].p[T[i].id] = (int16_t)T[i].v;
                if (T[i].id == SHP_FTYP) trk[0].p[SHP_CUT] = 30;
                r1 = hit(36, 200, 0);
                if (r1.hash == r0.hash) { printf("shdrum_test: %s changes nothing\n", T[i].n); fails++; }
            }
        }
        /* MIX ignores DPTH */
        fresh(0); trk[0].p[P_E2] = SHM_MIX; trk[0].p[SHP_BLVL] = 60; trk[0].p[SHP_CLIK] = 0; trk[0].p[P_E3] = 0; r0 = hit(36, 200, 0);
        trk[0].p[P_E3] = 127; r1 = hit(36, 200, 0); chk("MIX ignores DPTH", r0.hash == r1.hash);
        /* FM with LVL B 0 differs from MIX with LVL B 0 */
        fresh(0); trk[0].p[SHP_BLVL] = 0; trk[0].p[P_E2] = SHM_MIX; r0 = hit(36, 200, 0);
        trk[0].p[P_E2] = SHM_FM; trk[0].p[P_E3] = 60; r1 = hit(36, 200, 0); chk("FM (B silent) is not MIX", r0.hash != r1.hash && r1.peak > 100);
        /* the sweep: the kick's start is higher than its end; no sweep: steady */
        {
            int16_t *b = malloc(2 * 22050);
            double f0, f1; uint32_t i, z0 = 0, z1 = 0;
            fresh(0); trk[0].p[SHP_BLVL] = 0; trk[0].p[SHP_ADEC] = 100; trk[0].p[P_E5] = 36; trk[0].p[P_E6] = 40;
            (void)hit(36, 600, b);
            for (i = 1; i < 2000; i++) z0 += (b[i] < 0) != (b[i - 1] < 0);
            for (i = 8000; i < 10000; i++) z1 += (b[i] < 0) != (b[i - 1] < 0);
            f0 = z0 / 2.0 / (2000.0 / FS); f1 = z1 / 2.0 / (2000.0 / FS);
            printf("shdrum_test: sweep: first 45 ms %.0f Hz, at 180 ms %.0f Hz (key 36: 65.4 Hz)\n", f0, f1);
            chk("sweep falls to the key", f0 > 1.8 * f1 && f1 > 55 && f1 < 80);
            free(b);
        }
        /* a longer decay rings longer */
        fresh(0); trk[0].p[SHP_BLVL] = 0; trk[0].p[SHP_ADEC] = 50; r0 = hit(36, 1000, 0);
        trk[0].p[SHP_ADEC] = 80; r1 = hit(36, 1000, 0); chk("A DEC longer -> t30 longer", r1.t30 > r0.t30);
        /* velocity */
        fresh(0); r0 = hit(36, 100, 0);
        trk_note_on(&trk[0], 36, 40); { int32_t o[CTL]; double e = 0; uint32_t k, i; for (k = 0; k < NTRK; k++) memset(trk[k].v, 0, sizeof trk[k].v);
            trk_note_on(&trk[0], 36, 40); for (i = 0; i < 100; i++) { track_render(&trk[0], o, CTL); for (k = 0; k < CTL; k++) e += (double)o[k] * o[k]; }
            chk("velocity scales", sqrt(e / (100.0 * CTL)) < 0.6 * r0.rms); }
        (void)save;
    }

    /* 3. corners */
    {
        uint32_t xm, ft, c, bad2 = 0;
        for (xm = 0; xm < SHM_COUNT; xm++)
            for (ft = 0; ft < SHF_COUNT; ft++)
                for (c = 0; c < 64; c++) {
                    uint32_t b;
                    stat_t s;
                    fresh(0);
                    trk[0].p[P_E0] = (int16_t)(c % 5); trk[0].p[P_E1] = (int16_t)((c / 5) % 5);
                    trk[0].p[P_E2] = (int16_t)xm; trk[0].p[SHP_FTYP] = (int16_t)ft;
                    for (b = 0; b < 8; b++) {
                        int16_t lo = (c >> b) & 1 ? 0 : 127;
                        static const int ids[8] = {P_E3, P_E7, SHP_CLIK, SHP_FOLD, SHP_CRSH, SHP_CUT, SHP_RES, SHP_BLVL};
                        trk[0].p[ids[b]] = lo;
                    }
                    trk[0].p[P_E4] = (c & 1) ? -24 : 24; trk[0].p[P_E5] = (c & 2) ? -24 : 48; trk[0].p[SHP_FENV] = (c & 4) ? -64 : 63;
                    trk[0].p[SHP_ALVL] = 127; trk[0].p[SHP_ADEC] = (c & 8) ? 0 : 127 - (c & 7);
                    s = hit((c & 16) ? 0 : (c & 32) ? 127 : 60, 900, 0);
                    if (s.peak > 70000 || s.dc > 4000) { if (!bad2) printf("  corner xm %u ft %u c %u: peak %.0f dc %.1f\n", xm, ft, c, s.peak, s.dc); bad2++; }
                }
        printf("shdrum_test: corners (3 XMOD x 4 FLT x 64): %s\n", bad2 ? "FAIL" : "ok");
        fails += bad2 != 0;
    }

    /* 4. isolation: the extras of SHDRUM do not touch another engine */
    {
        stat_t a, b; uint32_t k;
        host_tracks_init(); host_preset(&trk[0], 0, 0); a = hit(48, 100, 0);
        host_tracks_init(); host_preset(&trk[0], 0, 0);
        for (k = SHP_AATK; k < SHP_END; k++) trk[0].p[k] = 99;
        b = hit(48, 100, 0);
        chk("ANALOG ignores SHDRUM slots", a.hash == b.hash);
        for (k = 0; k < 8; k++) chk("E desc ok", ENGINES[ENGI_SHDRUM]->edit[k].def >= ENGINES[ENGI_SHDRUM]->edit[k].min &&
                                    ENGINES[ENGI_SHDRUM]->edit[k].def <= ENGINES[ENGI_SHDRUM]->edit[k].max);
        for (k = SHP_AATK; k < SHP_END; k++) {
            const param_desc_t *d = param_desc_of(ENGI_SHDRUM, k);
            chk("X desc ok", d && d->def >= d->min && d->def <= d->max && d != &TP[k] && d->min >= -64 && d->max <= 127);
        }
        fresh(0); chk("track_desc reaches SHDRUM's labels", track_desc(&trk[0], SHP_FOLD) == &SHD_X[SHP_FOLD - SHP_AATK]);
        host_tracks_init(); host_preset(&trk[0], 0, 0); chk("other engines keep TP", track_desc(&trk[0], SHP_FOLD) == &TP[SHP_FOLD]);
    }

    /* 5. demos */
    if (dir) {
        static int16_t buf[44100 * 2];
        char path[512];
        for (pi = 0; pi < np; pi++) {
            FILE *f;
            fresh(pi);
            memset(buf, 0, sizeof buf);
            (void)hit(PNOTE[pi], NMAXB, buf);
            snprintf(path, sizeof path, "%s/shdrum_%s.wav", dir, PN[pi]);
            f = fopen(path, "wb");
            if (f) {
                wav_hdr(f, NMAXB * CTL);
                for (uint32_t i = 0; i < NMAXB * CTL; i++) wav_put(f, buf[i], buf[i]);
                fclose(f);
            }
        }
    }
    printf("shdrum_test: %s\n", fails ? "FAIL" : "ok");
    return fails != 0;
}
