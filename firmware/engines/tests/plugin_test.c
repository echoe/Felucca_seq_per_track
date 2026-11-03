/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca-Engines: the plugins (src/felucca_plugin.h, plugins/) against the built-in engines. The six engines
 * built as shared libraries (tools/build_plugin.sh), loaded at run time and played through tests/fe_host.c's
 * phrases on a host that sees them only through the plugin interface: tests/run.sh compares the lines with
 * tests/golden.txt, so each plugin sounds bit for bit as the engine built in.
 *   cc -O2 -I build -o plugin_test tests/plugin_test.c [-ldl]; plugin_test DIR (the libraries: DIR/analog.so ..)
 * plugin_test --one LIBRARY: any one plugin (yours, examples/): each preset through the phrase, its hash and peak;
 * it fails when a preset is silent or its peak is past full scale. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------- the host's part --- */
#include "fe_tables.h"
#define FE_HOST
#define track fe_track                                  /* the engines' struct track: the plugins' view */
#include "../src/felucca_plugin.h"
enum { P_ATK = FE_P_ATK, P_REL = FE_P_REL, P_FTYPE = FE_P_FTYPE, P_E0 = FE_P_E0 };
typedef fe_track_t track_t;
static fe_track_t trk[NPART];                         /* the views the plugins get */
static voice_t vv[NPART][NVOICE];                    /* the parts' voices */
static uint8_t mem[NPART][FE_MEM_MAX];
#include "../src/dsp.c"                                 /* pitch_inc, clamp: the host's own copy */

static const char *const IDS[] = {"analog", "lofi", "trio", "wheel", "noise", "phase"};
static const fe_plugin_t *PLG[6];
#define ENG_ANALOG (*PLG[0]->e)
#define ENG_LOFI (*PLG[1]->e)
static struct { const char *id; const engine_t *e; } ENGS[6];

static const struct fe_tables TABLES = {SINE, PITCH_OCT, SVF_G, TANH_Q15};
static const fe_host_t host = {FE_ABI, sizeof(fe_host_t), FE_PLATFORM_DESKTOP, trk, &TABLES, {0, 0, 0, 0}};

static const fe_plugin_t *check(const fe_plugin_t *d, const char *path)
{
    if (!d || d->magic != FE_MAGIC || d->abi != FE_ABI || d->size != sizeof(fe_plugin_t) ||
        d->e_size != sizeof(engine_t) || d->fs != FS || d->ctl != CTL || d->nvoice != NVOICE || d->npart != NPART ||
        d->e->mem > FE_MEM_MAX || !d->e->note_on || !d->e->render || !d->e->name || !d->e->presets) {
        fprintf(stderr, "plugin_test: %s is not a plugin of this host\n", path);
        exit(1);
    }
    return d;
}

static const fe_plugin_t *load_path(const char *path)
{
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    fe_entry_t f;
    if (!h || !(f = (fe_entry_t)dlsym(h, FE_ENTRY))) {
        fprintf(stderr, "plugin_test: %s: %s\n", path, h ? "no " FE_ENTRY : dlerror());
        exit(1);
    }
    return check(f(&host), path);
}

static const fe_plugin_t *load(const char *dir, const char *id)
{
    static const char *const EXT[] = {"so", "dylib", "dll"};
    char path[1024];
    uint32_t k;
    for (k = 0; k < 3u; k++) {
        void *h;
        fe_entry_t f;
        const fe_plugin_t *d;
        snprintf(path, sizeof path, "%s/%s.%s", dir, id, EXT[k]);
        if (!(h = dlopen(path, RTLD_NOW | RTLD_LOCAL)))
            continue;
        f = (fe_entry_t)dlsym(h, FE_ENTRY);
        d = f ? f(&host) : 0;
        return check(d, path);
    }
    fprintf(stderr, "plugin_test: no %s/%s.{so,dylib,dll}\n", dir, id);
    exit(1);
}

/* ------------------------------------------------- a minimal voice host --- */
static uint32_t vage;

static void voice_on(track_t *t, const engine_t *e, voice_t *v, uint32_t note, int32_t fine)
{
    int sounding = v->active && v->stage != 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t s[8];
    memcpy(s, v->s, sizeof s);
    v->note = (uint8_t)note;
    v->vel = 100;
    v->gate = 1;
    v->active = 1;
    v->stage = 1;
    v->age = ++vage;
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    v->fine = fine;
    if (!sounding)
        v->env = v->env_out = 0;
    e->note_on(t, v);
    if (sounding) {                                     /* a retrigger keeps the phases and the keep slots */
        uint32_t k;
        v->ph[0] = ph0;
        v->ph[1] = ph1;
        v->ph[2] = ph2;
        for (k = 0; k < 8u; k++)
            if (k != 2u && k != 3u && (e->keep >> k & 1u))
                v->s[k] = s[k];
    }
}

/* a plain envelope, once per block: attack in 4 blocks, sustain 3/4, release by 1/16 a block */
static int32_t env_tick(voice_t *v)
{
    if (!v->active)
        return 0;
    switch (v->stage) {
    case 1:
        v->env += 1 << 22;
        if (v->env >= 1 << 24) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 2:
        v->env -= (v->env - (3 << 22)) >> 3;
        break;
    case 3:
        v->env -= v->env >> 4;
        if (v->env < (1 << 24) / 1000) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
        }
        break;
    default:
        break;
    }
    return v->env >> 9;                                 /* Q15 */
}

#define NBLK 240

typedef struct { uint64_t h; int32_t peak; } result_t;

/* the phrase: a low note, a second voice an octave and a fifth up (detuned) over it, the first voice
 * retriggered on another note, a third note, all released, the tails; the cutoff and the shape move */
static result_t render(const engine_t *e, const preset_t *pr, uint32_t part, int32_t ftype)
{
    track_t *t = &trk[part];
    const fe_plugin_t *pl = PLG[0];
    int32_t out[CTL];
    int32_t last[NVOICE] = {0};
    uint32_t b, i, k;
    result_t r = {14695981039346656037ull, 0};
    for (k = 0; k < 6u; k++)                            /* the plugin of the engine */
        if (PLG[k] && PLG[k]->e == e)
            pl = PLG[k];
    memset(t, 0, sizeof *t);
    memset(vv[part], 0, sizeof vv[part]);
    memset(mem[part], 0, sizeof mem[part]);
    t->part = (uint16_t)part;
    t->v = vv[part];
    t->mem = mem[part];
    if (pl->reset)                                      /* WHEEL's own state: each render from power-on */
        pl->reset();
    vage = 0;
    for (k = 0; k < 8u; k++)
        t->p[P_E0 + k] = pr->e[k];
    t->p[P_ATK] = pr->env[0];
    t->p[P_REL] = pr->env[3];
    t->p[P_FTYPE] = (int16_t)ftype;
    for (b = 0; b < NBLK; b++) {
        if (b == 0)
            voice_on(t, e, &t->v[0], 43, 0);
        if (b == 30)
            voice_on(t, e, &t->v[1], 62, 23);
        if (b == 70)
            voice_on(t, e, &t->v[0], 50, 0);
        if (b == 100)
            voice_on(t, e, &t->v[2], 79, -17);
        if (b == 150)
            for (k = 0; k < NVOICE; k++)
                if (t->v[k].active) {
                    t->v[k].gate = 0;
                    t->v[k].stage = 3;
                }
        if (e->block)
            e->block(t);
        memset(out, 0, sizeof out);
        for (k = 0; k < NVOICE; k++) {
            voice_t *v = &t->v[k];
            vmod_t m;
            int32_t a;
            uint32_t base;
            if (!v->active)
                continue;
            a = env_tick(v);
            if (e->amp)
                a = e->amp(t, v, a);
            if (!v->active)
                continue;
            base = pitch_inc((uint32_t)clamp(v->pitch16, 0, 2047));
            memset(&m, 0, sizeof m);
            m.pitch16 = v->pitch16;
            m.inc = base + (uint32_t)((int32_t)(base >> 12) * v->fine);
            m.fine = v->fine;
            m.amp0 = last[k];
            m.amp1 = a;
            m.envq15 = a;
            m.cutoff = ((int32_t)(b % 96u) - 48) * 128; /* a slow triangle-ish sweep of +-24 << 8 */
            m.shape = (64 << 8) + ((int32_t)(b % 64u) - 32) * 256;
            last[k] = a;
            e->render(t, v, out, CTL, &m);
        }
        for (i = 0; i < CTL; i++) {
            uint32_t x = (uint32_t)out[i];
            int32_t a = out[i] < 0 ? -out[i] : out[i];
            for (k = 0; k < 4u; k++) {
                r.h ^= (x >> (8u * k)) & 0xFFu;
                r.h *= 1099511628211ull;
            }
            if (a > r.peak)
                r.peak = a;
        }
    }
    return r;
}

/* one plugin: each preset, -> 0 when every one sounds within full scale */
static int one(const char *path)
{
    uint32_t j, bad = 0;
    const engine_t *e;
    PLG[0] = load_path(path);
    e = PLG[0]->e;
    printf("%s: %s, id %08x, version %u, engine %s\n", path, e->name, (unsigned)PLG[0]->id, (unsigned)PLG[0]->version,
           PLG[0]->engine == FE_NEW ? "new" : "replaces a built-in one");
    for (j = 0; j < e->npresets; j++) {
        result_t r = render(e, &e->presets[j], j % NPART, 0);
        int ok = r.peak > 0 && r.peak <= 32767 * 2;
        printf("  %-8s %016llx peak %6d%s\n", e->presets[j].name, (unsigned long long)r.h, r.peak,
               ok ? "" : r.peak ? "  past full scale" : "  silent");
        bad += !ok;
    }
    return bad != 0;
}

int main(int argc, char **argv)
{
    uint32_t n, j, c = 0;
    if (argc > 2 && !strcmp(argv[1], "--one"))
        return one(argv[2]);
    for (n = 0; n < 6u; n++) {
        PLG[n] = load(argc > 1 ? argv[1] : "build/plugins", IDS[n]);
        ENGS[n].id = IDS[n];
        ENGS[n].e = PLG[n]->e;
    }
    for (n = 0; n < NELEM(ENGS); n++) {
        const engine_t *e = ENGS[n].e;
        for (j = 0; j < e->npresets; j++, c++) {
            result_t r = render(e, &e->presets[j], c % NPART, 0);
            printf("%s/%u %016llx %d %s\n", ENGS[n].id, j, (unsigned long long)r.h, r.peak, e->presets[j].name);
        }
    }
    for (j = 0; j < ENG_ANALOG.npresets; j++) {         /* ANALOG's filter types: band-pass, high-pass */
        int32_t ft;
        for (ft = 1; ft <= 2; ft++) {
            result_t r = render(&ENG_ANALOG, &ENG_ANALOG.presets[j], j % NPART, ft);
            printf("analog/%u/ftype%d %016llx %d %s\n", j, ft, (unsigned long long)r.h, r.peak,
                   ENG_ANALOG.presets[j].name);
        }
    }
    {   /* the modes the presets do not reach: every value of E0 (ANALOG WAVE incl. SYNC / SUB, LOFI CHIP,
         * TRIO WAVE, WHEEL REG, NOISE MODE) and LOFI's WAVE, on the engine's defaults */
        for (n = 0; n < NELEM(ENGS); n++) {
            const engine_t *e = ENGS[n].e;
            preset_t pr;
            int32_t v;
            memset(&pr, 0, sizeof pr);
            for (j = 0; j < 8u; j++)
                pr.e[j] = (int8_t)e->edit[j].def;
            pr.env[3] = 40;
            for (v = e->edit[0].min; v <= e->edit[0].max; v++) {
                result_t r;
                pr.e[0] = (int8_t)v;
                r = render(e, &pr, (uint32_t)v % NPART, 0);
                printf("%s/e0=%d %016llx %d\n", ENGS[n].id, v, (unsigned long long)r.h, r.peak);
            }
        }
        {
            const engine_t *e = &ENG_LOFI;
            preset_t pr;
            int32_t v;
            memset(&pr, 0, sizeof pr);
            for (j = 0; j < 8u; j++)
                pr.e[j] = (int8_t)e->edit[j].def;
            for (v = e->edit[1].min; v <= e->edit[1].max; v++) {
                result_t r;
                pr.e[1] = (int8_t)v;
                r = render(e, &pr, 0, 0);
                printf("lofi/wave=%d %016llx %d\n", v, (unsigned long long)r.h, r.peak);
            }
        }
    }
    return 0;
}
