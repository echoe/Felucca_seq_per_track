/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLICER: a per-track insert, a tempo-synced 16-step gate / stutter. It works on the track's dry
 * mono signal after DIST and before LEVEL / PAN / the sends (fx.c mix_part), so the sends follow
 * the chopped sound.
 *   P_SLCR    OFF / GATE / STUT / GRAIN
 *   P_SLPAT   one of SL_NPAT patterns of 16 steps ('x' live / open / a grain starts, '.' gated / repeated / none)
 *   P_SLRATE  the step: 1/8, 1/16, 1/32, 8T, 16T, 32T
 *   P_SLDEPTH GATE: how far a '.' step closes (100 % = silent); STUT: the level of the repeat; GRAIN: the grains' level
 * GRAIN (added for this tree): the track is recorded all the time (the same 22.05 kHz buffer, a ring) and every 'x' step starts a grain:
 *   a Hann-windowed piece of the recent past, twice the step long, swing aside (neighbours overlap by half), read at a pitch of 0 / +5 / +7 / -5 /
 *   -12 semitones and from a place in the last 186 ms that both come from the step's number (hashed: the same every pass of the
 *   transport, another from step to step). Two grains at most (the older ends first). The grains replace the dry sound as far as
 *   they are heard (a window's weight), so between them the track plays on; DEPTH is how far. With perform.c holding the buffer
 *   the track plays dry.
 * GATE: the gain moves at most 1 / SL_RAMP per sample (2.9 ms from open to closed): a closing
 *   ramp ends on the step boundary, an opening one starts on it.
 * STUT: an 'x' step plays live and is recorded (22.05 kHz, 16 bit, SL_LEN samples a track); a '.'
 *   step plays that recording from its start, looped at the step length (halved until it fits the
 *   recording), cross-faded with the live sound by DEPTH. Every pass of the loop is windowed
 *   (SL_RAMP at both ends and at the step end), the cross-fade moves as the gate does.
 * The step clock: per track, always running (also with the SLICER OFF, so switching it on lands
 * in time), restarted with the transport (seq_start -> slicer_start: step 0 starts with the
 * sequencer's step 0), BPM and the track's + the global SWING as the sequencer has them (seq.c
 * step_samples); sample exact. With the SLICER OFF and no ramp left, the signal is not touched. */
#define SL_NPAT 16
#define SL_LEN 4096u                    /* recording, 22.05 kHz samples a track: 186 ms, 8 KB */
#define SL_RAMP_LOG2 7
#define SL_RAMP (1 << SL_RAMP_LOG2)     /* 128 samples, 2.9 ms */
#define SL_SLOPE (32768 / SL_RAMP)
enum { SL_OFF, SL_GATE, SL_STUT, SL_GRAIN };   /* P_SLCR (params.c N_SLCR) */

/* bit k = step k: 1 = 'x' (open / live), 0 = '.' (closed / repeat) */
static const uint16_t SL_PAT[SL_NPAT] = {
    0x5555,   /*  1 x.x.x.x.x.x.x.x. */
    0xB6DB,   /*  2 xx.xx.xx.xx.xx.x */
    0x5249,   /*  3 x..x..x..x..x.x. */
    0xB777,   /*  4 xxx.xxx.xxx.xx.x */
    0x6D6D,   /*  5 x.xx.xx.x.xx.xx. */
    0x0F0F,   /*  6 xxxx....xxxx.... */
    0x1111,   /*  7 x...x...x...x... */
    0x54A5,   /*  8 x.x..x.x..x.x.x. */
    0x5333,   /*  9 xx..xx..xx..x.x. */
    0xA929,   /* 10 x..x.x..x..x.x.x */
    0xAAFF,   /* 11 xxxxxxxx.x.x.x.x */
    0xD501,   /* 12 x.......x.x.x.xx */
    0xAAAA,   /* 13 .x.x.x.x.x.x.x.x */
    0xAB6B,   /* 14 xx.x.xx.xx.x.x.x */
    0xBB5D,   /* 15 x.xxx.x.xx.xxx.x */
    0x7597,   /* 16 xxx.x..xx.x.xxx. */
};
static const uint8_t SL_DEN[6] = {2, 4, 8, 3, 6, 12};   /* P_SLRATE (N_SLDIV): a step = 1 / DEN beats */

static int16_t sl_buf[NTRK][SL_LEN] __attribute__((section(".pool")));
typedef struct {
    uint32_t pos, len;           /* samples into the step, its length */
    uint32_t base;               /* the step without swing */
    uint32_t rp, loop;           /* repeat: read position, loop length (44.1 kHz samples), 0 = none */
    uint32_t rec;                /* recorded (22.05 kHz samples) */
    int32_t gc;                  /* gate closure, Q15: 0 = open */
    int32_t w;                   /* STUT cross-fade, Q15: 0 = live */
    int32_t half;                /* recording: the first sample of a pair */
    uint8_t idx;                 /* step 0..15 */
    uint8_t bit;                 /* this step's pattern bit (latched at its start) */
    uint8_t rec_on;              /* recording this step */
    uint16_t gw;                 /* GRAIN: the ring's write position */
    uint32_t gn;                 /* GRAIN: the step count since the transport started */
    struct {                     /* GRAIN: two grains */
        uint32_t pos, len;       /* samples into the grain, its length (44.1 kHz) */
        uint32_t rp, inc;        /* the read position in the ring, Q16 (22.05 kHz samples); its step */
        uint32_t ph, dph;        /* the window's phase (0 .. half a cycle of sine_i) and its step */
        uint8_t on;
    } g[2];
} sl_t;
static sl_t sl[NTRK];
static uint8_t sl_lent;          /* perform.c has borrowed sl_buf: STUT plays live, records nothing */

static void slicer_start(void)   /* seq_start: the next block starts step 0 of every track */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        sl[k].idx = 15;
        sl[k].pos = sl[k].len = 0;
        sl[k].gn = 0;
        sl[k].g[0].on = sl[k].g[1].on = 0;
    }
}

static uint32_t sl_pattern(const track_t *t) { return SL_PAT[(uint32_t)(t->p[P_SLPAT] - 1) % SL_NPAT]; }

/* GRAIN: the pitch ratios (Q8, the buffer's own rate 256) of the grains: 0, 0, +7, -12, -5, +5 semitones; none above 1.5 (a grain never
 * reads past the write position: spawn) */
static const uint16_t SLG_RATIO[6] = {256, 256, 384, 128, 192, 341};

/* the step starts a grain: it takes a free voice, else the older one's */
static void slg_spawn(const track_t *t, sl_t *s)
{
    uint32_t h = (s->gn + 1u) * 2654435761u ^ ((uint32_t)t->p[P_SLPAT] * 40503u + 12345u), r = SLG_RATIO[(h >> 8) % 6u], lout = 2u * s->base, lb, back, v;   /* (the step without swing: a grain ends as the one after its neighbour starts) */
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    if (lout > 4096u)
        lout = 4096u;
    lb = lout * r >> 9;                                 /* the buffer samples it reads (at half the output rate, times the ratio) */
    {                                                   /* its start, this far behind the write position. The gap moves by (what the ring gains - what the grain reads) over the grain:
                                                         * it shrinks for a ratio above 1 (so 96 samples more than the grain reads), it grows for one below (so it must stay under
                                                         * the ring's length by that much): the whole grain is in the past and none of it has been overwritten */
        uint32_t grow = lout / 2u > lb ? lout / 2u - lb : 0u, lo = lb + 96u, hi = SL_LEN - grow - 64u;
        back = lo + h % (hi - lo);
    }
    v = !s->g[0].on ? 0u : !s->g[1].on ? 1u : s->g[0].pos >= s->g[1].pos ? 0u : 1u;
    s->g[v].pos = 0;
    s->g[v].len = lout;
    s->g[v].rp = ((uint32_t)(s->gw - back) & (SL_LEN - 1u)) << 16;
    s->g[v].inc = r << 7;
    s->g[v].ph = 0;
    s->g[v].dph = 0x80000000u / lout;
    s->g[v].on = 1;
}

/* the step clock enters the next step */
static void sl_enter(const track_t *t, sl_t *s)
{
    uint32_t mode = (uint32_t)t->p[P_SLCR];
    s->idx = (uint8_t)((s->idx + 1u) & 15u);
    s->base = (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM] / SL_DEN[(uint32_t)t->p[P_SLRATE] % 6u];
    s->len = swing_step_len(t, s->base, s->idx);   /* core.h, as seq.c step_samples */
    s->pos = 0;
    s->bit = (uint8_t)((sl_pattern(t) >> s->idx) & 1u);
    s->rp = 0;
    s->loop = 0;
    s->rec_on = 0;
    if (mode == SL_GRAIN && s->bit && !sl_lent)
        slg_spawn(t, s);
    s->gn++;
    if (mode != SL_STUT || sl_lent) {
        s->rec = 0;                                 /* nothing old to repeat when STUT comes on */
    } else if (s->bit) {
        s->rec = 0;                                 /* a live step: record it */
        s->rec_on = 1;
    } else if (s->rec >= 2u * SL_RAMP) {            /* a repeat: the last live step, looped */
        uint32_t l = s->base;
        while (l > 2u * SL_LEN)
            l >>= 1;                                /* (a long step: a half, a quarter .. of it) */
        s->loop = l < 2u * s->rec ? l : 2u * s->rec;
    }
}

/* GRAIN: m samples of one step: record, play the grains, mix. sd: SPREAD's side signal (the grains are mono: it keeps as much as they
 * leave of the dry sound) */
static __attribute__((noinline)) void slg_seg(const track_t *t, sl_t *s, int16_t *buf, int32_t *b, int32_t *sd, uint32_t m)
{
    int32_t tw = t->p[P_SLCR] == SL_GRAIN ? t->p[P_SLDEPTH] * 258 : s->w;   /* Q15, 0..32766 (the mode turned off: the grains play out) */
    uint32_t j, k;
    for (j = 0; j < m; j++) {
        int32_t x = b[j], g = 0, ge = 0, y, wet;
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);  /* DEPTH moves as the GATE's and STUT's do: no click from a turn */
        wet = s->w;
        if (!sl_lent && t->p[P_SLCR] == SL_GRAIN) {                                 /* the ring: 2:1, the pair's mean; Q15 >> 2 (as STUT's) */
            if ((s->pos + j) & 1u) {
                buf[s->gw] = (int16_t)clamp((s->half + x) >> 3, -32768, 32767);
                s->gw = (uint16_t)((s->gw + 1u) & (SL_LEN - 1u));
            } else {
                s->half = x;
            }
        }
        for (k = 0; k < 2u; k++) {
            int32_t r, w, sn;
            uint32_t i0;
            if (!s->g[k].on)
                continue;
            i0 = s->g[k].rp >> 16;
            r = buf[i0 & (SL_LEN - 1u)];
            r += ((buf[(i0 + 1u) & (SL_LEN - 1u)] - r) * (int32_t)((s->g[k].rp >> 1) & 0x7FFFu)) >> 15;
            sn = sine_i(s->g[k].ph);                    /* a Hann window: sin^2 over the grain */
            w = (sn * sn) >> 15;
            g += (r * 4 * w) >> 15;
            ge += w;
            s->g[k].rp += s->g[k].inc;
            if ((s->g[k].rp >> 16) >= SL_LEN)
                s->g[k].rp -= (uint32_t)SL_LEN << 16;
            s->g[k].ph += s->g[k].dph;
            if (++s->g[k].pos >= s->g[k].len)
                s->g[k].on = 0;
        }
        if (ge > 32767)
            ge = 32767;
        ge = (int32_t)(((int64_t)ge * wet) >> 15);       /* how far the grains replace the dry sound */
        y = x - mulq16(x, (uint32_t)ge << 1) + mulq16(g, (uint32_t)wet << 1);
        if (sd)
            sd[j] -= mulq16(sd[j], (uint32_t)ge << 1);
        b[j] = y;
    }
    if (t->p[P_SLCR] != SL_GRAIN && !s->g[0].on && !s->g[1].on)
        s->w = 0;                                       /* the tail is over: the dry sound alone (the other modes use w) */
}

/* m samples of one step (no boundary inside). sd: SPREAD's side signal (fx.c mix_spread) or 0: it gets the gate and
 * the live sound's share of the cross-fade (the repeat is the mono recording: on PAN). Inlined into both callers:
 * slicer_track's (sd 0) is the code it always was */
static inline __attribute__((always_inline)) void sl_seg(const track_t *t, sl_t *s, int16_t *buf, int32_t *b,
                                                          int32_t *sd, uint32_t m)
{
    uint32_t mode = (uint32_t)t->p[P_SLCR], j, nbit = (sl_pattern(t) >> ((s->idx + 1u) & 15u)) & 1u;
    if (mode == SL_GRAIN || s->g[0].on || s->g[1].on) {   /* (GRAIN, or the grains of it that are still sounding) */
        slg_seg(t, s, buf, b, sd, m);
        return;
    }
    int32_t depth = t->p[P_SLDEPTH] * 258;          /* Q15, 0..32766 */
    int32_t tc = mode == SL_GATE && !s->bit ? depth : 0;           /* gate closure of this step */
    int32_t tn = mode == SL_GATE && !nbit ? depth : 0;             /* .. of the next one */
    int32_t tw = mode == SL_STUT && s->loop ? depth : 0;           /* repeat level */
    for (j = 0; j < m; j++) {
        int32_t x = b[j], y, tg = tc;
        uint32_t left = s->len - s->pos - j;        /* samples to the step end, >= 1 */
        if (left <= (uint32_t)SL_RAMP && tn > tg)
            tg = tn;                                /* close by the boundary */
        s->gc += clamp(tg - s->gc, -SL_SLOPE, SL_SLOPE);
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
        y = s->gc ? x - mulq16(x, (uint32_t)s->gc << 1) : x;
        if (sd) {
            int32_t z = sd[j];
            if (s->gc)
                z -= mulq16(z, (uint32_t)s->gc << 1);
            if (s->w)
                z -= mulq16(z, (uint32_t)s->w << 1);
            sd[j] = z;
        }
        if (s->rec_on) {                            /* 2:1, the pair's mean; Q15 >> 2 (the mix's headroom) */
            if ((s->pos + j) & 1u) {
                buf[s->rec] = (int16_t)clamp((s->half + x) >> 3, -32768, 32767);
                if (++s->rec >= SL_LEN)
                    s->rec_on = 0;
            } else {
                s->half = x;
            }
        }
        if (s->loop) {                              /* the repeat, windowed, read between samples */
            uint32_t rp = s->rp, e = s->loop - 1u - rp;
            int32_t r = buf[rp >> 1];
            if (rp & 1u)
                r = (r + buf[((rp >> 1) + 1u) & (SL_LEN - 1u)]) >> 1;
            r <<= 2;
            if (rp < e)
                e = rp;
            if (left - 1u < e)
                e = left - 1u;
            if (e < (uint32_t)SL_RAMP)
                r = (r * (int32_t)e) >> SL_RAMP_LOG2;
            if (++s->rp >= s->loop)
                s->rp = 0;
            y += mulq16(r - y, (uint32_t)s->w << 1);
        } else if (s->w) {
            y -= mulq16(y, (uint32_t)s->w << 1);    /* (the repeat ended on a boundary: it is 0 there) */
        }
        b[j] = y;
    }
}

/* the step clock over n samples and the SLICER on b (0: the clock only, the track is silent) */
static void slicer_track(const track_t *t, int32_t *b, uint32_t n)
{
    uint32_t k = (uint32_t)(t - trk), i = 0;
    sl_t *s = &sl[k];
    int act = b && (t->p[P_SLCR] != SL_OFF || s->gc || s->w || s->g[0].on || s->g[1].on);
    while (i < n) {
        uint32_t m;
        if (s->pos >= s->len)
            sl_enter(t, s);
        m = s->len - s->pos;
        if (m > n - i)
            m = n - i;
        if (act)
            sl_seg(t, s, sl_buf[k], b + i, 0, m);
        s->pos += m;
        i += m;
    }
}

/* slicer_track with SPREAD's side signal sd (fx.c mix_spread; b != 0) */
static __attribute__((noinline)) void slicer_track_sd(const track_t *t, int32_t *b, int32_t *sd, uint32_t n)
{
    uint32_t k = (uint32_t)(t - trk), i = 0;
    sl_t *s = &sl[k];
    int act = t->p[P_SLCR] != SL_OFF || s->gc || s->w || s->g[0].on || s->g[1].on;
    while (i < n) {
        uint32_t m;
        if (s->pos >= s->len)
            sl_enter(t, s);
        m = s->len - s->pos;
        if (m > n - i)
            m = n - i;
        if (act)
            sl_seg(t, s, sl_buf[k], b + i, sd + i, m);
        s->pos += m;
        i += m;
    }
}

/* a silent part must still be rendered: a repeat is playing or fading (fx.c mix_part) */
static int slicer_busy(const track_t *t)
{
    const sl_t *s = &sl[t - trk];
    return s->w || (t->p[P_SLCR] == SL_STUT && s->loop) || s->g[0].on || s->g[1].on;
}
