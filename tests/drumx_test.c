/* SPDX-License-Identifier: GPL-3.0-only
 * After JIANT's tests/drumx_test.c (https://github.com/juanjiant-bit/JIANT, GPL-3.0-only, Juan Ignacio Antonucci); written for this tree's DRUM
 * engine, which plays DRUM-X (src/drumx_voice.c, FELUCCA_DRUMX=1):
 * 1. every lane of every kit, struck through the engine by its GM note, sounds, never at full scale, has no DC, and ends
 * 2. MORPH: 0 and 127 render different hits, 64 between them
 * 3. TUNE +12 semitones doubles the tom's frequency; the kick's pitch envelope falls
 * 4. the closed hat chokes the open one
 * 5. NOIS, FOLD, FM ENV move every lane; DECY lengthens the tail
 * 6. a stored KIT 9..11 plays 808 BOOM, TECHNO RAW, BREAKS; the SH kit still plays; every render repeats bit for bit */
#include <stdarg.h>
#define main hostsim_main
#include "hostsim.c"
#undef main
static int fails;
static void check(const char *what, int ok, const char *fmt, ...)
{
    printf("drumx: %-66s %s", what, ok ? "ok" : "FAIL");
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        printf("  (");
        vprintf(fmt, ap);
        printf(")");
        va_end(ap);
    }
    printf("\n");
    fails += !ok;
}
#define NB 700u                                  /* ~0.5 s of blocks */
static const uint8_t LANE_NOTE[8] = {36, 38, 39, 42, 46, 45, 37, 56};
typedef struct { double rms, peak, dc; uint32_t cross, ended; uint64_t h; } res_t;
/* strike the notes, render nb blocks of the part, measure the sum (the voices before the mix) */
static res_t strike(const int16_t *e, const uint8_t *notes, uint32_t nn, uint32_t nb, uint32_t from, uint32_t to)
{
    static int32_t b[CTL];
    res_t r = {0, 0, 0, 0, 0, 1469598103934665603ull};
    double sq = 0, sum = 0;
    uint32_t i, k, blk, cnt = 0;
    int32_t last = 0;
    memset(trk, 0, sizeof trk);
    memset(eng_mem, 0, sizeof eng_mem);              /* (a hit's noise state lives there: every strike from rest) */
    host_tracks_init();
    host_preset(&trk[0], ENGI_DRUM, 0);
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = e[i];
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    for (k = 0; k < nn; k++)
        trk_note_on(&trk[0], notes[k], 100);
    for (blk = 0; blk < nb; blk++) {
        memset(b, 0, sizeof b);
        track_render(&trk[0], b, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t x = b[i];
            uint64_t h = r.h;
            h ^= (uint32_t)x; h *= 1099511628211ull; r.h = h;
            if (blk >= from && blk < to) {
                sq += (double)x * x; sum += x; cnt++;
                if (fabs((double)x) > r.peak) r.peak = fabs((double)x);
                r.cross += (x < 0) != (last < 0);
            }
            last = x;
        }
    }
    for (i = 0; i < NVOICE; i++)
        r.ended += !trk[0].v[i].active;
    r.rms = cnt ? sqrt(sq / cnt) : 0;
    r.dc = cnt ? sum / cnt : 0;
    return r;
}
int main(void)
{
    uint32_t kit, lane, ok1 = 1, ok1b = 1, ok1c = 1, ok1d = 1;
    int16_t e[8] = DRUM_KIT_E;
    res_t a, b2, c;
    for (kit = 0; kit < DX_NKIT; kit++)
        for (lane = 0; lane < 8u; lane++) {
            e[0] = (int16_t)kit;
            a = strike(e, &LANE_NOTE[lane], 1, 1400, 0, 1400);   /* ~1 s */
            ok1 &= a.peak > 1000.0;
            ok1b &= a.peak < 60000.0;
            ok1c &= fabs(a.dc) < 0.02 * (a.peak + 1.0);
            a = strike(e, &LANE_NOTE[lane], 1, 12600, 12200, 12600);   /* ~9 s: gone (808 BOOM's and DUB ROOTS' kicks ring long by design) */
            if (!(a.rms < 4.0 && a.ended == NVOICE)) printf("drumx: (kit %u lane %u: rms %.1f, %u of %u voices free)\n", (unsigned)kit, (unsigned)lane, a.rms, (unsigned)a.ended, (unsigned)NVOICE);
            ok1d &= a.rms < 4.0 && a.ended == NVOICE;
        }
    check("every lane of every kit sounds (peak above -30 dBFS)", ok1, 0);
    check("never at full scale (a hit peaks under 60000 after the knee)", ok1b, 0);
    check("no DC (under 2 % of the peak)", ok1c, 0);
    check("every hit ends within 9 s (the booms ring long) and frees its voice", ok1d, 0);
    {   /* MORPH */
        uint32_t ok = 1;
        for (kit = 0; kit < DX_NKIT; kit++) {
            double r0, r64, r127;
            e[0] = (int16_t)kit;
            e[7] = 0;   a = strike(e, &LANE_NOTE[1], 1, 300, 0, 300); r0 = a.rms;
            e[7] = 127; b2 = strike(e, &LANE_NOTE[1], 1, 300, 0, 300); r127 = b2.rms;
            e[7] = 64;  c = strike(e, &LANE_NOTE[1], 1, 300, 0, 300); r64 = c.rms;
            ok &= a.h != b2.h && c.h != a.h && c.h != b2.h;
            ok &= r64 >= (r0 < r127 ? r0 : r127) * 0.7 && r64 <= (r0 > r127 ? r0 : r127) * 1.3;
        }
        check("MORPH: 0, 64 and 127 are three different hits, 64 between them", ok, 0);
        e[7] = 64;
    }
    {   /* TUNE +12 doubles the tom */
        double f0, f1;
        e[0] = 0;
        e[1] = 64;  a = strike(e, &LANE_NOTE[5], 1, 120, 10, 60); f0 = a.cross;
        e[1] = 64 + 32; b2 = strike(e, &LANE_NOTE[5], 1, 120, 10, 60); f1 = b2.cross;   /* 32 * 3/16 = 6 semitones: the code's step is 3/16 */
        check("TUNE raises the tom (6 semitones: x1.41 within 15 %)", f0 > 4.0 && f1 / f0 > 1.41 * 0.85 && f1 / f0 < 1.41 * 1.15, "%.0f -> %.0f crossings", f0, f1);
        e[1] = 64;
        a = strike(e, &LANE_NOTE[0], 1, 60, 0, 6); b2 = strike(e, &LANE_NOTE[0], 1, 60, 54, 60);
        check("the kick's pitch envelope falls", a.cross > b2.cross, "%u crossings early, %u late", (unsigned)a.cross, (unsigned)b2.cross);
    }
    {   /* the closed hat chokes the open one: the open rings, then the closed is struck */
        uint32_t blk, i, k;
        double ra = 0, rb = 0;
        for (k = 0; k < 2u; k++) {
            static int32_t bf[CTL];
            double sq = 0;
            memset(trk, 0, sizeof trk);
            memset(eng_mem, 0, sizeof eng_mem);
            host_tracks_init();
            host_preset(&trk[0], ENGI_DRUM, 0);
            trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
            trk_note_on(&trk[0], LANE_NOTE[4], 100);
            for (blk = 0; blk < 120u; blk++) {
                memset(bf, 0, sizeof bf);
                if (blk == 40u && k)
                    trk_note_on(&trk[0], LANE_NOTE[3], 100);      /* the closed hat, 40 blocks in */
                track_render(&trk[0], bf, CTL);
                if (blk >= 60u)
                    for (i = 0; i < CTL; i++)
                        sq += (double)bf[i] * bf[i];
            }
            if (k) rb = sqrt(sq / (60.0 * CTL)); else ra = sqrt(sq / (60.0 * CTL));
        }
        check("the closed hat chokes the open one (its tail 10 dB or more down)", ra > 10.0 && rb < ra * 0.32, "%.0f -> %.0f", ra, rb);
    }
    {   /* the knobs */
        uint32_t ok = 1, kn;
        for (lane = 0; lane < 8u; lane++) {
            e[0] = 0; e[2] = 70; e[3] = 64; e[4] = 64; e[5] = 0; e[6] = 0; e[7] = 64;
            a = strike(e, &LANE_NOTE[lane], 1, 200, 0, 200);
            for (kn = 3; kn <= 6; kn++) {               /* DECY, NOIS, FM ENV, FOLD */
                int16_t f[8];
                memcpy(f, e, sizeof f);
                f[kn] = kn == 3 ? 100 : kn == 4 ? 127 : 127;
                b2 = strike(f, &LANE_NOTE[lane], 1, 200, 0, 200);
                if (b2.h == a.h && lane == DV_CLAP && kn >= 4u) continue;   /* (the clap is the noise alone: NOIS is at its top, FM ENV and FOLD act on the oscillator) */
                if (b2.h == a.h) { ok = 0; printf("drumx: (lane %u: knob E%u at %d changes nothing)\n", (unsigned)lane, (unsigned)kn, (int)f[kn]); }
            }
        }
        check("DECY, NOIS, FM ENV and FOLD change every lane's sound", ok, 0);
        e[3] = 64; e[4] = 64; e[5] = 0; e[6] = 0;
        a = strike(e, &LANE_NOTE[0], 1, 700, 200, 400);
        e[3] = 110; b2 = strike(e, &LANE_NOTE[0], 1, 700, 200, 400);
        check("DECY lengthens the kick's tail", b2.rms > a.rms * 1.5, "%.0f -> %.0f", a.rms, b2.rms);
        e[3] = 64;
        e[6] = 127; a = strike(e, &LANE_NOTE[1], 1, 300, 0, 300);
        check("FOLD at 127 stays under the knee's ceiling", a.peak < 60000.0, "%.0f", a.peak);
        e[6] = 0;
    }
    {   /* aliases, SH, determinism */
        uint32_t k, ok = 1;
        static const uint8_t PLAYS[3] = {2, 7, 8};
        for (k = 0; k < 3u; k++) {
            int16_t f[8], g[8];
            memcpy(f, e, sizeof f); memcpy(g, e, sizeof g);
            f[0] = (int16_t)(9u + k); g[0] = PLAYS[k];
            a = strike(f, &LANE_NOTE[1], 1, 300, 0, 300); b2 = strike(g, &LANE_NOTE[1], 1, 300, 0, 300);
            ok &= a.h == b2.h;
        }
        check("a stored KIT 9, 10, 11 plays 808 BOOM, TECHNO RAW, BREAKS", ok, 0);
        a = strike(e, &LANE_NOTE[1], 1, 300, 0, 300); b2 = strike(e, &LANE_NOTE[1], 1, 300, 0, 300);
        check("a hit repeats bit for bit", a.h == b2.h, 0);
#if FELUCCA_SHDRUM
        {
            int16_t f[8];
            memcpy(f, e, sizeof f);
            f[0] = DK_SH;
            host_preset(&trk[0], ENGI_DRUM, DRUM_SH0);
            a = strike(f, &LANE_NOTE[1], 1, 300, 0, 300);
            check("the SH kit still plays", a.peak > 1000.0 && a.peak < 60000.0, "%.0f", a.peak);
        }
#endif
    }
    printf("drumx: %s\n", fails ? "FAILED" : "all ok");
    return fails ? 1 : 0;
}
