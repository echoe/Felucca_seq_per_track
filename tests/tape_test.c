/* SPDX-License-Identifier: GPL-3.0-only
 * Host test of the INSERT's TAPE type (firmware/src/fx.c ins_tape / ins_tape_shape), same sources as the firmware (hostsim.c).
 *  1. MIX 0 and TYPE OFF are transparent: the signal bit for bit
 *  2. bounded, no clicks: every DRIVE, WOW and TONE corner; a loud sine never beyond the input's peak (+5 %), a quiet one gains
 *     at most 4.7 dB, the largest step stays near the sine's own
 *  3. DRIVE: saturation (the 3rd harmonic of a 220 Hz sine grows with it) and the level made up (the RMS moves < 4.7 dB)
 *  4. WOW: the pitch of a 1 kHz sine, measured by demodulation (10 ms blocks): none at WOW 0, up to ~35 cents at 127, growing
 *  5. TONE: a 8 kHz sine is cut by TONE 70 (955 Hz), a 200 Hz one is not
 *  6. repeatable; the tail (TYPE OFF fades out, the signal untouched); the SLICER's buffer is not touched, STUT works beside it
 *  7. the INSERT names TAPE's values (DRIVE WOW TONE), TYPE 10, GRAIN stays 9
 *  8. a long run (60 s) with a full-scale square wave and the values turned: bounded */
#define main hostsim_main
#include "hostsim.c"
#undef main
static int check(const char *what, int ok)
{
    printf("tape: %-96s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
static void set_ins(track_t *t, int type, int drive, int wow, int tone, int mix)
{
    t->p[P_ITYPE] = (int16_t)type;
    t->p[P_IA] = (int16_t)drive;
    t->p[P_IB] = (int16_t)wow;
    t->p[P_IC] = (int16_t)tone;
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
    set_ins(&trk[0], IT_OFF, 40, 40, 100, 127);
    slicer_start();
}
#define MAXN (44100 * 9)
static int32_t outb[MAXN], inb[MAXN];
static uint32_t nout;
static void tp_chain(track_t *t, int32_t *b)
{
    if (t->p[P_ITYPE] | t->ins_run)
        track_insert(t, b, CTL);
    slicer_track(t, b, CTL);
}
/* secs of a sine (freq, amp) through track 1 into outb / inb */
static void run(double secs, double freq, int32_t amp)
{
    uint32_t f, i, frames = (uint32_t)(secs * FS) / CTL * CTL;
    double ph = 0, dph = 2 * M_PI * freq / FS;
    nout = frames < MAXN ? frames : MAXN;
    for (f = 0; f < nout; f += CTL) {
        int32_t b[CTL];
        for (i = 0; i < CTL; i++) {
            b[i] = (int32_t)lrint(amp * sin(ph));
            ph += dph;
            inb[f + i] = b[i];
        }
        tp_chain(&trk[0], b);
        for (i = 0; i < CTL; i++)
            outb[f + i] = b[i];
    }
}
static uint64_t hash_out(void)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t i;
    for (i = 0; i < nout; i++) h = (h ^ (uint32_t)outb[i]) * 1099511628211ull;
    return h;
}
static int32_t peak_of(const int32_t *x, uint32_t from)
{
    int32_t p = 0;
    uint32_t i;
    for (i = from; i < nout; i++) p = abs(x[i]) > p ? abs(x[i]) : p;
    return p;
}
static int32_t maxstep_of(const int32_t *x, uint32_t from)
{
    int32_t p = 0;
    uint32_t i;
    for (i = from + 1; i < nout; i++) p = abs(x[i] - x[i - 1]) > p ? abs(x[i] - x[i - 1]) : p;
    return p;
}
static double rms_of(const int32_t *x, uint32_t from)
{
    double s = 0;
    uint32_t i;
    for (i = from; i < nout; i++) s += (double)x[i] * x[i];
    return sqrt(s / (nout - from));
}
/* the amplitude of outb at freq (a DFT bin over from .. nout) */
static double tp_amp_at(double freq, uint32_t from)
{
    double re = 0, im = 0;
    uint32_t i;
    for (i = from; i < nout; i++) {
        double a = 2 * M_PI * freq * i / FS;
        re += outb[i] * cos(a);
        im += outb[i] * sin(a);
    }
    return 2 * sqrt(re * re + im * im) / (nout - from);
}
/* the largest pitch deviation of outb (a sine at freq) in cents: the phase of 10 ms blocks against freq, differentiated */
static double dev_cents(double freq)
{
    const uint32_t B = 441;
    double prev = 0, worst = 0;
    uint32_t k, i, have = 0;
    for (k = 44100 / 2 / B; (k + 1) * B <= nout; k++) {
        double re = 0, im = 0, ph, d, hz, c;
        for (i = k * B; i < (k + 1) * B; i++) {
            double a = 2 * M_PI * freq * i / FS;
            re += outb[i] * cos(a);
            im -= outb[i] * sin(a);
        }
        ph = atan2(im, re);
        if (have) {
            d = ph - prev;
            while (d > M_PI) d -= 2 * M_PI;
            while (d < -M_PI) d += 2 * M_PI;
            hz = freq - d / (2 * M_PI) * (FS / B);      /* the phase running ahead = the frequency below freq */
            c = fabs(1200.0 * log2(hz / freq));
            worst = c > worst ? c : worst;
        }
        prev = ph;
        have = 1;
    }
    return worst;
}
int main(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    host_tracks_init();
    song.g[G_BPM] = 120;

    {   /* transparent */
        uint64_t h0, h1;
        reset(); run(2.0, 110, 20000); h0 = hash_out();
        bad += check("TYPE OFF: the signal bit for bit", memcmp(inb, outb, nout * sizeof(int32_t)) == 0);
        reset(); set_ins(t, IT_TAPE, 80, 80, 80, 0); run(2.0, 110, 20000); h1 = hash_out();
        bad += check("TAPE at MIX 0: the signal bit for bit, nothing left running", h0 == h1 && !t->ins_run);
    }
    {   /* bounded, no clicks */
        uint32_t d, w, o, ok = 1, ok2 = 1, ok3 = 1, v[3] = {0, 64, 127};
        double worst_gain = 0;
        for (d = 0; d < 3u; d++)
            for (w = 0; w < 3u; w++)
                for (o = 0; o < 3u; o++) {
                    int32_t pk, ms, in_ms;
                    reset(); set_ins(t, IT_TAPE, (int)v[d], (int)v[w], (int)v[o], 127); run(2.0, 110, 20000);
                    pk = peak_of(outb, 4410); ms = maxstep_of(outb, 4410); in_ms = maxstep_of(inb, 4410);
                    ok &= pk <= peak_of(inb, 4410) * 105 / 100;
                    ok2 &= ms <= in_ms * 3;
                    reset(); set_ins(t, IT_TAPE, (int)v[d], (int)v[w], 127, 127); run(1.0, 110, 1500);
                    worst_gain = fmax(worst_gain, (double)peak_of(outb, 4410) / peak_of(inb, 4410));
                }
        ok3 = worst_gain < 1.75;
        bad += check("a loud sine (20000) never exceeds the input's peak by more than 5 %, every DRIVE / WOW / TONE corner", ok);
        bad += check("no clicks: the largest step is at most 3x the sine's own", ok2);
        printf("tape:   (a quiet sine (1500) gains at most %.2fx = %.1f dB)\n", worst_gain, 20 * log10(worst_gain));
        bad += check("a quiet sine gains at most 4.7 dB (1.75x) from the saturator's makeup", ok3);
    }
    {   /* DRIVE: saturation and level */
        double h3_lo, h3_hi, r_lo, r_hi, f1_hi;
        reset(); set_ins(t, IT_TAPE, 0, 0, 127, 127); run(3.0, 220, 16000);
        h3_lo = tp_amp_at(660, 44100); r_lo = rms_of(outb, 44100);
        reset(); set_ins(t, IT_TAPE, 127, 0, 127, 127); run(3.0, 220, 16000);
        h3_hi = tp_amp_at(660, 44100); r_hi = rms_of(outb, 44100); f1_hi = tp_amp_at(220, 44100);
        printf("tape:   (3rd harmonic %.0f at DRIVE 0, %.0f at 127 of a %.0f fundamental; RMS %.0f -> %.0f)\n", h3_lo, h3_hi, f1_hi, r_lo, r_hi);
        bad += check("DRIVE saturates: the 3rd harmonic grows 3x and passes 3 % of the fundamental", h3_hi > 3 * h3_lo && h3_hi > 0.03 * f1_hi);
        bad += check("the level is made up: the RMS moves less than 4.7 dB across DRIVE", fabs(20 * log10(r_hi / r_lo)) < 4.7);
    }
    {   /* WOW: pitch deviation */
        double c0, c64, c127;
        reset(); set_ins(t, IT_TAPE, 0, 0, 127, 127); run(8.0, 1000, 10000); c0 = dev_cents(1000);
        reset(); set_ins(t, IT_TAPE, 0, 64, 127, 127); run(8.0, 1000, 10000); c64 = dev_cents(1000);
        reset(); set_ins(t, IT_TAPE, 0, 127, 127, 127); run(8.0, 1000, 10000); c127 = dev_cents(1000);
        printf("tape:   (the largest pitch deviation: WOW 0 %.2f, 64 %.1f, 127 %.1f cents)\n", c0, c64, c127);
        bad += check("WOW 0: no pitch movement (< 0.5 cent)", c0 < 0.5);
        bad += check("WOW grows the deviation: 0 < 64 < 127", c0 < c64 && c64 < c127);
        bad += check("WOW 127: a worn tape's wander, 12 .. 45 cents", c127 > 12 && c127 < 45);
    }
    {   /* TONE */
        double hi_lo, hi_hi, lo_lo, lo_hi;
        reset(); set_ins(t, IT_TAPE, 0, 0, 70, 127); run(2.0, 8000, 10000); hi_lo = rms_of(outb, 22050);
        reset(); set_ins(t, IT_TAPE, 0, 0, 127, 127); run(2.0, 8000, 10000); hi_hi = rms_of(outb, 22050);
        reset(); set_ins(t, IT_TAPE, 0, 0, 70, 127); run(2.0, 200, 10000); lo_lo = rms_of(outb, 22050);
        reset(); set_ins(t, IT_TAPE, 0, 0, 127, 127); run(2.0, 200, 10000); lo_hi = rms_of(outb, 22050);
        printf("tape:   (8 kHz RMS %.0f at TONE 70 (955 Hz), %.0f at 127; 200 Hz %.0f, %.0f)\n", hi_lo, hi_hi, lo_lo, lo_hi);
        bad += check("TONE 70 (955 Hz) cuts 8 kHz by at least 12 dB against TONE open", hi_lo < hi_hi / 4);
        bad += check("TONE 70 leaves 200 Hz within 1 dB of TONE open", lo_lo > lo_hi * 0.89 && lo_lo < lo_hi * 1.12);
    }
    {   /* repeatable, the tail, the slicer's buffer */
        uint64_t h1, h2, sum = 0;
        uint32_t k, i;
        int own_off, clean = 1;
        reset(); set_ins(t, IT_TAPE, 70, 90, 80, 100); run(3.0, 220, 15000); h1 = hash_out();
        reset(); set_ins(t, IT_TAPE, 70, 90, 80, 100); run(3.0, 220, 15000); h2 = hash_out();
        bad += check("repeatable from a cleared state: the same input, the same output", h1 == h2);
        for (k = 0; k < SL_LEN * NTRK; k++) sum += ((int16_t *)sl_buf)[k] != 0;
        bad += check("it never touches the SLICER's buffer (no GRAIN here)", sum == 0 && !ins_gr[0]);
        t->p[P_ITYPE] = IT_OFF;
        for (k = 0; k < 200u; k++) {
            int32_t bf[CTL];
            for (i = 0; i < CTL; i++) bf[i] = 5000;
            tp_chain(t, bf);
        }
        own_off = t->ins_run;
        {
            int32_t bf[CTL];
            for (i = 0; i < CTL; i++) bf[i] = 5000;
            tp_chain(t, bf);
            for (i = 0; i < CTL; i++) clean &= bf[i] == 5000;
        }
        bad += check("TYPE OFF: it fades out, then the signal is untouched", !own_off && clean);
    }
    {   /* STUT beside TAPE */
        uint32_t f, i, rec = 0;
        reset(); set_ins(t, IT_TAPE, 40, 40, 100, 127);
        t->p[P_SLCR] = SL_STUT; t->p[P_SLPAT] = 3; t->p[P_SLRATE] = 1; t->p[P_SLDEPTH] = 127;
        for (f = 0; f < 2u * (uint32_t)FS; f += CTL) {
            int32_t b[CTL];
            for (i = 0; i < CTL; i++) b[i] = (int32_t)lrint(15000 * sin((f + i) * 0.031));
            tp_chain(t, b);
            rec |= sl[0].rec_on || sl[0].loop;
        }
        bad += check("STUT works beside TAPE (TAPE does not take the SLICER's buffer)", rec);
    }
    {   /* names */
        const param_desc_t *d;
        bad += check("INSRT has TAPE as 10 and GRAIN stays 9", TP[P_ITYPE].max == IT_TAPE && IT_TAPE == 10 && IT_GRAIN == 9);
        d = ins_desc(IT_TAPE, 0);
        bad += check("TAPE's values are DRIVE WOW TONE", !strcmp(d->label, "DRIVE") && !strcmp(ins_desc(IT_TAPE, 1)->label, "WOW") &&
                                                         !strcmp(ins_desc(IT_TAPE, 2)->label, "TONE"));
    }
    {   /* a long run */
        uint32_t f, i, frames = 60u * (uint32_t)FS / CTL * CTL;
        int32_t pk = 0;
        reset(); set_ins(t, IT_TAPE, 100, 100, 100, 127);
        for (f = 0; f < frames; f += CTL) {
            int32_t b[CTL];
            if (f % 4096u == 0u) {
                uint32_t q = f / 4096u;
                set_ins(t, IT_TAPE, (int)(q * 37u % 128u), (int)(q * 53u % 128u), (int)(q * 29u % 128u), (int)(40u + q * 41u % 88u));
            }
            for (i = 0; i < CTL; i++) b[i] = (((f + i) / 150u) & 1u) ? 32000 : -32000;
            tp_chain(t, b);
            for (i = 0; i < CTL; i++) pk = abs(b[i]) > pk ? abs(b[i]) : pk;
        }
        bad += check("60 s, a full-scale square wave, the values turned: bounded (< 33000)", pk < 33000);
    }
    printf("tape: %s\n", bad ? "FAILED" : "all ok");
    return bad ? 1 : 0;
}
