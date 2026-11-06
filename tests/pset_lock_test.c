/* SPDX-License-Identifier: GPL-3.0-only
 * Host test of the PRESET LOCK (core.h MOTION_PSET, motion.c motion_step, ui.c motion_pset_apply): the real motion, preset and project sources.
 * 1. its step plays what a sound load of that preset sets (compared with apply_preset_to on another track) for every engine one can pick:
 *    the engine's knobs, the envelope, the sends, the per-engine slots; the voice mode, the preset number and the track's own settings stay
 * 2. the next step puts every parameter back (the sound's own value, or the latest automation)
 * 3. a parameter lock on the same step plays over it; a step whose chance failed (play 0) plays nothing; a track whose engine is changing, nothing
 * 4. the value is a preset index (past the list: wraps); a preset is only ever a LOCK (automation turned into one); the store keeps it
 * 5. through the project store and its validation; engine change drops it like every engine-bound record */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int pl_same_sound(const track_t *a, const track_t *b, const char *what, uint32_t e)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++) {
        int skip = i == P_VOICE || (i < P_E0 && param_kept(i)) || i == P_ED_FX;
        if (!skip && a->p[i] != b->p[i]) {
            printf("pset: (%s, engine %u: parameter %u is %d, a sound load has %d)\n", what, (unsigned)e, (unsigned)i, a->p[i], b->p[i]);
            return 0;
        }
    }
    return 1;
}
int main(void)
{
    int bad = 0;
    uint32_t e, ok1 = 1, ok2 = 1;
    track_t *t, *r;
    ui_power_on();
    t = &trk[0];
    r = &trk[1];
    for (e = 0; e < NENGINES; e++) {
        uint32_t np, pi, base;
        int16_t snap[P_COUNT];
        if (!eng_ok(e) || !(np = ENGINES[e]->npresets)) continue;
        pi = np > 2u ? 2u : np - 1u;
        base = 0;
        set_engine_of(t, e); t->engine = t->eng_req;
        set_engine_of(r, e); r->engine = r->eng_req;
        apply_preset_to(t, base);                       /* the track plays preset `base` */
        memcpy(snap, t->p, sizeof snap);
        apply_preset_to(r, pi);                         /* the reference: what a sound load of `pi` sets */
        memset(&motion, 0, sizeof motion);
        motion_clear(t);
        ok1 &= !motion_set_lock(t, 2, MOTION_PSET, (int16_t)pi);
        motion_set_enabled(t, 1);
        motion_begin();
        motion_step(t, 0, &motion, 1);
        motion_step(t, 1, &motion, 1);
        ok1 &= !memcmp(t->p, snap, sizeof snap) && t->preset == base;   /* (nothing before its step) */
        motion_step(t, 2, &motion, 1);
        ok1 &= pl_same_sound(t, r, "at the lock's step", e);
        ok1 &= t->p[P_VOICE] == snap[P_VOICE] && t->preset == base;
        motion_step(t, 3, &motion, 1);
        {
            uint32_t i, back = 1;
            for (i = 0; i < P_COUNT; i++)
                if (t->p[i] != snap[i]) { back = 0; printf("pset: (engine %u: parameter %u not put back: %d, was %d)\n", (unsigned)e, (unsigned)i, t->p[i], snap[i]); }
            ok2 &= back;
        }
        motion_end();
        motion_clear(t);
    }
    bad += check("its step plays what a sound load of the preset sets, for every engine (voice mode and preset number stay)", ok1);
    bad += check("the next step puts every parameter back", ok2);
    {   /* an engine with slots and a drum, in detail */
        int16_t snap[P_COUNT], v;
        set_engine_of(t, ENGI_SWARM); t->engine = t->eng_req; apply_preset_to(t, 0);
        memcpy(snap, t->p, sizeof snap);
        motion_clear(t);
        bad += check("a preset lock is added at a step", !motion_set_lock(t, 4, MOTION_PSET, 5) && motion_lock_get(t, 4, MOTION_PSET, &v) && v == 5);
        bad += check("a preset is only ever a LOCK: as automation it becomes one", !motion_set_event(t, 6, MOTION_PSET, 3) && (motion.event[motion.count - 1u].param & MOTION_LOCK) != 0u);
        bad += check("its value is checked (a preset index 0..127)", motion_set_lock(t, 7, MOTION_PSET, 200) == 1);
        motion_set_enabled(t, 1);
        motion_begin();
        motion_step(t, 4, &motion, 0);
        bad += check("a step whose chance failed (play 0) plays no lock", !memcmp(t->p, snap, sizeof snap));
        motion_step(t, 5, &motion, 1);
        {   /* wraps past the list: 100 % the list's length */
            int16_t ref[P_COUNT];
            uint32_t np = ENGINES[ENGI_SWARM]->npresets;
            motion_end();
            motion_clear(t);
            motion_set_lock(t, 1, MOTION_PSET, 100);
            motion_set_enabled(t, 1); motion_begin();
            motion_step(t, 1, &motion, 1);
            memcpy(ref, t->p, sizeof ref);
            motion_end(); motion_clear(t);
            motion_set_lock(t, 1, MOTION_PSET, (int16_t)(100u % np));
            motion_set_enabled(t, 1); motion_begin();
            motion_step(t, 1, &motion, 1);
            bad += check("a value past the preset list wraps (100 on SWARM: 100 % its count)", !memcmp(ref, t->p, sizeof ref));
            motion_end(); motion_clear(t);
        }
        /* a parameter lock on the same step plays over it */
        motion_set_lock(t, 2, MOTION_PSET, 4);
        motion_set_lock(t, 2, P_E4, 77);
        motion_set_enabled(t, 1); motion_begin();
        motion_step(t, 2, &motion, 1);
        {
            track_t *q = &trk[2];
            set_engine_of(q, ENGI_SWARM); q->engine = q->eng_req;
            apply_preset_to(q, 4);
            bad += check("a parameter lock on the same step plays over the preset's value", t->p[P_E4] == 77 && q->p[P_E4] != 77 && t->p[P_E1] == q->p[P_E1] && t->p[P_E3] == q->p[P_E3]);
        }
        motion_step(t, 3, &motion, 1);
        bad += check("... and both go back at the next step", !memcmp(t->p, snap, sizeof snap));
        motion_end(); motion_clear(t);
        /* the engine changing: nothing */
        motion_set_lock(t, 1, MOTION_PSET, 4);
        motion_set_enabled(t, 1); motion_begin();
        t->eng_req = ENGI_WAVETONE;                      /* (a change under way: the engine follows after its fade) */
        memcpy(snap, t->p, sizeof snap);
        motion_step(t, 1, &motion, 1);
        bad += check("a track whose engine is changing takes no preset lock", !memcmp(t->p, snap, sizeof snap));
        t->eng_req = t->engine;
        motion_end(); motion_clear(t);
    }
    {   /* the store */
        project_t q;
        project_store_t st;
        project_t back;
        set_engine_of(t, ENGI_FMTONE); t->engine = t->eng_req; apply_preset_to(t, 0);
        motion_clear(t);
        motion_set_lock(t, 3, MOTION_PSET, 6);
        motion_set_lock(t, 9, MOTION_PSET, 2);
        motion_set_lock(t, 9, P_E1, 50);
        motion_set_enabled(t, 1);
        project_capture(&q);
        bad += check("the project holds the preset locks (valid, packs)", motion_valid(&q.motion) && proj_pack(&st, &q));
        memset(&back, 0, sizeof back);
        {
            uint32_t i, j, all = proj_import(&back, &st, sizeof st) && back.motion.count == q.motion.count;
            for (i = 0; all && i < q.motion.count; i++) {
                for (j = 0; j < back.motion.count; j++)
                    if (!memcmp(&back.motion.event[j], &q.motion.event[i], sizeof q.motion.event[0])) break;
                all &= j < back.motion.count;
            }
            bad += check("... and loads them back as they were (every record, id, kind and value)", all);
        }
        motion_clear(t);
        motion_set_lock(t, 3, MOTION_PSET, 6);
        motion_set_enabled(t, 1);
        set_engine_of(t, ENGI_DRUM);                     /* another engine: its presets are another list */
        project_capture(&q);
        bad += check("a preset lock stays on the track whose engine it was recorded for (the store keeps the record)", motion_valid(&q.motion));
        motion_clear(t);
    }
    {   /* the AUTOMATION editor: + ADD a PRESET, KNOB 4 its value, the name, OCT+ */
        const engine_t *E;
        char b[16];
        const char *unit;
        int at, v0;
        uint32_t np, ok = 1;
        ui_power_on();
        set_engine_of(TSEL, ENGI_SWARM); TSEL->engine = TSEL->eng_req;
        apply_preset_to(TSEL, 1);
        E = ENGINES[ENGI_SWARM];
        np = E->npresets;
        go_auto_add(MOTION_PSET, 6); frame();
        ok &= ui.ev_id == MOTION_PSET;                  /* (the + ADD row stands on PRESET: it is one of the choices) */
        press(B_OCTUP);                                 /* ADD */
        at = motion_find(TSEL, 6, MOTION_PSET);
        ok &= at >= 0 && (motion.event[at].param & MOTION_LOCK) && motion.event[at].value == (int16_t)TSEL->preset;
        bad += check("AUTOMATION: + ADD PRESET adds a preset lock at the step, starting at the track's preset", ok);
        v0 = at >= 0 ? motion.event[at].value : -1;
        ev_value_text(TSEL, MOTION_PSET, v0, b, &unit);
        bad += check("... its value is shown as the preset's name", at >= 0 && !strcmp(b, E->presets[v0].name));
        frame();
        turn(EN_K4, 2); at = motion_find(TSEL, 6, MOTION_PSET);
        bad += check("KNOB 4 steps the preset (up)", at >= 0 && motion.event[at].value > v0 && motion.event[at].value < (int16_t)np);
        turn(EN_K4, 100000); at = motion_find(TSEL, 6, MOTION_PSET);
        bad += check("... and holds at the end of the engine's list", at >= 0 && motion.event[at].value == (int16_t)(np - 1u));
        turn(EN_K4, -100000); at = motion_find(TSEL, 6, MOTION_PSET);
        bad += check("... and at the first", at >= 0 && motion.event[at].value == 0);
        press(B_OCTUP);                                 /* OCT+ on a record: a preset stays a lock */
        at = motion_find(TSEL, 6, MOTION_PSET);
        bad += check("OCT+ cannot turn a preset lock into automation", at >= 0 && (motion.event[at].param & MOTION_LOCK));
        {   /* the new record plays: its step sets the preset, the next puts it back */
            int16_t base[P_COUNT];
            track_t *q = &trk[1];
            memcpy(base, TSEL->p, sizeof base);
            set_engine_of(q, ENGI_SWARM); q->engine = q->eng_req; apply_preset_to(q, 0);
            motion_set_enabled(TSEL, 1);
            motion_begin();
            motion_step(TSEL, 6, &motion, 1);
            bad += check("the record plays: its step has the preset's sound", TSEL->p[P_E3] == q->p[P_E3] && TSEL->p[P_E4] == q->p[P_E4] && TSEL->preset == 1u);
            motion_step(TSEL, 7, &motion, 1);
            bad += check("... and the next step has the track's again", !memcmp(base, TSEL->p, sizeof base));
            motion_end();
        }
    }
    printf("pset: %s\n", bad ? "FAILED" : "all ok");
    return bad ? 1 : 0;
}
