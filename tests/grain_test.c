/* SPDX-License-Identifier: GPL-3.0-only
 * Host test of the INSERT's GRAIN type (firmware/src/fx.c ig_spawn / ins_grain), same sources as the firmware (through hostsim.c).
 * (It was the SLICER's GRAIN mode up to the Argo 1.7.0 build; the slicer keeps GATE and STUT.)
 * 1. MIX 0 and TYPE OFF are transparent: the signal bit for bit
 * 2. no clicks, no overflow: a 110 Hz sine through GRAIN at every SIZE, PITCH and SPRAY, the values turned while it runs: the largest
 *    sample step stays within a few times the sine's own, and the output never exceeds the input's peak by more than 15 %
 * 3. it grains: at MIX 127 the output is not the input, its level is near the input's, it has no DC; every control changes it
 * 4. PITCH is what it says: a 220 Hz sine comes out at 110, 220, 440 Hz for -12, 0, +12 semitones (the strongest frequency;
 *    SPRAY 0: each grain starts where the pitch needs it, so they join in phase)
 * 5. repeatable: from a cleared state the same input gives the same output, bit for bit
 * 6. the tail: TYPE OFF fades the grains out, then the signal is untouched and ins_gr (the buffer's owner) is 0 again
 * 7. the buffer: with GRAIN running STUT repeats nothing (the slicer's output is the insert's), and STUT records again after;
 *    perform.c holding it (sl_lent): the track plays dry
 * 8. a stored SLICER GRAIN (3) loads as OFF (param_fit), the INSERT names GRAIN's values (SIZE ms, PITCH st, SPRAY)
 * 9. a long run (60 s) with a full-scale square wave and the values turned: bounded. (Clean under -fsanitize=address; under
 *    undefined too except -fsanitize=shift: the STUT repeat's `r <<= 2` on a negative sample, slicer.c, as the drum voices have)  */
#define main hostsim_main
#include "hostsim.c"
#undef main
static int check(const char *what, int ok)
{
    printf("grain: %-92s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
static void set_ins(track_t *t, int type, int size, int pitch, int spray, int mix)
{
    t->p[P_ITYPE] = (int16_t)type;
    t->p[P_IA] = (int16_t)size;
    t->p[P_IB] = (int16_t)pitch;
    t->p[P_IC] = (int16_t)spray;
    t->p[P_IMIX] = (int16_t)mix;
}
static void reset(void)
{
    memset(sl_buf, 0, sizeof sl_buf);
    memset(sl, 0, sizeof sl);
    memset(ins, 0, sizeof ins);
    memset(ins_gr, 0, sizeof ins_gr);
    sl_lent = 0;
    trk[0].ins_run = 0;
    trk[0].p[P_SLCR] = SL_OFF;
    set_ins(&trk[0], IT_OFF, 64, 64, 48, 127);
    slicer_start();
}
typedef struct { int32_t maxstep, instep, peak, inpeak; double rms, inrms, dc; uint64_t h; } res_t;
static int32_t last_s[44100];                  /* the last second of the output of run() */
/* mix_part's order on one block: the INSERT, then the SLICER */
static void insl_chain(track_t *t, int32_t *b)
{
    if (t->p[P_ITYPE] | t->ins_run)
        track_insert(t, b, CTL);
    slicer_track(t, b, CTL);
}
/* secs of a sine (freq, amp) through track 1: turn = change the values while it runs; square: a full-scale square instead */
static res_t run(double secs, double freq, int32_t amp, int turn, int square)
{
    uint32_t f, i, frames = (uint32_t)(secs * FS) / CTL * CTL, w_from = frames > (uint32_t)FS ? frames - (uint32_t)FS : 0;
    int32_t prev = 0, pin = 0;
    double ph = 0, dph = 2 * M_PI * freq / FS, sq = 0, sqi = 0, sum = 0;
    res_t r = {0, 0, 0, 0, 0, 0, 0, 1469598103934665603ull};
    track_t *t = &trk[0];
    for (f = 0; f < frames; f += CTL) {
        int32_t b[CTL];
        if (turn && f % 2976u == 0u) {
            uint32_t k = f / 2976u;
            set_ins(t, IT_GRAIN, (int)(k * 37u % 128u), (int)(k * 53u % 128u), (int)(k * 29u % 128u), (int)(40u + k * 41u % 88u));
        }
        for (i = 0; i < CTL; i++) {
            b[i] = square ? (sin(ph) >= 0 ? amp : -amp) : (int32_t)lrint(amp * sin(ph));
            ph += dph;
            if (f + i) { int32_t d = abs(b[i] - pin); r.instep = d > r.instep ? d : r.instep; }
            pin = b[i];
            r.inpeak = abs(b[i]) > r.inpeak ? abs(b[i]) : r.inpeak;
            sqi += (double)b[i] * b[i];
        }
        insl_chain(t, b);
        for (i = 0; i < CTL; i++) {
            uint64_t h = r.h;
            if (f + i) { int32_t d = abs(b[i] - prev); r.maxstep = d > r.maxstep ? d : r.maxstep; }
            if (f >= w_from && f + i - w_from < 44100u)
                last_s[f + i - w_from] = b[i];
            prev = b[i];
            r.peak = abs(b[i]) > r.peak ? abs(b[i]) : r.peak;
            sq += (double)b[i] * b[i]; sum += b[i];
            h ^= (uint32_t)b[i]; h *= 1099511628211ull; r.h = h;
        }
    }
    r.rms = sqrt(sq / frames); r.inrms = sqrt(sqi / frames); r.dc = sum / frames;
    return r;
}
int main(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    res_t a, b, c;
    host_tracks_init();
    song.g[G_BPM] = 120;

    reset(); a = run(2.0, 110, 20000, 0, 0);
    bad += check("TYPE OFF: transparent (the output is the sine, no step beyond its own)", a.maxstep == a.instep && a.peak == a.inpeak);
    {   /* MIX 0: the same signal bit for bit */
        reset(); a = run(2.0, 110, 20000, 0, 0);
        reset(); set_ins(t, IT_GRAIN, 64, 64, 48, 0); b = run(2.0, 110, 20000, 0, 0);
        bad += check("GRAIN at MIX 0: transparent, bit for bit", a.h == b.h && !t->ins_run);
    }
    {   /* clicks and overflow */
        uint32_t sz, pi, sp, ok = 1, ok2 = 1, pitches[5] = {0, 32, 64, 96, 127}, sprays[3] = {0, 64, 127};
        int32_t worst = 0;
        for (sz = 0; sz < 16u; sz++)
            for (pi = 0; pi < 5u; pi++)
                for (sp = 0; sp < 3u; sp++) {
                    reset(); set_ins(t, IT_GRAIN, (int)(sz * 8u + 3u), (int)pitches[pi], (int)sprays[sp], 127);
                    a = run(1.5, 110, 20000, 0, 0);
                    ok &= a.maxstep <= 8 * a.instep;       /* (a grain an octave up has twice the sine's slope) */
                    ok2 &= a.peak <= a.inpeak + a.inpeak * 15 / 100;
                    worst = a.maxstep > worst ? a.maxstep : worst;
                }
        reset(); set_ins(t, IT_GRAIN, 40, 64, 64, 100); a = run(12.0, 110, 20000, 1, 0);
        ok &= a.maxstep <= 8 * a.instep; ok2 &= a.peak <= a.inpeak + a.inpeak * 15 / 100;
        bad += check("no clicks: every SIZE, PITCH and SPRAY, the values turned while it runs (step <= 8x the sine's)", ok);
        bad += check("no overflow: the output never exceeds the input's peak by more than 15 %", ok2);
        printf("grain:   (largest step %d, the sine's own %d)\n", (int)worst, (int)a.instep);
    }
    {   /* it grains */
        reset(); a = run(4.0, 110, 20000, 0, 0);
        reset(); set_ins(t, IT_GRAIN, 64, 64, 48, 127); b = run(4.0, 110, 20000, 0, 0);
        bad += check("at MIX 127 the output is not the input", a.h != b.h);
        bad += check("its level is near the input's (0.4 .. 1.15 x the RMS)", b.rms > 0.4 * b.inrms && b.rms < 1.15 * b.inrms);
        bad += check("no DC (under 1 % of the peak)", fabs(b.dc) < 0.01 * b.inpeak);
        reset(); set_ins(t, IT_GRAIN, 24, 64, 48, 127); c = run(4.0, 110, 20000, 0, 0);
        bad += check("another SIZE grains another way", c.h != b.h);
        reset(); set_ins(t, IT_GRAIN, 64, 90, 48, 127); c = run(4.0, 110, 20000, 0, 0);
        bad += check("another PITCH grains another way", c.h != b.h);
        reset(); set_ins(t, IT_GRAIN, 64, 64, 120, 127); c = run(4.0, 110, 20000, 0, 0);
        bad += check("another SPRAY grains another way", c.h != b.h);
        reset(); set_ins(t, IT_GRAIN, 64, 64, 48, 60); c = run(4.0, 110, 20000, 0, 0);
        bad += check("a lower MIX is another sound, still near the level", c.h != b.h && c.rms > 0.4 * c.inrms && c.rms < 1.15 * c.inrms);
    }
    {   /* PITCH: the strongest frequency (40 .. 1000 Hz, 2 Hz steps) of the last second of a 220 Hz sine */
        double fq[3];
        uint32_t pv, n;
        static const int pitch[3] = {0, 64, 127};
        for (pv = 0; pv < 3u; pv++) {
            double best = 0, f;
            reset(); set_ins(t, IT_GRAIN, 64, pitch[pv], 0, 127); (void)run(3.0, 220, 20000, 0, 0);
            fq[pv] = 0;
            for (f = 40; f < 1000; f += 2) {
                double re = 0, im = 0;
                for (n = 0; n < 44100u; n++) {
                    double a = 2 * M_PI * f * n / FS;
                    re += last_s[n] * cos(a);
                    im += last_s[n] * sin(a);
                }
                if (re * re + im * im > best) {
                    best = re * re + im * im;
                    fq[pv] = f;
                }
            }
        }
        printf("grain:   (the strongest frequency of a 220 Hz sine: -12 st %.0f Hz, 0 st %.0f Hz, +12 st %.0f Hz)\n", fq[0], fq[1], fq[2]);
        bad += check("PITCH 0 st keeps the pitch (220 Hz +- 2)", fabs(fq[1] - 220) <= 2);
        bad += check("PITCH -12 st: an octave down (110 Hz +- 6)", fabs(fq[0] - 110) <= 6);
        bad += check("PITCH +12 st: an octave up (440 Hz +- 6)", fabs(fq[2] - 440) <= 6);
    }
    {   /* repeatable */
        reset(); set_ins(t, IT_GRAIN, 70, 80, 90, 100); a = run(3.0, 220, 15000, 0, 0);
        reset(); set_ins(t, IT_GRAIN, 70, 80, 90, 100); b = run(3.0, 220, 15000, 0, 0);
        bad += check("repeatable from a cleared state: the same input, the same output", a.h == b.h);
    }
    {   /* the tail and the buffer's owner */
        uint32_t k, i;
        int own_on, own_off, clean = 1;
        reset(); set_ins(t, IT_GRAIN, 64, 64, 48, 127);
        (void)run(1.0, 110, 20000, 0, 0);
        own_on = ins_gr[0] && t->ins_run;
        t->p[P_ITYPE] = IT_OFF;
        for (k = 0; k < 200u; k++) {                    /* ~ 0.15 s of blocks */
            int32_t bf[CTL];
            for (i = 0; i < CTL; i++) bf[i] = 5000;
            insl_chain(t, bf);
        }
        own_off = ins_gr[0] || t->ins_run;
        {
            int32_t bf[CTL];
            for (i = 0; i < CTL; i++) bf[i] = 5000;
            insl_chain(t, bf);
            for (i = 0; i < CTL; i++) clean &= bf[i] == 5000;
        }
        bad += check("TYPE OFF: it fades out within 150 ms, ins_gr is 0 again and the signal is untouched", own_on && !own_off && clean);
    }
    {   /* the buffer: STUT while GRAIN runs, sl_lent */
        uint32_t f, i, same = 1, rec_after = 0;
        reset(); set_ins(t, IT_GRAIN, 64, 70, 48, 127); t->p[P_SLCR] = SL_STUT; t->p[P_SLPAT] = 3; t->p[P_SLRATE] = 1; t->p[P_SLDEPTH] = 127;
        for (f = 0; f < 3u * (uint32_t)FS; f += CTL) {
            int32_t b1[CTL];
            for (i = 0; i < CTL; i++) b1[i] = (int32_t)lrint(15000 * sin((f + i) * 0.031));
            t->p[P_SLCR] = SL_STUT;
            insl_chain(t, b1);
            same &= sl[0].loop == 0u && !sl[0].rec_on;
        }
        bad += check("GRAIN running: STUT records nothing and repeats nothing (it plays live)", same);
        {   /* the same chain with the slicer OFF: bit for bit the insert's own output */
            uint64_t ha = 1469598103934665603ull, hb = 1469598103934665603ull;
            reset(); set_ins(t, IT_GRAIN, 64, 70, 48, 127); t->p[P_SLCR] = SL_STUT; t->p[P_SLPAT] = 3; t->p[P_SLRATE] = 1; t->p[P_SLDEPTH] = 127;
            for (f = 0; f < 3u * (uint32_t)FS; f += CTL) {
                int32_t b1[CTL];
                for (i = 0; i < CTL; i++) b1[i] = (int32_t)lrint(15000 * sin((f + i) * 0.031));
                insl_chain(t, b1);
                for (i = 0; i < CTL; i++) ha = (ha ^ (uint32_t)b1[i]) * 1099511628211ull;
            }
            reset(); set_ins(t, IT_GRAIN, 64, 70, 48, 127);
            for (f = 0; f < 3u * (uint32_t)FS; f += CTL) {
                int32_t b1[CTL];
                for (i = 0; i < CTL; i++) b1[i] = (int32_t)lrint(15000 * sin((f + i) * 0.031));
                insl_chain(t, b1);
                for (i = 0; i < CTL; i++) hb = (hb ^ (uint32_t)b1[i]) * 1099511628211ull;
            }
            bad += check("GRAIN with STUT on is bit for bit GRAIN with the slicer OFF", ha == hb);
        }
        {   /* GRAIN off: STUT works again */
            reset(); t->p[P_SLCR] = SL_STUT; t->p[P_SLPAT] = 3; t->p[P_SLRATE] = 1; t->p[P_SLDEPTH] = 127;
            set_ins(t, IT_GRAIN, 64, 64, 48, 127);
            for (f = 0; f < (uint32_t)FS; f += CTL) {
                int32_t b1[CTL];
                for (i = 0; i < CTL; i++) b1[i] = (int32_t)lrint(15000 * sin((f + i) * 0.031));
                insl_chain(t, b1);
            }
            t->p[P_ITYPE] = IT_OFF;
            for (f = 0; f < 2u * (uint32_t)FS; f += CTL) {
                int32_t b1[CTL];
                for (i = 0; i < CTL; i++) b1[i] = (int32_t)lrint(15000 * sin((f + i) * 0.031));
                insl_chain(t, b1);
                rec_after |= sl[0].rec_on || sl[0].rec > 0u || sl[0].loop > 0u;
            }
            bad += check("GRAIN off again: STUT records and repeats", rec_after);
        }
        reset(); a = run(2.0, 110, 20000, 0, 0);
        reset(); sl_lent = 1; set_ins(t, IT_GRAIN, 64, 64, 48, 127); b = run(2.0, 110, 20000, 0, 0); sl_lent = 0;
        bad += check("perform.c holding the buffer: the track plays dry", a.h == b.h);
    }
    {   /* stored values and names */
        const param_desc_t *d = &TP[P_SLCR];
        char v[8];
        const char *u;
        bad += check("a stored SLICER GRAIN (3) loads as OFF, STUT (2) stays", param_fit(d, 3) == 0 && param_fit(d, 2) == 2 && param_fit(d, 1) == 1);
        bad += check("the SLICER has three modes (OFF GATE STUT)", d->max == 2);
        bad += check("INSRT keeps GRAIN as 9 (append-only: the others keep their numbers, TAPE came after)", TP[P_ITYPE].max >= IT_GRAIN && IT_GRAIN == 9 && IT_CHORUS == 8);
        bad += check("GRAIN's values are SIZE PITCH SPRAY",
                     !strcmp(ins_desc(IT_GRAIN, 0)->label, "SIZE") && !strcmp(ins_desc(IT_GRAIN, 1)->label, "PITCH") &&
                     !strcmp(ins_desc(IT_GRAIN, 2)->label, "SPRAY"));
        param_format(ins_desc(IT_GRAIN, 1), 64, v, &u);
        bad += check("PITCH 64 reads 0 st", !strcmp(v, "0") && !strcmp(u, "st"));
        param_format(ins_desc(IT_GRAIN, 1), 0, v, &u);
        bad += check("PITCH 0 reads -12", !strcmp(v, "-12"));
        param_format(ins_desc(IT_GRAIN, 1), 127, v, &u);
        bad += check("PITCH 127 reads +12", !strcmp(v, "+12"));
        param_format(ins_desc(IT_GRAIN, 0), 64, v, &u);
        bad += check("SIZE 64 reads 24 ms", !strcmp(v, "24") && !strcmp(u, "ms"));
        param_format(ins_desc(IT_GRAIN, 0), 127, v, &u);
        bad += check("SIZE 127 reads 93 ms (the longest, 4096 samples)", !strcmp(v, "93") && IG_SIZE[15] == 4096u);
    }
    {   /* a long run */
        reset(); set_ins(t, IT_GRAIN, 100, 100, 100, 127);
        a = run(60.0, 150, 32000, 1, 1);
        bad += check("60 s, a full-scale square, the values turned: bounded", a.peak <= a.inpeak + a.inpeak * 15 / 100 && a.maxstep < 70000);
    }
    printf("grain: %s\n", bad ? "FAILED" : "all ok");
    return bad ? 1 : 0;
}
