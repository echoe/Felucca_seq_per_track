/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* TOOLS INIT and CLEAN INIT take the SLICER back to OFF (ui.c slicer_reset): a preset load leaves it alone (param_kept), the
 * last session's autosave brings it back at power-on, and INIT clears it. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#define FELUCCA_FLASH 1
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
static void panel_setup(void) {}

static uint8_t nor[0x100000], flash_ok = 1;
static int erase_error, fail_after = -1;
static uint32_t erases, as_erases;               /* (as_erases: the autosave's two sectors, 0xE5000 / 0xE6000) */
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    erases++;
    as_erases += off >= 0xE5000u && off < 0xE7000u;
    if (erase_error) return -8;
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (fail_after == 0) return -9;
    if (fail_after > 0) fail_after--;
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0; }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"
#include "../firmware/src/patbank_store.c"

#define PB_BASE_END (OBJ_PB_BASE + OBJ_PB_N * 2u * ST_SECTOR)
_Static_assert(PB_BASE_END <= 0xDC000u, "the sequence banks overrun the user preset banks");
_Static_assert(PB_NOBJ * PB_NSTORE == OBJ_PB_N, "objects");
_Static_assert(PB_HDR + PB_PER * PB_PAT_B <= ST_PAYLOAD_MAX, "an object does not fit a sector");
_Static_assert(PB_META_FIX + 3u * 800u <= ST_PAYLOAD_MAX, "fewer than 800 automation records fit the last object");

static int check(const char *what, int ok)
{
    printf("slicer_init: %-70s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
static void reset(void)
{
    memset(nor, 0xFF, sizeof nor);
    memset(&song, 0, sizeof song);
    memset(trk, 0, sizeof trk);
    memset(&motion, 0, sizeof motion);
    memset(motion_active, 0, sizeof motion_active);
    memset(&chain, 0, sizeof chain);
    chain_defaults(&chain_config);
    memset(proj_slot, 0, sizeof proj_slot);
    memset(&as, 0, sizeof as);
    memset(&ui, 0, sizeof ui);
    host_tracks_init();
    fm1_ms = 0;
    transport_req = 0;
    flash_ok = 1;
    erase_error = 0;
    fail_after = -1;
    ui_prefs2 = 0;
    ui_prefs3 = 0;
    cpk_defaults();
    pb_ready = 0;
    pb_dirty = 0;
    pb_clear_all();
}
static void note(uint32_t t, uint32_t i, uint32_t n)       /* step i of track t: note n */
{
    step_t *s = &trk[t].step[i];
    s->time = ST_NOTE; s->n = 1; s->note[0] = (uint8_t)n; s->vel = 100;
}
static int has(uint32_t t, uint32_t i, uint32_t n) { return trk[t].step[i].n == 1 && trk[t].step[i].note[0] == n; }
static void as_run(uint32_t ms)
{
    uint32_t end = fm1_ms + ms;
    while ((int32_t)(end - fm1_ms) > 0) {
        fm1_ms += 50u;
        autosave_poll();
    }
}
static void power_cycle(void)                              /* the RAM lost, the flash kept, the autosave restored */
{
    uint32_t k;
    memset(trk, 0, sizeof trk);
    memset(&motion, 0, sizeof motion);
    memset(motion_active, 0, sizeof motion_active);
    memset(&as, 0, sizeof as);
    memset(&ui, 0, sizeof ui);
    proj_name[0] = 0;
    chain_defaults(&chain_config);
    host_tracks_init();
    for (k = 0; k < NTRK; k++)
        trk[k].eng_req = trk[k].engine = (uint8_t)TRK_DEF[k][0];
    pb_ready = 0;
    pb_dirty = 0;
    pb_clear_all();
    (void)autosave_boot(1);
}

int main(void)
{
    int bad = 0, ok;
    uint32_t k;
    reset();

    ok = trk[0].p[P_SLCR] == 0 && trk[0].p[P_SLDEPTH] == TP[P_SLDEPTH].def;
    bad += check("power-on: the slicer is OFF on track 1", ok);

    trk[0].p[P_SLCR] = SL_STUT; trk[0].p[P_SLRATE] = 4; trk[0].p[P_SLPAT] = 7; trk[0].p[P_SLDEPTH] = 40;
    apply_preset_to(&trk[0], 2);
    ok = trk[0].p[P_SLCR] == SL_STUT && trk[0].p[P_SLRATE] == 4;
    bad += check("a preset load leaves the slicer (STUT) as it was", ok);

    as_run(80000u);                                       /* the autosave writes (idle, stopped) */
    power_cycle();
    ok = trk[0].p[P_SLCR] == SL_STUT;
    bad += check("the autosave restores the slicer mode at power-on (the reported symptom)", ok);

    init_sound();
    ok = trk[0].p[P_SLCR] == 0 && trk[0].p[P_SLPAT] == TP[P_SLPAT].def && trk[0].p[P_SLRATE] == TP[P_SLRATE].def &&
         trk[0].p[P_SLDEPTH] == TP[P_SLDEPTH].def;
    bad += check("TOOLS INIT: the slicer is back at its defaults (OFF)", ok);

    ok = 1;
    for (k = 0; k < NTRK; k++) {                          /* INIT acts on the selected track only */
        trk[k].p[P_SLCR] = 2;
        trk[k].p[P_LEVEL] = 90; trk[k].p[P_AMODE] = 2; trk[k].p[P_ROOT] = 5;
    }
    song.sel = 1;
    init_sound();
    ok &= trk[1].p[P_SLCR] == 0 && trk[0].p[P_SLCR] == 2 && trk[2].p[P_SLCR] == 2 && trk[3].p[P_SLCR] == 2;
    ok &= trk[1].p[P_LEVEL] == 90 && trk[1].p[P_AMODE] == 2 && trk[1].p[P_ROOT] == 5;   /* mix, ARP, SCL stay */
    bad += check("INIT resets the selected track's slicer only; LEVEL, ARP, SCL stay", ok);

    trk[1].p[P_SLCR] = SL_STUT; trk[1].p[P_SLDEPTH] = 10;
    clean_engine_of(&trk[1]);
    ok = trk[1].p[P_SLCR] == 0 && trk[1].p[P_SLDEPTH] == TP[P_SLDEPTH].def && trk[1].p[P_LEVEL] == 90 && trk[1].p[P_AMODE] == 2;
    bad += check("CLEAN INIT: the slicer OFF too; LEVEL and ARP stay", ok);

    printf(bad ? "slicer_init: %d FAILED\n" : "slicer_init: all ok\n", bad);
    return bad != 0;
}
