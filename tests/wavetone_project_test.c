/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from Mini-Tone (https://github.com/eldontyrellcorp/Mini-Tone, GPL-3.0-only); engine numbers changed for this tree (WAVETONE 17) */
/* WAVETONE through the stores (src/project.c, the editor's value clamp): the slots keep their values, the negative
 * ones (OFS1 / OFS2, -64..63) included, through a project save -> import -> restore; an out-of-range stored value
 * is clamped to WAVETONE's range (not TP's 0..127, which would turn every negative OFS into 0); the same store read as
 * another engine clamps to TP's.
 *   build/host/wavetone_project_test        (needs build/gen) */
#define EDITOR_TEST_NO_MAIN 1
#include "editor_test.c"

static int bad;
static void chk(const char *what, int ok)
{
    printf("wavetone-project: %-92s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

int main(void)
{
    static project_t a, c;
    static project_store_t st;
    uint32_t i;
    int ok;
    reset();
    host_preset(&trk[1], ENGI_WAVETONE, 4);                  /* DRIFT PAD on part 2: its own slots */
    trk[1].p[WP_OFS1] = -50;
    trk[1].p[WP_OFS2] = 63;
    trk[1].p[WP_INTR] = 3;
    trk[1].p[WP_NTYP] = 2;
    trk[1].p[WP_NCUT] = 33;
    project_capture(&a);
    chk("the capture holds OFS1 -50 / OFS2 63 as the track has them", a.t[1].p[WP_OFS1] == -50 && a.t[1].p[WP_OFS2] == 63);
    chk("the engine byte is 17", a.t[1].engine == ENGI_WAVETONE);
    ok = proj_pack(&st, &a);
    chk("the project packs", ok);
    memset(&c, 0, sizeof c);
    ok = proj_import(&c, &st, (int)PROJ_STORE_SIZE);
    chk("it imports", ok);
    chk("OFS1 -50, OFS2 63, INTR 3, NTYP 2, NCUT 33 are the same after the store",
        c.t[1].p[WP_OFS1] == -50 && c.t[1].p[WP_OFS2] == 63 && c.t[1].p[WP_INTR] == 3 && c.t[1].p[WP_NTYP] == 2 && c.t[1].p[WP_NCUT] == 33);
    for (i = 0; i < NTRK; i++)
        trk[i].p[WP_OFS1] = 5;
    project_restore_runtime(&c);
    chk("restored into the running track: the negative value is there", trk[1].p[WP_OFS1] == -50 && trk[1].p[WP_OFS2] == 63);
    chk("the engine and its preset are restored", trk[1].eng_req == ENGI_WAVETONE && trk[1].preset == 4);

    a.t[1].p[WP_OFS1] = -300;
    a.t[1].p[WP_OFS2] = 250;
    a.sum = proj_sum(&a);
    proj_bound(&a);
    chk("-300 is clamped to -64 and 250 to 63 for WAVETONE (the engine's range, not TP's)",
        a.t[1].p[WP_OFS1] == -64 && a.t[1].p[WP_OFS2] == 63);
    a.t[1].engine = 0;
    a.t[1].p[WP_OFS1] = -120;
    a.t[1].p[WP_OFS2] = 60;
    proj_bound(&a);
    chk("for another engine (ANALOG) the same slots clamp to TP's 0..127: -120 -> 0, 60 stays",
        a.t[1].p[WP_OFS1] == 0 && a.t[1].p[WP_OFS2] == 60);
    printf("wavetone-project: %s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
