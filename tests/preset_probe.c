/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Developer tool of tools/gen_presets.py (not a test): through the firmware's own engines (hostsim.c)
 *   preset_probe dump          every preset of the engines the generator extends: JSON, all fields (name, e, env, fenv, mono, fx, pat, category)
 *   preset_probe level E A B    the level of presets A..B-1 of engine E as the engine tests measure it (flat envelope, no sends, C4, 16384 samples after 20 blocks),
 *                               in dB against ANALOG's SAW LEAD (0.401917): one "P dB" line each
 *   preset_probe probe E A B D  renders presets A..B-1 of engine E to D/E_P.raw: 2.4 s (drums 3.6 s) of the voice sum (int32 LE, 44.1 kHz): a melodic engine
 *                               plays C4 (velocity 100) for 1 s; DRUM and SHDRUM strike their lanes (kick 0 s, snare 0.6 s, closed hat 1.2 s, tom 1.8 s) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <stdio.h>
#ifndef CAT_DRUM_INCLUDED
#include "category.c"
#endif
static char preset_cat_char(uint32_t e, uint32_t k)
{
    return e < NELEM(PRESET_CAT) && PRESET_CAT[e] && strlen(PRESET_CAT[e]) > k ? PRESET_CAT[e][k] : '?';
}
static const char *const WANT[] = {"DRUM", "SHDRUM", "SWARM", "FMTONE", "WAVETONE"};
static void jstr(const char *s) { putchar('"'); for (; *s; s++) { if (*s == '"' || *s == '\\') putchar('\\'); putchar(*s); } putchar('"'); }
static int dump(void)
{
    uint32_t w, e, i, k, first = 1;
    printf("[\n");
    for (w = 0; w < NELEM(WANT); w++)
        for (e = 0; e < NENGINES; e++) {
            const engine_t *E = ENGINES[e];
            if (strcmp(E->name, WANT[w])) continue;
            for (i = 0; i < E->npresets; i++) {
                const preset_t *p = &E->presets[i];
                if (!first) printf(",\n");
                first = 0;
                printf("{\"eng\":");
                jstr(E->name);
                printf(",\"ei\":%u,\"pi\":%u,\"name\":", (unsigned)e, (unsigned)i);
                jstr(p->name);
                printf(",\"e\":[");
                for (k = 0; k < 8; k++) printf("%s%d", k ? "," : "", p->e[k]);
                printf("],\"env\":[%u,%u,%u,%u],\"fenv\":%d,\"mono\":%u,\"fx\":[%u,%u,%u,%u],\"pat\":%u,\"cat\":\"%c\"}", p->env[0], p->env[1], p->env[2], p->env[3], p->fenv, p->mono,
                       p->fx[0], p->fx[1], p->fx[2], p->fx[3], p->pat, preset_cat_char(e, i));
            }
        }
    printf("\n]\n");
    return 0;
}
static int probe(uint32_t e, uint32_t a, uint32_t b, const char *dir)
{
    static int32_t buf[CTL];
    uint32_t pi, blk, nb;
    int drum = !strcmp(ENGINES[e]->name, "DRUM") || !strcmp(ENGINES[e]->name, "SHDRUM");
    int sh = !strcmp(ENGINES[e]->name, "SHDRUM");
    nb = (sh ? 1000u : drum ? 3600u : 2400u) * 44100u / 1000u / CTL;   /* (a DRUM-X kit gets 1.8 s after the last hit to end; a SHDRUM sound is one hit and 1 s: tests/shdrum_test.c) */
    for (pi = a; pi < b && pi < ENGINES[e]->npresets; pi++) {
        char path[256];
        FILE *f;
        memset(trk, 0, sizeof trk);
        memset(eng_mem, 0, sizeof eng_mem);
        host_tracks_init();
        host_preset(&trk[0], e, pi);
        trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
        snprintf(path, sizeof path, "%s/%u_%u.raw", dir, (unsigned)e, (unsigned)pi);
        f = fopen(path, "wb");
        if (!f) return 1;
        for (blk = 0; blk < nb; blk++) {
            uint32_t ms = blk * CTL * 1000u / 44100u;
            if (sh) {
                static const uint8_t PN[8] = {36, 50, 80, 80, 48, 60, 72, 72};   /* shdrum_test.c PNOTE */
                if (blk == 0) trk_note_on(&trk[0], PN[pi & 7u], 100);
            } else if (drum) {
                static const uint16_t AT[4] = {0, 600, 1200, 1800};
                static const uint8_t N[4] = {36, 38, 42, 45};
                uint32_t k;
                for (k = 0; k < 4; k++)
                    if (ms >= AT[k] && (blk - 1u) * CTL * 1000u / 44100u < AT[k] && blk) trk_note_on(&trk[0], N[k], 100);
                if (blk == 0) trk_note_on(&trk[0], 36, 100);
            } else {
                if (blk == 0) trk_note_on(&trk[0], 60, 100);
                if (ms >= 1000u && (blk - 1u) * CTL * 1000u / 44100u < 1000u) trk_note_off(&trk[0], 60);
            }
            memset(buf, 0, sizeof buf);
            track_render(&trk[0], buf, CTL);
            fwrite(buf, 4, CTL, f);
        }
        fclose(f);
        {                                                /* the voices still alive at the end: <E>_<P>.live */
            uint32_t i, live = 0;
            FILE *g;
            for (i = 0; i < NVOICE; i++) live += trk[0].v[i].active != 0;
            snprintf(path, sizeof path, "%s/%u_%u.live", dir, (unsigned)e, (unsigned)pi);
            g = fopen(path, "w");
            if (g) { fprintf(g, "%u\n", (unsigned)live); fclose(g); }
        }
    }
    return 0;
}
static int level(uint32_t e, uint32_t a, uint32_t b)
{
    static int32_t buf[CTL];
    uint32_t pi, i, blk;
    for (pi = a; pi < b && pi < ENGINES[e]->npresets; pi++) {
        double sq = 0;
        memset(trk, 0, sizeof trk);
        memset(eng_mem, 0, sizeof eng_mem);
        host_tracks_init();
        host_preset(&trk[0], e, pi);
        trk[0].p[P_ATK] = 0;
        trk[0].p[P_SUS] = 127;
        trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
        trk_note_on(&trk[0], 60, 100);
        for (blk = 0; blk < 20; blk++) { memset(buf, 0, sizeof buf); track_render(&trk[0], buf, CTL); }
        for (blk = 0; blk < 16384u / CTL; blk++) {
            memset(buf, 0, sizeof buf);
            track_render(&trk[0], buf, CTL);
            for (i = 0; i < CTL; i++) sq += (buf[i] / 32768.0) * (buf[i] / 32768.0);
        }
        printf("%u %.2f\n", (unsigned)pi, 20.0 * log10(sqrt(sq / 16384.0) / 0.401917 + 1e-12));
    }
    return 0;
}
int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "dump")) return dump();
    if (argc >= 5 && !strcmp(argv[1], "level")) return level((uint32_t)atoi(argv[2]), (uint32_t)atoi(argv[3]), (uint32_t)atoi(argv[4]));
    if (argc >= 6 && !strcmp(argv[1], "probe")) return probe((uint32_t)atoi(argv[2]), (uint32_t)atoi(argv[3]), (uint32_t)atoi(argv[4]), argv[5]);
    fprintf(stderr, "usage: preset_probe dump | probe ENGINE FROM TO DIR\n");
    return 2;
}
