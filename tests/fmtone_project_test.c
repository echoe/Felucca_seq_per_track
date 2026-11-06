/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from Mini-Tone (https://github.com/eldontyrellcorp/Mini-Tone, GPL-3.0-only); engine numbers changed for this tree (SWARM 15, FMTONE 16) */
/* FMTONE through the stores (src/project.c, src/upreset.c, the editor's value clamp): the operator slots keep their
 * values, the negative ones (FTO A / B1 / B2, -64..63) included, through a project save -> import -> restore; an
 * out-of-range stored value is clamped to the FMTONE range (not TP's 0..127, which would turn every negative FTO
 * into 0); the same store read as another engine clamps to TP's.
 *   build/host/fmtone_project_test        (needs build/gen) */
#define EDITOR_TEST_NO_MAIN 1
#include "editor_test.c"

static int bad;
static void chk(const char *what, int ok)
{
    printf("fmtone-project: %-92s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

int main(void)
{
    static project_t a, c;
    static project_store_t st;
    uint32_t i;
    int ok;
    reset();
    host_preset(&trk[1], ENGI_FMTONE, 3);                    /* BRASS on part 2: its own slots */
    trk[1].p[P_FM3_REL] = -30;                               /* FTO A */
    trk[1].p[P_FM4_ATK] = 20;                                /* FTO B1 */
    trk[1].p[P_FM4_DEC] = -64;                               /* FTO B2 at its minimum */
    trk[1].p[P_FM3_ATK] = 77;                                /* TRK A */
    trk[1].p[P_FM1_REL] = 55;                                /* DEL A */
    project_capture(&a);
    chk("the capture holds -30 / 20 / -64 as the track has them",
          a.t[1].p[P_FM3_REL] == -30 && a.t[1].p[P_FM4_ATK] == 20 && a.t[1].p[P_FM4_DEC] == -64);
    chk("the engine byte is 16", a.t[1].engine == ENGI_FMTONE);
    ok = proj_pack(&st, &a);
    chk("the project packs", ok);
    memset(&c, 0, sizeof c);
    ok = proj_import(&c, &st, (int)PROJ_STORE_SIZE);
    chk("it imports", ok);
    chk("FTO A -30, FTO B1 20, FTO B2 -64, TRK A 77, DEL A 55 are the same after the store",
          c.t[1].p[P_FM3_REL] == -30 && c.t[1].p[P_FM4_ATK] == 20 && c.t[1].p[P_FM4_DEC] == -64 && c.t[1].p[P_FM3_ATK] == 77 &&
          c.t[1].p[P_FM1_REL] == 55);
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_FM3_REL] = 5;
    project_restore_runtime(&c);
    chk("restored into the running track: the negative values are there", trk[1].p[P_FM3_REL] == -30 && trk[1].p[P_FM4_DEC] == -64);
    chk("the engine and its preset are restored", trk[1].eng_req == ENGI_FMTONE && trk[1].preset == 3);

    /* a stored value out of FMTONE's range is clamped to it (imports that go through proj_bound) */
    a.t[1].p[P_FM3_REL] = -120;
    a.t[1].p[P_FM4_ATK] = 100;
    a.sum = proj_sum(&a);
    proj_bound(&a);
    chk("-120 is clamped to -64 and 100 to 63 for FMTONE (the engine's range, not TP's)",
          a.t[1].p[P_FM3_REL] == -64 && a.t[1].p[P_FM4_ATK] == 63);
    /* the same slots on ANALOG are TP's: 0..127 */
    a.t[1].engine = 0;
    a.t[1].p[P_FM3_REL] = -120;
    a.t[1].p[P_FM4_ATK] = 100;
    proj_bound(&a);
    chk("for another engine (ANALOG) the same slots clamp to TP's 0..127: -120 -> 0, 100 stays",
          a.t[1].p[P_FM3_REL] == 0 && a.t[1].p[P_FM4_ATK] == 100);
    printf("fmtone-project: %s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
