/* SPDX-License-Identifier: GPL-3.0-only
 * Host test of the SLICER's GRAIN mode (firmware/src/slicer.c slg_spawn / slg_seg), same sources as the firmware (through hostsim.c).
 * 1. DEPTH 0 and OFF are transparent: the signal bit for bit
 * 2. no clicks, no overflow: a 110 Hz sine through GRAIN at every rate, many patterns and tempi, the settings turned while it runs:
 *    the largest sample step of the output stays within a few times the sine's own, and it never exceeds the input's peak by more than 15 %
 * 3. it grains: at DEPTH 127 the output is not the input, its level is near the input's, it has no DC
 * 4. repeatable: after slicer_start and a cleared ring the same input gives the same output, bit for bit
 * 5. the tail: switching GRAIN OFF lets the sounding grains end (within 200 ms) and then the signal is untouched, slicer_busy follows
 * 6. perform.c holding the buffer (sl_lent): the track plays dry; SPREAD's side signal is never louder than it came
 * 7. a long run (60 s at 300 BPM, 1/32) with a full-scale square wave: bounded, no crash (run it with ASan: run_tests.sh) */
#define main hostsim_main
#include "hostsim.c"
#undef main
static int check(const char *what, int ok)
{
    printf("grain: %-84s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
static void set_slicer(track_t *t, int mode, int pat, int rate, int depth)
{
    t->p[P_SLCR] = (int16_t)mode;
    t->p[P_SLPAT] = (int16_t)pat;
    t->p[P_SLRATE] = (int16_t)rate;
    t->p[P_SLDEPTH] = (int16_t)depth;
}
static void reset(void)
{
    memset(sl_buf, 0, sizeof sl_buf);
    memset(sl, 0, sizeof sl);
    sl_lent = 0;
    slicer_start();
}
typedef struct { int32_t maxstep, instep, peak, inpeak; double rms, inrms, dc; uint64_t h; } res_t;
/* secs of a sine (freq, amp) through track 1: turn = change the settings while it runs; square: a full-scale square instead */
static res_t run(double secs, double freq, int32_t amp, int turn, int square)
{
    uint32_t f, i, frames = (uint32_t)(secs * FS) / CTL * CTL;
    int32_t prev = 0, pin = 0;
    double ph = 0, dph = 2 * M_PI * freq / FS, sq = 0, sqi = 0, sum = 0;
    res_t r = {0, 0, 0, 0, 0, 0, 0, 1469598103934665603ull};
    track_t *t = &trk[0];
    for (f = 0; f < frames; f += CTL) {
        int32_t b[CTL];
        if (turn && f % 2976u == 0u) {
            uint32_t k = f / 2976u;
            set_slicer(t, SL_GRAIN, (int)(1u + k * 7u % 16u), (int)(k % 6u), (int)(30u + k * 41u % 98u));
        }
        for (i = 0; i < CTL; i++) {
            b[i] = square ? (sin(ph) >= 0 ? amp : -amp) : (int32_t)lrint(amp * sin(ph));
            ph += dph;
            if (f + i) { int32_t d = abs(b[i] - pin); r.instep = d > r.instep ? d : r.instep; }
            pin = b[i];
            r.inpeak = abs(b[i]) > r.inpeak ? abs(b[i]) : r.inpeak;
            sqi += (double)b[i] * b[i];
        }
        slicer_track(t, b, CTL);
        for (i = 0; i < CTL; i++) {
            uint64_t h = r.h;
            if (f + i) { int32_t d = abs(b[i] - prev); r.maxstep = d > r.maxstep ? d : r.maxstep; }
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
    reset(); set_slicer(t, SL_OFF, 1, 1, 100); a = run(2.0, 110, 20000, 0, 0);
    bad += check("OFF: transparent (the output is the sine, no step beyond its own)", a.maxstep == a.instep && a.peak == a.inpeak);
    {   /* DEPTH 0: the same signal bit for bit */
        reset(); set_slicer(t, SL_OFF, 1, 1, 0); a = run(2.0, 110, 20000, 0, 0);
        reset(); set_slicer(t, SL_GRAIN, 3, 2, 0); b = run(2.0, 110, 20000, 0, 0);
        bad += check("GRAIN at DEPTH 0: transparent, bit for bit", a.h == b.h);
    }
    {   /* clicks and overflow */
        uint32_t rate, pat, ok = 1, ok2 = 1, bpms[3] = {70, 120, 190}, bi;
        int32_t worst = 0;
        for (bi = 0; bi < 3u; bi++)
            for (rate = 0; rate < 6u; rate++)
                for (pat = 1; pat <= 16u; pat += 5u) {
                    song.g[G_BPM] = (int16_t)bpms[bi];
                    reset(); set_slicer(t, SL_GRAIN, (int)pat, (int)rate, 127);
                    a = run(3.0, 110, 20000, 0, 0);
                    ok &= a.maxstep <= 4 * a.instep;
                    ok2 &= a.peak <= a.inpeak + a.inpeak * 15 / 100;
                    worst = a.maxstep > worst ? a.maxstep : worst;
                }
        song.g[G_BPM] = 120;
        reset(); set_slicer(t, SL_GRAIN, 1, 1, 100); a = run(12.0, 110, 20000, 1, 0);
        ok &= a.maxstep <= 4 * a.instep; ok2 &= a.peak <= a.inpeak + a.inpeak * 15 / 100;
        bad += check("no clicks: every rate, 16-step patterns, three tempi, settings turned while it runs (step <= 4x the sine's)", ok);
        bad += check("no overflow: the output never exceeds the input's peak by more than 15 %", ok2);
        printf("grain:   (largest step %d, the sine's own %d)\n", (int)worst, (int)a.instep);
    }
    {   /* it grains */
        reset(); set_slicer(t, SL_OFF, 1, 1, 100); a = run(4.0, 110, 20000, 0, 0);
        reset(); set_slicer(t, SL_GRAIN, 1, 1, 127); b = run(4.0, 110, 20000, 0, 0);
        bad += check("at DEPTH 127 the output is not the input", a.h != b.h);
        bad += check("its level is near the input's (0.4 .. 1.15 x the RMS)", b.rms > 0.4 * b.inrms && b.rms < 1.15 * b.inrms);
        bad += check("no DC (under 1 % of the peak)", fabs(b.dc) < 0.01 * b.inpeak);
        reset(); set_slicer(t, SL_GRAIN, 7, 1, 127); c = run(4.0, 110, 20000, 0, 0);
        bad += check("another pattern grains another way", c.h != b.h);
        reset(); set_slicer(t, SL_GRAIN, 1, 4, 127); c = run(4.0, 110, 20000, 0, 0);
        bad += check("another rate grains another way", c.h != b.h);
    }
    {   /* repeatable */
        reset(); set_slicer(t, SL_GRAIN, 5, 2, 90); a = run(3.0, 220, 15000, 0, 0);
        reset(); set_slicer(t, SL_GRAIN, 5, 2, 90); b = run(3.0, 220, 15000, 0, 0);
        bad += check("repeatable from a cleared ring: the same input, the same output", a.h == b.h);
    }
    {   /* the tail */
        uint32_t k, i, n = 0;
        int busy_on, busy_off, clean = 1;
        reset(); set_slicer(t, SL_GRAIN, 1, 0, 127);
        (void)run(1.0, 110, 20000, 0, 0);
        busy_on = slicer_busy(t);
        set_slicer(t, SL_OFF, 1, 0, 127);
        for (k = 0; k < 200u; k++) {                    /* ~ 0.15 s of blocks */
            int32_t bf[CTL];
            for (i = 0; i < CTL; i++) bf[i] = 5000;
            slicer_track(t, bf, CTL);
            n++;
        }
        busy_off = slicer_busy(t);
        {
            int32_t bf[CTL];
            for (i = 0; i < CTL; i++) bf[i] = 5000;
            slicer_track(t, bf, CTL);
            for (i = 0; i < CTL; i++) clean &= bf[i] == 5000;
        }
        bad += check("GRAIN OFF: the grains end within 200 ms, slicer_busy follows and the signal is untouched", busy_on && !busy_off && clean && n == 200u);
    }
    {   /* sl_lent, the side signal */
        reset(); set_slicer(t, SL_OFF, 1, 1, 100); a = run(2.0, 110, 20000, 0, 0);
        reset(); sl_lent = 1; set_slicer(t, SL_GRAIN, 1, 1, 127); b = run(2.0, 110, 20000, 0, 0); sl_lent = 0;
        bad += check("perform.c holding the buffer: the track plays dry", a.h == b.h);
        {
            uint32_t f, i, ok = 1;
            reset(); set_slicer(t, SL_GRAIN, 1, 1, 127);
            for (f = 0; f < 90000u; f += CTL) {
                int32_t bf[CTL], sd[CTL], s0[CTL];
                for (i = 0; i < CTL; i++) { bf[i] = (int32_t)lrint(15000 * sin((f + i) * 0.03)); sd[i] = s0[i] = (int32_t)lrint(9000 * sin((f + i) * 0.05)); }
                slicer_track_sd(t, bf, sd, CTL);
                for (i = 0; i < CTL; i++) ok &= abs(sd[i]) <= abs(s0[i]);
            }
            bad += check("SPREAD's side signal is never louder than it came", ok);
        }
    }
    {   /* a long run */
        song.g[G_BPM] = 300;
        reset(); set_slicer(t, SL_GRAIN, 12, 2, 127);
        a = run(60.0, 150, 32000, 0, 1);
        bad += check("60 s at 300 BPM, 1/32, a full-scale square: bounded", a.peak <= a.inpeak + a.inpeak * 15 / 100 && a.maxstep < 70000);
        song.g[G_BPM] = 120;
    }
    printf("grain: %s\n", bad ? "FAILED" : "all ok");
    return bad ? 1 : 0;
}
