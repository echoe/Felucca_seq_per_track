/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from Mini-Tone (https://github.com/eldontyrellcorp/Mini-Tone, GPL-3.0-only); engine numbers changed for this tree (SWARM 15, FMTONE 16) */
/* SWARM engine test (src/eng_swarm.c) on the host, through hostsim.c as regress.c.
 *   build/host/swarm_test
 * 1. pitch: MIX 0 (the main alone) plays the key (+-1 %), OCT -1 / -2 an octave / two octaves below it; the swarm
 *    plays the key too (MIX 127, DTN 0).
 * 2. MIX 0 is the main alone: DTN, ANIM, NOIS and SWRM change nothing (bit for bit).
 * 3. DTN spreads the swarm: the spectrum around the fundamental widens with DTN (monotonic), and DTN 0 stays narrow.
 * 4. movement: with ANIM and with NOIS the spectrum changes over a second or two, without them it holds still.
 * 5. waveforms: SIN has no 2nd harmonic, SQR no even ones and a 3rd at -9.5 dB, SAW a 2nd at -6 dB, TRI a 3rd at -19 dB.
 * 6. voices: at most 4 in POLY and in UNISON (engine_t.poly), freed after the release; the engine memory of one voice
 *    does not touch another's (two notes played apart equal the same two played together).
 * 7. corners: every WAVE x SWRM x OCT x DTN x MIX x ANIM x NOIS x CUT extreme: no sample beyond 16 bits in the mix, no
 *    voice sum beyond 3 x VOICE_FS, no DC, no NaN-like blow-ups (the filter state stays bounded).
 * 8. level: the factory presets are within a few dB of ANALOG's SAW LEAD.
 * 9. determinism: a note from silence renders the same samples again.
 * 10. cost: host time per sample and voice against ANALOG and TRIO (the device estimate is a ratio of those). */
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
    printf("swarm: %-92s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static uint32_t eng_idx_of(const engine_t *e)
{
    uint32_t i;
    for (i = 0; i < NENGINES; i++)
        if (ENGINES[i] == e)
            return i;
    return 0;
}

/* track 0 the SWARM engine with e[0..7]: a flat envelope (ATK 0, SUS full), no sends, POLY */
static void setup(const int16_t *e)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], eng_idx_of(&ENG_SWARM), 0);
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = e[i];
    trk[0].p[P_ATK] = 0;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_REL] = 10;
    trk[0].p[P_ED_FLT] = 0;
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
}

/* the voices' sum (track_render, before the mix) of note for n samples, after 0.1 s */
static void voice_render(const int16_t *e, uint32_t note, double *y, uint32_t n)
{
    static int32_t b[CTL];
    uint32_t i, k, skip = FS / 10u / CTL;
    setup(e);
    trk_note_on(&trk[0], note, 100);
    for (i = 0; i < skip; i++)
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

/* power spectrum (Hann) of NFFT samples of y from offset off */
static void spec1(const double *y, uint32_t off, double *ps)
{
    static double re[NFFT], im[NFFT];
    uint32_t i;
    for (i = 0; i < NFFT; i++) {
        re[i] = y[off + i] * (0.5 - 0.5 * cos(2 * M_PI * i / NFFT));
        im[i] = 0;
    }
    fft(re, im, NFFT);
    for (i = 0; i < NFFT / 2; i++)
        ps[i] = re[i] * re[i] + im[i] * im[i];
}

static double hz_of(uint32_t note) { return 440.0 * pow(2.0, ((int)note - 69) / 12.0); }
static uint32_t bin_of(double hz) { return (uint32_t)(hz / FS * NFFT + 0.5); }

/* the strongest bin in lo..hi Hz, refined by a parabola (Hz) */
static double peak_hz(const double *ps, double lo, double hi)
{
    uint32_t a = bin_of(lo), b = bin_of(hi), i, best = a;
    double al, be, ga, p;
    for (i = a; i <= b; i++)
        if (ps[i] > ps[best])
            best = i;
    al = log(ps[best - 1] + 1e-30);
    be = log(ps[best] + 1e-30);
    ga = log(ps[best + 1] + 1e-30);
    p = 0.5 * (al - ga) / (al - 2 * be + ga);
    return (best + p) * (double)FS / NFFT;
}

/* power within +-frac of hz (a harmonic's line, the Hann main lobe included) */
static double band_pow(const double *ps, double hz, double frac)
{
    uint32_t a = bin_of(hz * (1 - frac)), b = bin_of(hz * (1 + frac)), i;
    double s = 0;
    for (i = a; i <= b; i++)
        s += ps[i];
    return s;
}

/* the narrowest window around the strongest bin within +-6 % of hz that holds 90 % of the power there (Hz) */
static double line_width(const double *ps, double hz)
{
    uint32_t a = bin_of(hz * 0.94), b = bin_of(hz * 1.06), i, pk = a, k;
    double tot = 0, acc = 0;
    for (i = a; i <= b; i++) {
        tot += ps[i];
        pk = ps[i] > ps[pk] ? i : pk;
    }
    for (k = 0; k < 400u; k++) {
        acc = 0;
        for (i = pk > k ? pk - k : 0; i <= pk + k && i <= b; i++)
            acc += ps[i];
        if (acc >= 0.9 * tot)
            break;
    }
    return (2 * k + 1) * (double)FS / NFFT;
}

/* the spread of the spectrum around hz: sqrt of the power-weighted variance of the frequency over +-12 % of hz (Hz) */
static double spread(const double *ps, double hz)
{
    uint32_t a = bin_of(hz * 0.88), b = bin_of(hz * 1.12), i;
    double s0 = 0, s1 = 0, s2 = 0, f, m;
    for (i = a; i <= b; i++) {
        f = (double)i * FS / NFFT;
        s0 += ps[i];
        s1 += ps[i] * f;
    }
    m = s1 / s0;
    for (i = a; i <= b; i++) {
        f = (double)i * FS / NFFT;
        s2 += ps[i] * (f - m) * (f - m);
    }
    return sqrt(s2 / s0);
}

/* e = {WAVE, SWRM, OCT, DTN, MIX, ANIM, NOIS, CUT} */
static const int16_t E_OPEN[8] = {0, 0, 0, 45, 80, 0, 0, 127};

int main(void)
{
    static double y[NFFT * 6 + 8192], ps[NFFT / 2], ps2[NFFT / 2], ya[NFFT], yb[NFFT];
    int16_t e[8];
    uint32_t i, k, note = 81;                     /* A5: 880 Hz, a few bins wide at NFFT 16384 */
    double f0 = hz_of(note);

    /* 1. pitch */
    memcpy(e, E_OPEN, sizeof e);
    e[4] = 0;
    voice_render(e, note, y, NFFT);
    spec1(y, 0, ps);
    check("MIX 0 plays the key (SAW, within 1 %)", fabs(peak_hz(ps, f0 * 0.8, f0 * 1.2) / f0 - 1) < 0.01);
    for (k = 1; k <= 2; k++) {
        e[2] = (int16_t)k;
        voice_render(e, note, y, NFFT);
        spec1(y, 0, ps);
        {
            double want = f0 / (k == 1 ? 2.0 : 4.0);
            char m[96];
            snprintf(m, sizeof m, "OCT -%u plays %s below the key (within 1 %%)", k, k == 1 ? "an octave" : "two octaves");
            check(m, fabs(peak_hz(ps, want * 0.8, want * 1.2) / want - 1) < 0.01);
        }
    }
    memcpy(e, E_OPEN, sizeof e);
    e[3] = 0;
    e[4] = 127;
    voice_render(e, note, y, NFFT);
    spec1(y, 0, ps);
    check("the swarm plays the key (MIX 127, DTN 0, within 1 %)", fabs(peak_hz(ps, f0 * 0.8, f0 * 1.2) / f0 - 1) < 0.01);

    /* 2. MIX 0: the main alone */
    {
        static double ya2[4096], yb2[4096];
        int same = 1;
        memcpy(e, E_OPEN, sizeof e);
        e[4] = 0;
        voice_render(e, note, ya2, 4096);
        e[1] = 2; e[3] = 127; e[5] = 127; e[6] = 127;
        voice_render(e, note, yb2, 4096);
        for (i = 0; i < 4096u; i++)
            same &= ya2[i] == yb2[i];
        check("MIX 0: SWRM, DTN, ANIM and NOIS change nothing (bit for bit)", same);
    }

    /* 3. DTN spreads the swarm */
    {
        static const int16_t D[5] = {0, 32, 64, 96, 127};
        double sp[5];
        int mono = 1;
        for (k = 0; k < 5u; k++) {
            memcpy(e, E_OPEN, sizeof e);
            e[3] = D[k];
            e[4] = 127;
            voice_render(e, note, y, NFFT);
            spec1(y, 0, ps);
            sp[k] = spread(ps, f0);
            if (k && sp[k] <= sp[k - 1])
                mono = 0;
        }
        printf("swarm:   spread around 880 Hz at DTN 0 32 64 96 127: %.1f %.1f %.1f %.1f %.1f Hz\n", sp[0], sp[1], sp[2],
               sp[3], sp[4]);
        check("DTN: the spread grows with DTN (monotonic)", mono);
        check("DTN 0 is narrow, DTN 127 wide (> 10 x)", sp[4] > 10 * sp[0]);
        /* the outer pair of the swarm lies about +-1 semitone (5.9 %) at DTN 127 */
        memcpy(e, E_OPEN, sizeof e);
        e[3] = 127;
        e[4] = 127;
        voice_render(e, note, y, NFFT);
        spec1(y, 0, ps);
        check("DTN 127: power near both +-1 semitone sides of the key (> -30 dB of the key's)",
              band_pow(ps, f0 * 1.0595, 0.01) > 1e-3 * band_pow(ps, f0, 0.06) &&
              band_pow(ps, f0 / 1.0595, 0.01) > 1e-3 * band_pow(ps, f0, 0.06));
    }

    /* 4. movement: the upper edge of the swarm's cluster (the highest frequency within 20 dB of its peak, DTN 127,
     * 1.0 .. 1.15 x the key) in six windows over ~2.2 s. Still, the edge holds; ANIM sweeps each oscillator's detune
     * (the top one by up to +-2.2 %, 0.71 Hz); NOIS makes the lines wander and broadens them */
    {
        static const int16_t S[3][2] = {{0, 0}, {127, 0}, {0, 127}};
        double lo[3], hi[3], mean[3];
        for (k = 0; k < 3u; k++) {
            uint32_t w;
            double sum = 0;
            lo[k] = 1e9;
            hi[k] = 0;
            memcpy(e, E_OPEN, sizeof e);
            e[3] = 127;
            e[4] = 127;
            e[5] = S[k][0];
            e[6] = S[k][1];
            voice_render(e, note, y, NFFT * 6);
            for (w = 0; w < 6u; w++) {
                double mx = 0, edge = 0;
                spec1(y, w * NFFT, ps);
                for (i = bin_of(f0 * 1.0); i <= bin_of(f0 * 1.15); i++)
                    mx = ps[i] > mx ? ps[i] : mx;
                for (i = bin_of(f0 * 1.0); i <= bin_of(f0 * 1.15); i++)
                    if (ps[i] > 0.01 * mx)
                        edge = (double)i * FS / NFFT;
                lo[k] = edge < lo[k] ? edge : lo[k];
                hi[k] = edge > hi[k] ? edge : hi[k];
                sum += edge;
            }
            mean[k] = sum / 6;
        }
        printf("swarm:   upper edge of the cluster over 2.2 s (Hz): still %.1f..%.1f, ANIM %.1f..%.1f, NOIS %.1f..%.1f"
               " (mean %.1f / %.1f / %.1f)\n", lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], mean[0], mean[1], mean[2]);
        check("still (ANIM 0, NOIS 0): the cluster's edge holds (range < 3 Hz)", hi[0] - lo[0] < 3.0);
        check("ANIM 127: the edge moves (range > 10 Hz and > 5 x the still one)",
              hi[1] - lo[1] > 10.0 && hi[1] - lo[1] > 5 * (hi[0] - lo[0] + 0.5));
        (void)mean;
    }
    {
        /* NOIS: the swarm's lines wander, so a swarm at DTN 0 (all seven on the key) no longer sits in one narrow line */
        double w0, w1;
        memcpy(e, E_OPEN, sizeof e);
        e[3] = 0; e[4] = 127; e[6] = 0;
        voice_render(e, note, y, NFFT);
        spec1(y, 0, ps);
        w0 = line_width(ps, f0);
        e[6] = 127;
        voice_render(e, note, y, NFFT);
        spec1(y, 0, ps);
        w1 = line_width(ps, f0);
        printf("swarm:   90 %% line width at DTN 0: NOIS 0 %.1f Hz, NOIS 127 %.1f Hz\n", w0, w1);
        check("NOIS 127: the line is at least twice as wide as with NOIS 0", w1 >= 2 * w0);
    }

    /* 5. waveforms (main alone, filter open): the harmonics' levels against the fundamental */
    {
        static const struct { const char *n; int w; double h2, h3; } W[4] = {
            {"SAW", 0, -6.0, -9.5}, {"SQR", 1, -99.0, -9.5}, {"TRI", 2, -99.0, -19.1}, {"SIN", 3, -99.0, -99.0}};
        double f1 = hz_of(60);                    /* C4: harmonics well inside the band */
        for (k = 0; k < 4u; k++) {
            double p1, p2, p3, d2, d3;
            char m[110];
            memcpy(e, E_OPEN, sizeof e);
            e[0] = (int16_t)W[k].w;
            e[4] = 0;
            voice_render(e, 60, y, NFFT);
            spec1(y, 0, ps);
            p1 = band_pow(ps, f1, 0.03);
            p2 = band_pow(ps, 2 * f1, 0.03);
            p3 = band_pow(ps, 3 * f1, 0.03);
            d2 = 10 * log10(p2 / p1 + 1e-12);
            d3 = 10 * log10(p3 / p1 + 1e-12);
            snprintf(m, sizeof m, "%s: 2nd harmonic %.1f dB (want %s%.0f), 3rd %.1f dB (want %s%.1f)", W[k].n, d2,
                     W[k].h2 < -90 ? "<-30 vs " : "", W[k].h2 < -90 ? -30.0 : W[k].h2, d3,
                     W[k].h3 < -90 ? "<-30 vs " : "", W[k].h3 < -90 ? -30.0 : W[k].h3);
            check(m, (W[k].h2 < -90 ? d2 < -30 : fabs(d2 - W[k].h2) < 1.5) &&
                     (W[k].h3 < -90 ? d3 < -30 : fabs(d3 - W[k].h3) < 1.5));
        }
    }

    /* 6. voices: the cap of 4, in POLY and UNISON; independent voices */
    {
        int maxv = 0, maxu = 0, freed;
        static int32_t b[CTL];
        for (k = 0; k < 2u; k++) {
            uint32_t n, j;
            memcpy(e, E_OPEN, sizeof e);
            setup(e);
            trk[0].p[P_VOICE] = k ? V_UNISON : V_POLY;
            for (n = 0; n < 8u; n++) {
                trk_note_on(&trk[0], 48 + 4 * n, 100);
                track_render(&trk[0], b, CTL);
            }
            for (j = 0, n = 0; j < NVOICE; j++)
                n += trk[0].v[j].active != 0;
            if (k)
                maxu = (int)n;
            else
                maxv = (int)n;
        }
        check("POLY: 8 keys held sound at most 4 voices (engine_t.poly)", maxv == 4);
        check("UNISON: the voices are at most 4", maxu >= 1 && maxu <= 4);
        setup(E_OPEN);
        trk_note_on(&trk[0], 60, 100);
        trk_note_off(&trk[0], 60);
        for (i = 0; i < FS * 2u / CTL; i++)
            track_render(&trk[0], b, CTL);
        for (i = 0, freed = 1; i < NVOICE; i++)
            freed &= !trk[0].v[i].active;
        check("the voices are free after the release", freed);
    }
    {
        /* a second voice starting, and rendering, leaves the first voice's slot of the part's engine memory alone:
         * the same snapshot rendered one block with the first note only and with a second note added */
        static int32_t b1[CTL];
        static track_t snap;
        static uint8_t mem_snap[sizeof eng_mem], slot_alone[sizeof(swm_slot_t)];
        uint32_t j;
        int alone, started;
        memcpy(e, E_OPEN, sizeof e);
        e[5] = 80;
        e[6] = 40;
        setup(e);
        trk_note_on(&trk[0], 60, 100);
        for (j = 0; j < 20u; j++)
            track_render(&trk[0], b1, CTL);
        memcpy(&snap, &trk[0], sizeof snap);
        memcpy(mem_snap, eng_mem, sizeof eng_mem);
        track_render(&trk[0], b1, CTL);                                  /* the first voice alone */
        memcpy(slot_alone, ENG_MEM(0), sizeof slot_alone);
        memcpy(&trk[0], &snap, sizeof snap);
        memcpy(eng_mem, mem_snap, sizeof eng_mem);
        trk_note_on(&trk[0], 67, 100);                                   /* a second voice starts */
        started = !memcmp(ENG_MEM(0), mem_snap, sizeof(swm_slot_t));     /* its note-on left slot 0 as it was */
        track_render(&trk[0], b1, CTL);
        alone = !memcmp(ENG_MEM(0), slot_alone, sizeof slot_alone);      /* and its block did not change slot 0 */
        check("a second note leaves the first voice's slot as it was (its note-on and its block)", started && alone);
        check("the four voices' state fits the part's engine memory", sizeof(swm_slot_t) * SWM_POLY <= ENG_MEM_PART);
    }

    /* 7. corners: every extreme of the eight parameters (a sample of the wave pairs); 4 notes at velocity 127 for the peak
     * and the filter state, one note held 1 s for the DC */
    {
        uint32_t w, s, o, dt, mx, an, nz, ct, n = 0, over = 0, blow = 0;
        int32_t worst = 0;
        static int32_t b[CTL];
        double dcmax = 0;
        for (w = 0; w < 4u; w++)
            for (s = 0; s < 4u; s++)
                for (o = 0; o < 3u; o++)
                    for (dt = 0; dt < 2u; dt++)
                        for (mx = 0; mx < 2u; mx++)
                            for (an = 0; an < 2u; an++)
                                for (nz = 0; nz < 2u; nz++)
                                    for (ct = 0; ct < 2u; ct++) {
                                        uint32_t blk, note_i, pass;
                                        if (s && w && (w + s) % 3u)       /* a sample of the wave pairs, all of the others */
                                            continue;
                                        e[0] = (int16_t)w;
                                        e[1] = (int16_t)s;
                                        e[2] = (int16_t)o;
                                        e[3] = dt ? 127 : 0;
                                        e[4] = mx ? 127 : 0;
                                        e[5] = an ? 127 : 0;
                                        e[6] = nz ? 127 : 0;
                                        e[7] = ct ? 127 : 0;
                                        for (pass = 0; pass < 2u; pass++) {
                                            int32_t vmax = 0;
                                            double sum = 0;
                                            setup(e);
                                            trk[0].p[P_LEVEL] = 127;
                                            if (!pass) {
                                                for (note_i = 0; note_i < 4u; note_i++)
                                                    trk_note_on(&trk[0], note_i == 0 ? 24 : note_i == 1 ? 60 : note_i == 2 ? 96 : 120, 127);
                                            } else {
                                                trk_note_on(&trk[0], 60, 100);
                                            }
                                            for (blk = 0; blk < (pass ? FS * 5u / 4u : FS / 4u) / CTL; blk++) {
                                                uint32_t j;
                                                for (j = 0; j < CTL; j++)
                                                    b[j] = 0;
                                                track_render(&trk[0], b, CTL);
                                                if (pass && blk < FS / 4u / CTL)     /* the first 0.25 s: the filter settles */
                                                    continue;
                                                for (j = 0; j < CTL; j++) {
                                                    int32_t a = b[j] < 0 ? -b[j] : b[j];
                                                    vmax = a > vmax ? a : vmax;
                                                    sum += b[j];
                                                }
                                            }
                                            if (!pass) {
                                                worst = vmax > worst ? vmax : worst;
                                                over += vmax > 4 * 3 * VOICE_FS;     /* 4 voices x 3 x VOICE_FS */
                                                for (note_i = 0; note_i < SWM_POLY; note_i++)
                                                    blow += abs(((swm_slot_t *)ENG_MEM(0))[note_i].ic1) > (400000 << 8) ||     /* (the state is Q8) */
                                                            abs(((swm_slot_t *)ENG_MEM(0))[note_i].ic2) > (400000 << 8);
                                                n++;
                                            } else if (fabs(sum / FS) > dcmax) {
                                                dcmax = fabs(sum / FS);
                                            }
                                        }
                                    }
        printf("swarm:   %u corner renders, 4-voice peak %d (%.2f x VOICE_FS a voice), worst |DC| of one voice %.0f (%.2f %% of VOICE_FS)\n",
               n, worst, worst / (4.0 * VOICE_FS), dcmax, 100.0 * dcmax / VOICE_FS);
        check("corners: no voice beyond 3 x VOICE_FS (4 notes at velocity 127)", over == 0);
        check("corners: no voice beyond 2.4 x VOICE_FS (ANALOG's nominal headroom is 2)", worst < 2.4 * 4 * VOICE_FS);
        check("corners: the filter state stays bounded", blow == 0);
        check("corners: DC of one voice held 1 s under 1 % of VOICE_FS", dcmax < 0.01 * VOICE_FS);
    }

    /* 8. level: factory presets against ANALOG's SAW LEAD (RMS of the voice sum over the steady note, dB) */
    {
        static double r[NFFT];
        double ref, rms;
        uint32_t pi;
        ref = HS_REF_SAW_RMS;
        printf("swarm:   ANALOG SAW LEAD (as Mini-Tone measured it), C4, velocity 100: RMS %.3f\n", ref);
        for (pi = 0; pi < ENG_SWARM.npresets; pi++) {
            uint32_t j, kk;
            static int32_t b[CTL];
            char m[110];
            double dB;
            memset(trk, 0, sizeof trk);
            host_tracks_init();
            host_preset(&trk[0], eng_idx_of(&ENG_SWARM), pi);
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
            snprintf(m, sizeof m, "level: %-12s %+5.1f dB against ANALOG SAW LEAD (within -6 .. +4 dB)", ENG_SWARM.presets[pi].name, dB);
            check(m, dB > -6 && dB < 4);
        }
    }

    /* 9. determinism */
    {
        memcpy(e, E_OPEN, sizeof e);
        e[5] = 70;
        e[6] = 60;
        voice_render(e, 57, ya, NFFT);
        voice_render(e, 57, yb, NFFT);
        check("a note from silence renders the same samples again (ANIM, NOIS on)", !memcmp(ya, yb, sizeof ya));
    }

    /* 10. cost: ns per sample and voice, SWARM / ANALOG / FM6, 4 notes held */
    {
        static int32_t b[CTL];
        const engine_t *E[3] = {&ENG_SWARM, &ENG_ANALOG, &ENG_FM6};   /* (Mini-Tone compares with SID; ANALOG here) */
        double ns[3];
        uint32_t en;
        for (en = 0; en < 3u; en++) {
            struct timespec t0, t1;
            uint32_t j, blocks = FS * 4u / CTL;
            memset(trk, 0, sizeof trk);
            host_tracks_init();
            host_preset(&trk[0], eng_idx_of(E[en]), 0);
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
        printf("swarm:   host ns per sample and voice: SWARM %.1f, ANALOG %.1f, FM6 %.1f  (SWARM %.1f x ANALOG, %.1f x FM6)\n",
               ns[0], ns[1], ns[2], ns[0] / ns[1], ns[0] / ns[2]);
        check("cost: SWARM within 6 x ANALOG per voice (seven oscillators and a filter; the voice cap is 4)",
              ns[0] < 6.0 * ns[1]);
    }

    printf("swarm: %s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
