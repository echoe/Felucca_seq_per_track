/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Chord keys (firmware/src/chord.c; SCL > CHORD: CHRD, VOIC) against the real keyboard, MIDI IN, arp, recording
 * and voice code: the diatonic triads and sevenths of several scales and roots as music theory has them, the
 * fixed shapes, the voicings, at most 4 notes, the names, MONO plays the root, a key's or a MIDI note's release
 * ends exactly the notes it started (CHRD / VOIC changed while held too), shared notes, live recording writes the
 * chord into one step, the ARP gets the chord as held notes, QNT WHITE maps the key first, a kit (the DRUM engine,
 * SAMPLE PERC) ignores CHRD, MIDI OUT of the keys, and OFF plays exactly as before.
 * Run by tests/run_tests.sh (needs build/gen from one firmware build). */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int gate_note(const track_t *t, uint32_t note)
{
    for (uint32_t i = 0; i < NVOICE; i++) if (t->v[i].active && t->v[i].gate && t->v[i].note == note) return 1;
    return 0;
}
static uint32_t ngated(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++) n += t->v[i].active && t->v[i].gate;
    return n;
}
static void reset(void)
{
    ui_power_on();
    memset(play_lat, 0, sizeof play_lat);
    memset(kb_chn, 0, sizeof kb_chn); memset(mchord, 0, sizeof mchord); memset(chord_last, 0, sizeof chord_last);
    fm1_in.notes = kb_prev = 0; mo_w = mo_r = 0; usb.config = 1;
    trk[0].p[P_VOICE] = V_POLY; trk[0].p[P_SCALE] = 1; trk[0].p[P_ROOT] = 0;   /* C major, POLY */
    events_block(CTL);
}
/* the chord track 0 plays on note r: "n1 n2 n3 [n4]" compared with want (0-terminated) */
static int chord_is(uint32_t r, const uint8_t *want)
{
    uint8_t out[CHORD_MAX];
    uint32_t n = chord_build(&trk[0], r, out), i;
    for (i = 0; i < n; i++) if (want[i] != out[i]) return 0;
    return i == 4u || !want[i];
}
static int name_is(uint32_t r, const char *want)
{
    uint8_t out[CHORD_MAX];
    int32_t root;
    uint16_t mask;
    char b[12];
    chord_make(&trk[0], r, out, &root, &mask);
    chord_name(b, (uint32_t)root, mask);
    if (!str_eq(b, want)) printf("  name of %u: %s, want %s\n", r, b, want);
    return str_eq(b, want);
}
static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    midi_enqueue(st >> 4 | st << 8 | d1 << 16 | d2 << 24, 1);
    events_block(CTL);
}
#define K_C4 7u                                   /* keys from F3 (0): C4 7, D4 9, E4 11, G4 14 */
#define K_D4 9u
#define K_E4 11u

static int theory(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    static const char *const MAJ3[7] = {"C", "Dm", "Em", "F", "G", "Am", "Bdim"};
    static const char *const MAJ7[7] = {"Cmaj7", "Dm7", "Em7", "Fmaj7", "G7", "Am7", "Bm7b5"};
    static const uint8_t CMAJ[7] = {60, 62, 64, 65, 67, 69, 71};
    uint32_t i;
    reset();
    t->p[P_CHRD] = CH_DIA3;
    bad += check("C major DIA3 on D: D F A", chord_is(62, (const uint8_t[]){62, 65, 69, 0}));
    bad += check("C major DIA3 on B: B D F (diminished)", chord_is(71, (const uint8_t[]){71, 74, 77, 0}));
    t->p[P_CHRD] = CH_DIA7;
    bad += check("C major DIA7 on G: G B D F", chord_is(67, (const uint8_t[]){67, 71, 74, 77}));
    bad += check("C major DIA7 on C: C E G B", chord_is(60, (const uint8_t[]){60, 64, 67, 71}));
    for (i = 0, ok = 1; i < 7u; i++) {
        t->p[P_CHRD] = CH_DIA3; ok &= name_is(CMAJ[i], MAJ3[i]);
        t->p[P_CHRD] = CH_DIA7; ok &= name_is(CMAJ[i], MAJ7[i]);
    }
    bad += check("C major: C Dm Em F G Am Bdim, Cmaj7 Dm7 Em7 Fmaj7 G7 Am7 Bm7b5", ok);
    t->p[P_SCALE] = 2; t->p[P_ROOT] = 9; t->p[P_CHRD] = CH_DIA3;   /* A minor */
    bad += check("A minor DIA3 on B: B D F", chord_is(71, (const uint8_t[]){71, 74, 77, 0}));
    bad += check("A minor DIA3 on A: A C E (Am), on C: C E G, on E: E G B (Em)",
                 chord_is(69, (const uint8_t[]){69, 72, 76, 0}) && chord_is(60, (const uint8_t[]){60, 64, 67, 0}) &&
                 chord_is(64, (const uint8_t[]){64, 67, 71, 0}) && name_is(69, "Am") && name_is(64, "Em"));
    t->p[P_SCALE] = 7;                                             /* A harmonic minor */
    bad += check("A harmonic minor DIA3 on E: E G# B (E), on C: C E G# (Caug)",
                 chord_is(64, (const uint8_t[]){64, 68, 71, 0}) && name_is(64, "E") &&
                 chord_is(60, (const uint8_t[]){60, 64, 68, 0}) && name_is(60, "Caug"));
    t->p[P_CHRD] = CH_DIA7;
    bad += check("A harmonic minor DIA7 on E: E G# B D (E7), on G#: G# B D F (G#dim7), on A: AmM7",
                 chord_is(64, (const uint8_t[]){64, 68, 71, 74}) && name_is(64, "E7") && name_is(68, "G#dim7") &&
                 name_is(69, "AmM7"));
    t->p[P_SCALE] = 1; t->p[P_ROOT] = 3;                           /* Eb major */
    bad += check("Eb major DIA7 on Bb: Bb D F Ab (A#7)", chord_is(58, (const uint8_t[]){58, 62, 65, 68}) && name_is(58, "A#7"));
    t->p[P_SCALE] = 3; t->p[P_ROOT] = 2; t->p[P_CHRD] = CH_DIA3;   /* D dorian */
    bad += check("D dorian DIA3 on D: Dm, on G: G (the major IV)", name_is(62, "Dm") && name_is(67, "G") &&
                 chord_is(67, (const uint8_t[]){67, 71, 74, 0}));
    t->p[P_SCALE] = 1; t->p[P_ROOT] = 0;
    bad += check("a key outside the scale: the scale note below (C# in C major: C)", chord_is(61, (const uint8_t[]){60, 64, 67, 0}));
    t->p[P_SCALE] = 0; t->p[P_ROOT] = 7;                           /* CHR: the major scale of G */
    bad += check("CHR: the major scale of ROOT (G: on A, Am; on F#, F#dim)", name_is(69, "Am") && name_is(66, "F#dim"));
    t->p[P_SCALE] = 5; t->p[P_ROOT] = 0;                           /* C major pentatonic: every other note */
    bad += check("pentatonic DIA3: in the scale (C E A: C6)", chord_is(60, (const uint8_t[]){60, 64, 69, 0}) && name_is(60, "C6"));
    {   /* every scale, root, mode: every note in the scale */
        uint32_t s, r, n, k, note;
        uint8_t out[CHORD_MAX];
        ok = 1;
        for (s = 1; s <= (uint32_t)TP[P_SCALE].max; s++)
            for (r = 0; r < 12u; r++)
                for (note = 36; note < 96u; note++) {
                    t->p[P_SCALE] = (int16_t)s; t->p[P_ROOT] = (int16_t)r;
                    t->p[P_CHRD] = (int16_t)(note & 1u ? CH_DIA3 : CH_DIA7);
                    n = chord_build(t, note, out);
                    ok &= n == (note & 1u ? 3u : 4u);
                    for (k = 0; k < n; k++)
                        ok &= (scale_mask(t) >> ((out[k] + 120u - r) % 12u)) & 1u;
                }
        bad += check("DIA3 / DIA7 of every scale and root: 3 / 4 notes, every one in key", ok);
    }
    return bad;
}

static int shapes_voicings(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    reset();
    t->p[P_CHRD] = CH_MAJ; bad += check("MAJ on C4: C E G", chord_is(60, (const uint8_t[]){60, 64, 67, 0}) && name_is(60, "C"));
    t->p[P_CHRD] = CH_MIN; bad += check("MIN: C Eb G (Cm)", chord_is(60, (const uint8_t[]){60, 63, 67, 0}) && name_is(60, "Cm"));
    t->p[P_CHRD] = CH_DOM7; bad += check("DOM7: C E G Bb (C7)", chord_is(60, (const uint8_t[]){60, 64, 67, 70}) && name_is(60, "C7"));
    t->p[P_CHRD] = CH_MAJ7; bad += check("MAJ7: C E G B (Cmaj7)", chord_is(60, (const uint8_t[]){60, 64, 67, 71}) && name_is(60, "Cmaj7"));
    t->p[P_CHRD] = CH_MIN7; bad += check("MIN7: C Eb G Bb (Cm7)", chord_is(60, (const uint8_t[]){60, 63, 67, 70}) && name_is(60, "Cm7"));
    t->p[P_CHRD] = CH_SUS4; bad += check("SUS4: C F G (Csus4)", chord_is(60, (const uint8_t[]){60, 65, 67, 0}) && name_is(60, "Csus4"));
    t->p[P_CHRD] = CH_POW; bad += check("POW: C G C (C5)", chord_is(60, (const uint8_t[]){60, 67, 72, 0}) && name_is(60, "C5"));
    t->p[P_CHRD] = CH_MIN; t->p[P_SCALE] = 1;
    bad += check("a fixed shape ignores the scale (MIN on D# in C major: D# F# A#)", chord_is(63, (const uint8_t[]){63, 66, 70, 0}));
    t->p[P_CHRD] = CH_MAJ;
    t->p[P_VOIC] = VC_OPEN; bad += check("OPEN: C G E (1-5-3)", chord_is(60, (const uint8_t[]){60, 67, 76, 0}));
    t->p[P_VOIC] = VC_INV1; bad += check("INV1: E G C", chord_is(60, (const uint8_t[]){64, 67, 72, 0}));
    t->p[P_VOIC] = VC_INV2; bad += check("INV2: G C E", chord_is(60, (const uint8_t[]){67, 72, 76, 0}));
    t->p[P_VOIC] = VC_BASS; bad += check("+OCT: C3 C E G", chord_is(60, (const uint8_t[]){48, 60, 64, 67}));
    t->p[P_CHRD] = CH_MAJ7;
    bad += check("+OCT of a seventh: C3 C E B (the fifth dropped, 4 notes)", chord_is(60, (const uint8_t[]){48, 60, 64, 71}));
    t->p[P_VOIC] = VC_OPEN; bad += check("OPEN seventh: C G B E", chord_is(60, (const uint8_t[]){60, 67, 71, 76}));
    t->p[P_VOIC] = VC_INV1; bad += check("INV1 seventh: E G B C", chord_is(60, (const uint8_t[]){64, 67, 71, 72}));
    t->p[P_VOIC] = VC_INV2; bad += check("INV2 seventh: G B C E", chord_is(60, (const uint8_t[]){67, 71, 72, 76}));
    t->p[P_CHRD] = CH_POW; t->p[P_VOIC] = VC_INV1;
    bad += check("INV1 of POW: G C (the doubled root once)", chord_is(60, (const uint8_t[]){67, 72, 0}));
    {   /* every mode, voicing and note: 1..4 notes, ascending, each once, inside 0..127; the name fits */
        uint32_t c, v, s, note, n, k;
        uint8_t out[CHORD_MAX];
        int32_t r;
        uint16_t mask;
        char b[12];
        ok = 1;
        for (s = 0; s <= (uint32_t)TP[P_SCALE].max; s++)
            for (c = 1; c <= CH_POW; c++)                    /* (STR3 / STR7 strum: the keys, below) */
                for (v = 0; v <= (uint32_t)TP[P_VOIC].max; v++)
                    for (note = 0; note < 128u; note++) {
                        t->p[P_SCALE] = (int16_t)s; t->p[P_CHRD] = (int16_t)c; t->p[P_VOIC] = (int16_t)v;
                        n = chord_make(t, note, out, &r, &mask);
                        ok &= n >= 1u && n <= CHORD_MAX && (mask & 1u);
                        for (k = 1; k < n; k++) ok &= out[k] > out[k - 1u];
                        for (k = 0; k < n; k++) ok &= out[k] <= 127u;
                        chord_name(b, (uint32_t)r, mask);
                        ok &= str_len(b) >= 1u && str_len(b) <= 7u;
                    }
        bad += check("every scale x CHRD x VOIC x note: 1..4 notes, ascending, once each, 0..127, a name", ok);
    }
    t->p[P_CHRD] = CH_OFF;
    bad += check("OFF: the note alone", chord_is(61, (const uint8_t[]){61, 0}));
    return bad;
}

static int keys(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    uint32_t before;
    reset();
    t->p[P_CHRD] = CH_DIA3;
    key_down(K_D4);
    bad += check("a key plays its chord (D4: D F A), MIDI OUT the three", gate_note(t, 62) && gate_note(t, 65) && gate_note(t, 69) &&
                 ngated(t) == 3u && mo_w == 3u && last_note == 62);
    bad += check("  chord_last: Dm for the CHORD page", chord_last[0].root == 62 && chord_last[0].n == 3u);
    t->p[P_CHRD] = CH_MIN7; t->p[P_VOIC] = VC_BASS;               /* changed while held */
    key_up(K_D4);
    bad += check("its release ends exactly those (CHRD / VOIC changed meanwhile), MIDI OUT their note-offs",
                 !ngated(t) && mo_w == 6u && ((midi_out_q[5] >> 8) & 0xF0u) == 0x80u);
    t->p[P_CHRD] = CH_DIA3; t->p[P_VOIC] = VC_CLOSE;
    mo_w = mo_r = 0;
    key_down(K_C4); key_down(K_E4);                                /* C E G and E G B: E and G shared */
    before = mo_w;
    bad += check("two chords: the shared notes sound once (C E G B)", ngated(t) == 4u && gate_note(t, 71) && before == 4u);
    key_up(K_C4);
    bad += check("  C up: C ends, E G stay for the E key", !gate_note(t, 60) && gate_note(t, 64) && gate_note(t, 67) && gate_note(t, 71));
    key_up(K_E4);
    bad += check("  E up: nothing left, MIDI OUT balanced", !ngated(t) && mo_w == 8u);
    t->p[P_VOICE] = V_MONO;
    key_down(K_C4 + 1u);                                           /* C#: DIA snaps to C */
    bad += check("MONO: the root alone (C# in C major: C)", ngated(t) == 1u && gate_note(t, 60));
    t->p[P_VOICE] = V_POLY;
    key_up(K_C4 + 1u);
    bad += check("  released (MONO switched off meanwhile)", !ngated(t));
    t->p[P_VOICE] = V_LEGATO;
    key_down(K_E4);
    bad += check("LEGATO: the root alone (E)", ngated(t) == 1u && gate_note(t, 64));
    key_up(K_E4);
    t->p[P_VOICE] = V_POLY;
    t->p[P_QUANT] = 2;                                             /* WHITE in A minor: D4 is the 4th degree */
    t->p[P_SCALE] = 2; t->p[P_ROOT] = 9;
    key_down(K_D4);                                                /* WHITE: C4 = A4 (the root), D4 = B4 */
    bad += check("QNT WHITE: the key mapped first (A minor, D4 -> B4), then its chord B D F",
                 kb_note[K_D4] == 71 && gate_note(t, 71) && gate_note(t, 74) && gate_note(t, 77));
    key_up(K_D4);
    bad += check("  released", !ngated(t));
    t->p[P_CHRD] = CH_OFF; t->p[P_QUANT] = 0;
    before = mo_w;
    key_down(K_C4);
    bad += check("OFF: one note, as before", ngated(t) == 1u && gate_note(t, 60) && mo_w == before + 1u);
    key_up(K_C4);
    return bad;
}

static int arp_rec(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    uint32_t i;
    reset();
    t->p[P_CHRD] = CH_DIA7;
    t->p[P_AMODE] = 1;
    key_down(K_C4);
    bad += check("ARP: one key, the chord as held notes (C E G B)", t->nheld == 4u && t->held[0] == 60 && t->held[1] == 64 &&
                 t->held[2] == 67 && t->held[3] == 71);
    key_up(K_C4);
    bad += check("  key up: nothing held", !t->nheld && !t->arp_phys);
    t->p[P_AMODE] = 0;
    for (i = 0; i < NSTEP; i++) t->step[i] = (step_t){{0}, 0, ST_REST, 0, 0};
    t->p[P_CHRD] = CH_DIA3;
    song.rec = 1; song.playing = 1;
    t->seq_idx = 0; t->seq_pos = 0;
    key_down(K_D4);
    bad += check("live recording: the chord into one step (D F A)", t->step[0].n == 3u && t->step[0].time == ST_NOTE &&
                 t->step[0].note[0] == 62 && t->step[0].note[1] == 65 && t->step[0].note[2] == 69);
    key_up(K_D4);
    t->p[P_CHRD] = CH_MAJ7; t->p[P_VOIC] = VC_BASS;
    t->seq_idx = 4;
    key_down(K_C4);
    bad += check("  a 4-note voicing: 4 notes in the step (the most a step holds)", t->step[4].n == 4u && t->step[4].note[0] == 48);
    key_up(K_C4);
    song.rec = 0; song.playing = 0;
    t->p[P_VOICE] = V_MONO; t->p[P_CHRD] = CH_DIA3; t->p[P_VOIC] = VC_CLOSE;
    song.rec = 1; song.playing = 1; t->seq_idx = 8;
    key_down(K_E4);
    bad += check("  MONO records its root alone", t->step[8].n == 1u && t->step[8].note[0] == 64);
    key_up(K_E4);
    song.rec = 0; song.playing = 0;
    return bad;
}

static int midi_in(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    uint32_t i, free = 1;
    reset();
    t->p[P_CHRD] = CH_DIA3;
    midi(0x90, 62, 100);
    bad += check("MIDI IN: a note plays its chord (D F A)", gate_note(t, 62) && gate_note(t, 65) && gate_note(t, 69) && ngated(t) == 3u);
    t->p[P_CHRD] = CH_OFF;
    midi(0x80, 62, 0);
    for (i = 0; i < MCHORD_N; i++) free &= !mchord[i].id;
    bad += check("  its note-off ends the chord (CHRD OFF meanwhile), the table empty", !ngated(t) && free);
    t->p[P_CHRD] = CH_DIA3;
    midi(0x90, 60, 100); key_down(K_E4);                           /* MIDI C E G, key E G B */
    bad += check("MIDI and a key share notes: C E G B once each", ngated(t) == 4u);
    midi(0x80, 60, 0);
    bad += check("  MIDI C off: C ends, E G stay for the key", !gate_note(t, 60) && gate_note(t, 64) && gate_note(t, 67));
    key_up(K_E4);
    bad += check("  key up: nothing left", !ngated(t));
    midi(0xB0, 64, 127); midi(0x90, 65, 100); midi(0x80, 65, 0);
    bad += check("pedal: the chord (F A C) held after its note-off", gate_note(t, 65) && gate_note(t, 69) && gate_note(t, 72));
    midi(0xB0, 64, 0);
    bad += check("  pedal up: released", !ngated(t));
    for (i = 0; i < MCHORD_N + 2u; i++) midi(0x90, 36 + i, 100);   /* more chords than the table holds */
    for (i = 0, free = 1; i < 2u; i++) free &= gate_note(t, 36 + MCHORD_N + i);   /* (the last two: their root alone) */
    for (i = 0; i < MCHORD_N + 2u; i++) midi(0x80, 36 + i, 0);
    bad += check("a full chord table: further notes play alone; all end", free && !ngated(t) && !t->nmono);
    midi(0x90, 62, 100); midi(0xB0, 120, 0);
    for (i = 0, free = 1; i < MCHORD_N; i++) free &= !mchord[i].id;
    bad += check("All Sound Off forgets the track's MIDI chords", free && !ngated(t));
    return bad;
}

static int kits(void)
{
    int bad = 0;
    track_t *d = &trk[3];
    uint8_t out[CHORD_MAX];
    reset();
    song.sel = 3;                                                  /* the DRUM track */
    d->p[P_CHRD] = CH_DIA7;
    key_down(K_C4);
    bad += check("DRUM: CHRD ignored (one hit per key)", chord_build(d, 36, out) == 1u && out[0] == 36 &&
                 kb_chn[K_C4] == 1u && mo_w == 1u);
    key_up(K_C4);
    midi(0x93, 38, 100);
    bad += check("  MIDI IN on the DRUM track: one hit, no chord kept", !mchord[0].id);
    midi(0x83, 38, 0);
    song.sel = 0;
    trk[0].eng_req = trk[0].engine = ENGI_SAMPLE;                   /* SAMPLE: no set is a kit (PERC, retired, */
    trk[0].p[P_CHRD] = CH_DIA3;                                     /* loads as DRUM; its SET 4 is a PIANO alias) */
    trk[0].p[P_E0] = SMP_SET_PERC;
    bad += check("SAMPLE SET 4 (once PERC): chords, no kit", !chord_kit(&trk[0]) && chord_build(&trk[0], 60, out) == 3u);
    trk[0].p[P_E0] = 0;
    bad += check("SAMPLE with a melodic set: chords", !chord_kit(&trk[0]) && chord_build(&trk[0], 60, out) == 3u);
    return bad;
}

/* ---- 1.6.1_sequences: STR3 / STR7, the black keys choose the chord, the white keys strum it ---- */
static uint32_t key_of(int black, uint32_t place)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (key_black(k) == black && key_place(k) == place)
            return k;
    return 99u;
}
static void kdown(uint32_t k) { fm1_in.notes |= 1u << k; events_block(CTL); }
static void kup(uint32_t k) { fm1_in.notes &= ~(1u << k); events_block(CTL); }
static int strum(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    uint32_t w;
    char b[12];
    reset();
    t->p[P_CHRD] = CH_STR3; t->p[P_VOIC] = VC_CLOSE; strum_slot[0] = 0;
    kdown(key_of(1, 0));                                    /* the first black key: the chord on the tonic */
    ok = ngated(t) == 0 && strum_slot[0] == 0;              /* it makes no sound */
    kup(key_of(1, 0));
    kdown(key_of(0, 0));
    ok &= ngated(t) == 1 && gate_note(t, 48);               /* string 1: C3 */
    kup(key_of(0, 0));
    kdown(key_of(0, 1)); ok &= gate_note(t, 52); kup(key_of(0, 1));
    kdown(key_of(0, 2)); ok &= gate_note(t, 55); kup(key_of(0, 2));
    kdown(key_of(0, 3)); ok &= gate_note(t, 60); kup(key_of(0, 3));      /* the chord again an octave up */
    kdown(key_of(0, 7)); ok &= gate_note(t, 76); kup(key_of(0, 7));                    /* string 8: E, two octaves up */
    ok &= ngated(t) == 0;
    bad += check("STRUM STR3: a black key makes no sound; the white keys are the strings, C E G then up an octave", ok);

    reset();
    t->p[P_CHRD] = CH_STR3; t->p[P_VOIC] = VC_CLOSE;
    kdown(key_of(1, 4)); kup(key_of(1, 4));             /* the fifth black key: degree 5, G */
    chord_name(b, chord_last[0].root, chord_last[0].mask);
    ok = str_eq(b, "G") && chord_last[0].n == 3 && chord_last[0].root == 55;
    kdown(key_of(0, 0)); ok &= gate_note(t, 55); kup(key_of(0, 0));
    kdown(key_of(0, 1)); ok &= gate_note(t, 59); kup(key_of(0, 1));
    kdown(key_of(0, 2)); ok &= gate_note(t, 62); kup(key_of(0, 2));
    kdown(key_of(1, 1)); kup(key_of(1, 1));             /* the second: Dm */
    chord_name(b, chord_last[0].root, chord_last[0].mask);
    ok &= str_eq(b, "Dm");
    kdown(key_of(1, 6)); kup(key_of(1, 6));             /* the seventh: Bdim */
    chord_name(b, chord_last[0].root, chord_last[0].mask);
    ok &= str_eq(b, "Bdim");
    kdown(key_of(1, 7)); kup(key_of(1, 7));             /* past the scale's seven: the tonic an octave up */
    chord_name(b, chord_last[0].root, chord_last[0].mask);
    ok &= str_eq(b, "C") && chord_last[0].root == 60;
    bad += check("STRUM: the black keys are the diatonic chords (G, Dm, Bdim, then the tonic an octave up), named on the CHORD page", ok);

    reset();
    t->p[P_CHRD] = CH_STR7; t->p[P_VOIC] = VC_CLOSE;
    kdown(key_of(1, 4)); kup(key_of(1, 4));
    ok = chord_last[0].n == 4;
    kdown(key_of(0, 3)); ok &= gate_note(t, 65); kup(key_of(0, 3));     /* G B D F: the fourth string, the seventh */
    chord_name(b, chord_last[0].root, chord_last[0].mask);
    ok &= str_eq(b, "G7");
    kdown(key_of(0, 4)); ok &= gate_note(t, 67); kup(key_of(0, 4));                    /* the fifth string: the root an octave up */
    bad += check("STRUM STR7: sevenths (G7: G B D F), the strings walk up them", ok);

    reset();
    t->p[P_CHRD] = CH_STR3; t->p[P_VOIC] = VC_OPEN;
    kdown(key_of(1, 0)); kup(key_of(1, 0));
    kdown(key_of(0, 1)); ok = gate_note(t, 55); kup(key_of(0, 1));   /* OPEN: 1-5-3: the second string is the fifth */
    t->p[P_VOIC] = VC_BASS;
    kdown(key_of(1, 0)); kup(key_of(1, 0));
    kdown(key_of(0, 0)); ok &= gate_note(t, 36); kup(key_of(0, 0));                          /* +OCT: the lowest string the root an octave down */
    bad += check("STRUM: VOIC voices the chord (OPEN, +OCT) as it does the chord keys", ok);

    reset();
    t->p[P_CHRD] = CH_STR3; t->p[P_TRANS] = 2; t->p[P_ROOT] = 2; t->p[P_SCALE] = 5;         /* D pentatonic, TRN +2 */
    kdown(key_of(1, 0)); kup(key_of(1, 0));
    kdown(key_of(0, 0)); ok = ngated(t) == 1; kup(key_of(0, 0));
    kdown(key_of(1, 5)); kup(key_of(1, 5));             /* five notes: the sixth black key is the tonic an octave up */
    ok &= chord_last[0].root == 48 + 2 + 12 + 2 && ngated(t) == 0;
    bad += check("STRUM: ROOT, SCALE and TRN apply (a pentatonic has five chords before the octave)", ok);

    reset();
    t->p[P_CHRD] = CH_STR3; t->p[P_VOICE] = V_MONO;
    kdown(key_of(1, 0)); kup(key_of(1, 0));
    kdown(key_of(0, 1)); ok = ngated(t) == 1 && gate_note(t, 52); kup(key_of(0, 1));
    ok &= ngated(t) == 0;
    t->p[P_CHRD] = CH_OFF; t->p[P_VOICE] = V_POLY;
    kdown(key_of(0, 0)); ok &= gate_note(t, 53); kup(key_of(0, 0));                   /* CHRD OFF: the key plays itself, F3 */
    kdown(key_of(1, 0)); ok &= gate_note(t, 54); kup(key_of(1, 0));                   /* (a black key sounds again) */
    bad += check("STRUM: a MONO part plays the string; CHRD OFF plays the keys as ever", ok);

    reset();
    t->p[P_CHRD] = CH_STR3;
    kdown(key_of(1, 0)); kup(key_of(1, 0));
    kdown(key_of(0, 0)); kdown(key_of(0, 1)); kdown(key_of(0, 2));                          /* a strum: three strings at once */
    ok = ngated(t) == 3 && gate_note(t, 48) && gate_note(t, 52) && gate_note(t, 55);
    kup(key_of(0, 0)); kup(key_of(0, 1)); kup(key_of(0, 2));
    ok &= ngated(t) == 0;
    for (w = 0; w < 16u; w++) { kdown(key_of(0, w)); kup(key_of(0, w)); }              /* all sixteen strings: none hangs */
    ok &= ngated(t) == 0;
    bad += check("STRUM: a strum sounds the strings held and every release ends its string", ok);
    return bad;
}

/* ---- 1.6.1_sequences: CHRD PLAY, a chord with each white key shaped by the black keys held ---- */
static int chord_is_at(const track_t *t, uint32_t root, const int *iv)     /* exactly these tones above root sound */
{
    uint32_t n = 0, i;
    for (i = 0; iv[i] >= 0; i++, n++)
        if (!gate_note(t, root + (uint32_t)iv[i]))
            return 0;
    return ngated(t) == n;
}
static void run_blocks(uint32_t n) { while (n--) events_block(CTL); }
static int play(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    uint32_t w = key_of(0, 4), root, b, i, heard, maxg;
    static const int MAJ[] = {0, 4, 7, -1}, MIN[] = {0, 3, 7, -1}, MIN7[] = {0, 3, 7, 10, -1}, DOM7[] = {0, 4, 7, 10, -1},
        MAJ7[] = {0, 4, 7, 11, -1}, SUS4[] = {0, 5, 7, -1}, DIM[] = {0, 3, 6, -1}, ADD9[] = {0, 4, 7, 14, -1},
        MIN_ADD9[] = {0, 3, 7, 14, -1};
    reset();
    cpk_defaults();                                         /* POP: MIN, MIN7, 7, MAJ7, SUS4, SUS2, DIM, +9, 6, STRUM, ARP */
    t->p[P_CHRD] = CH_PLAY; t->p[P_VOIC] = VC_CLOSE; t->p[P_SCALE] = 0;
    kdown(w); root = kb_note[w];
    ok = chord_is_at(t, root, MAJ);                         /* no black key: the major chord */
    kup(w);
    kdown(key_of(1, 6)); ok &= ngated(t) == 0;              /* a black key alone makes no sound */
    kdown(w); ok &= chord_is_at(t, root, DIM); kup(w);       /* key 7 held: DIM */
    kup(key_of(1, 6));
    kdown(key_of(1, 0)); kdown(w); ok &= chord_is_at(t, root, MIN); kup(w); kup(key_of(1, 0));
    kdown(key_of(1, 1)); kdown(w); ok &= chord_is_at(t, root, MIN7); kup(w); kup(key_of(1, 1));
    kdown(key_of(1, 2)); kdown(w); ok &= chord_is_at(t, root, DOM7); kup(w); kup(key_of(1, 2));
    kdown(key_of(1, 3)); kdown(w); ok &= chord_is_at(t, root, MAJ7); kup(w); kup(key_of(1, 3));
    kdown(key_of(1, 4)); kdown(w); ok &= chord_is_at(t, root, SUS4); kup(w); kup(key_of(1, 4));
    bad += check("PLAY: a white key plays a major chord; the black keys held make it MIN, MIN7, 7, MAJ7, SUS4 or DIM; they alone are silent", ok);

    reset(); cpk_defaults();
    t->p[P_CHRD] = CH_PLAY; t->p[P_VOIC] = VC_CLOSE; t->p[P_SCALE] = 0;
    kdown(key_of(1, 0)); kdown(key_of(1, 2)); kdown(w);     /* MIN then 7: a minor seventh (the lower key first) */
    ok = chord_is_at(t, root, MIN7); kup(w); kup(key_of(1, 0)); kup(key_of(1, 2));
    kdown(key_of(1, 0)); kdown(key_of(1, 7)); kdown(w);     /* MIN and +9 */
    ok &= chord_is_at(t, root, MIN_ADD9); kup(w); kup(key_of(1, 0)); kup(key_of(1, 7));
    kdown(key_of(1, 7)); kdown(w); ok &= chord_is_at(t, root, ADD9); kup(w); kup(key_of(1, 7));
    kdown(w);                                               /* the chord is the one held when the key goes down */
    kdown(key_of(1, 0)); ok &= chord_is_at(t, root, MAJ); kup(w); kup(key_of(1, 0));
    bad += check("PLAY: black keys stack (MIN with 7: a minor seventh, MIN with +9), and the chord is the one held at the key press", ok);

    reset(); cpk_defaults();
    t->p[P_CHRD] = CH_PLAY; t->p[P_VOIC] = VC_CLOSE; t->p[P_SCALE] = 0;
    cpk.act[0] = CA_A9; cpk.act[1] = CA_A11; cpk.act[2] = CA_A13; cpk.act[3] = CA_7;
    for (b = 0; b < 4u; b++) kdown(key_of(1, b));
    kdown(w);                                               /* 1 3 5 b7 9 11 13: seven tones, six sound: the fifth goes */
    ok = ngated(t) == 6u && gate_note(t, root) && gate_note(t, root + 4) && gate_note(t, root + 10) && !gate_note(t, root + 7) &&
         gate_note(t, root + 14) && gate_note(t, root + 17) && gate_note(t, root + 21);
    kup(w);
    for (b = 0; b < 4u; b++) kup(key_of(1, b));
    cpk_defaults(); cpk.act[0] = CA_BASS; cpk.act[1] = CA_INV;
    kdown(key_of(1, 0)); kdown(w); ok &= gate_note(t, root - 12) && gate_note(t, root + 4) && gate_note(t, root + 7); kup(w); kup(key_of(1, 0));
    kdown(key_of(1, 1)); kdown(w); ok &= gate_note(t, root + 12) && gate_note(t, root + 4) && !gate_note(t, root); kup(w); kup(key_of(1, 1));
    cpk_defaults(); cpk.act[0] = CA_SUS2; cpk.act[1] = CA_POW;                          /* the mapping is the user's */
    kdown(key_of(1, 0)); kdown(w); ok &= gate_note(t, root + 2) && gate_note(t, root + 7) && !gate_note(t, root + 4); kup(w); kup(key_of(1, 0));
    kdown(key_of(1, 1)); kdown(w); ok &= gate_note(t, root + 12) && ngated(t) == 3u; kup(w); kup(key_of(1, 1));
    bad += check("PLAY: at most six tones (the fifth goes first); BASS, INV; what each black key does is the pack's (cpk)", ok);

    reset(); cpk_defaults();
    t->p[P_CHRD] = CH_PLAY; t->p[P_VOIC] = VC_OPEN; t->p[P_SCALE] = 0;
    kdown(w); ok = gate_note(t, root) && gate_note(t, root + 7) && gate_note(t, root + 16); kup(w);   /* OPEN: 1 5 3' */
    t->p[P_VOIC] = VC_CLOSE;
    bad += check("PLAY: VOIC voices the chord as it does the other chord modes", ok);

    /* STRUM: the notes one after the other (cpk.strum x 5 ms), all held until the key goes */
    reset(); cpk_defaults();
    t->p[P_CHRD] = CH_PLAY; t->p[P_VOIC] = VC_CLOSE; t->p[P_SCALE] = 0;
    cpk.strum = 8;                                          /* 40 ms: 1764 samples, about 14 blocks */
    kdown(key_of(1, 9));                                    /* STRUM */
    kdown(w);
    ok = ngated(t) == 1u && gate_note(t, root);             /* the lowest at once */
    run_blocks(4);
    ok &= ngated(t) == 1u;                                  /* the next not yet (12 ms) */
    run_blocks(20);
    ok &= ngated(t) == 2u && gate_note(t, root + 4);        /* about 60 ms: the second */
    run_blocks(20);
    ok &= ngated(t) == 3u && gate_note(t, root + 7);
    run_blocks(30);
    ok &= ngated(t) == 3u;                                  /* all three, held */
    kup(w); kup(key_of(1, 9));
    ok &= ngated(t) == 0;
    kdown(key_of(1, 9)); kdown(w); run_blocks(2); kup(w);       /* let go before the strum is done: nothing is left to start */
    run_blocks(80);
    ok &= ngated(t) == 0;
    kup(key_of(1, 9));
    cpk.strum = 0;
    kdown(key_of(1, 9)); kdown(w); run_blocks(1);
    ok &= ngated(t) == 3u;                                  /* strum 0: the chord at once (within a block) */
    kup(w); kup(key_of(1, 9));
    bad += check("PLAY: STRUM plays the chord's notes one after the other at the pack's speed, a release ends what has started", ok);

    /* ARP: one note at a time, round the chord, at the track's ARP RATE and GATE */
    reset(); cpk_defaults();
    t->p[P_CHRD] = CH_PLAY; t->p[P_VOIC] = VC_CLOSE; t->p[P_SCALE] = 0;
    t->p[P_ARATE] = 2; t->p[P_AGATE] = 64;
    song.g[G_BPM] = 120; song.g[G_SWING] = 0;
    kdown(key_of(1, 1)); kdown(key_of(1, 10));              /* MIN7 and ARP: four notes, one at a time */
    kdown(w);
    heard = 0; maxg = 0;
    {
        uint32_t per = div_samples(2) / HALF_FRAMES + 1u, seen[4] = {0, 0, 0, 0};
        for (i = 0; i < per * 12u; i++) {
            uint32_t j;
            events_block(CTL);
            if (ngated(t) > maxg) maxg = ngated(t);
            for (j = 0; j < 4u; j++)
                if (gate_note(t, root + (uint32_t[]){0, 3, 7, 10}[j]))
                    seen[j]++;
        }
        heard = seen[0] && seen[1] && seen[2] && seen[3];
    }
    ok = heard && maxg == 1u;                               /* all four heard, never two at once */
    kup(w);
    ok &= ngated(t) == 0;
    run_blocks(120);
    ok &= ngated(t) == 0;                                   /* released: it stops */
    kup(key_of(1, 1)); kup(key_of(1, 10));
    bad += check("PLAY: ARP plays the chord one note at a time at the track's ARP RATE, never two at once, and stops with the key", ok);

    /* LATCH: a black key pressed toggles its action on (and off), instead of being held */
    reset(); cpk_defaults();
    t->p[P_CHRD] = CH_PLAY; t->p[P_VOIC] = VC_CLOSE; t->p[P_SCALE] = 0;
    cpk.strum |= CPK_LATCH;
    kdown(key_of(1, 0)); kup(key_of(1, 0));                  /* MIN: on, and the key is let go */
    ok = ngated(t) == 0 && play_latch_keys(t) == 1u << key_of(1, 0);
    kdown(w); ok &= chord_is_at(t, root, MIN); kup(w);       /* a white key: the minor chord, no key held */
    kdown(key_of(1, 2)); kup(key_of(1, 2));                  /* 7 on as well: MIN and 7, a minor seventh */
    kdown(w); ok &= chord_is_at(t, root, MIN7); kup(w);
    kdown(key_of(1, 0)); kup(key_of(1, 0));                  /* MIN off again: 7 alone */
    kdown(w); ok &= chord_is_at(t, root, DOM7); kup(w);
    ok &= play_latch_keys(t) == 1u << key_of(1, 2);
    kdown(key_of(1, 2)); kup(key_of(1, 2));                  /* 7 off: the major chord */
    kdown(w); ok &= chord_is_at(t, root, MAJ); kup(w);
    ok &= play_latch_keys(t) == 0;
    kdown(key_of(1, 1)); kdown(w);                           /* a key HELD does not count twice, the chord is the latched ones */
    ok &= chord_is_at(t, root, MIN7); kup(w); kup(key_of(1, 1));
    kdown(w); ok &= chord_is_at(t, root, MIN7); kup(w);      /* (it stayed on) */
    cpk.strum &= ~CPK_LATCH;                                 /* LATCH off: the keys are held again, nothing stays */
    kdown(w); ok &= chord_is_at(t, root, MAJ); kup(w);
    kdown(key_of(1, 1)); kdown(w); ok &= chord_is_at(t, root, MIN7); kup(w); kup(key_of(1, 1));
    bad += check("PLAY LATCH: a black key pressed toggles its action on and off, no holding; off, they are held as before", ok);

    reset(); cpk_defaults();                                  /* STRM and ARP latch too, and each track has its own */
    t->p[P_CHRD] = CH_PLAY; t->p[P_VOIC] = VC_CLOSE; t->p[P_SCALE] = 0;
    cpk.strum |= CPK_LATCH; cpk.strum = (uint8_t)((cpk.strum & CPK_LATCH) | 0u);
    kdown(key_of(1, 9)); kup(key_of(1, 9));                  /* STRM on, speed 0: the chord at once */
    kdown(w); run_blocks(2); ok = ngated(t) == 3u; kup(w);
    kdown(key_of(1, 9)); kup(key_of(1, 9));
    kdown(w); ok &= ngated(t) == 3u; kup(w);
    ok &= play_latch_keys(&trk[1]) == 0;
    bad += check("PLAY LATCH: STRM latches too, and the latched keys are the track's own", ok);

    /* other modes and keys as before */
    reset(); cpk_defaults();
    t->p[P_CHRD] = CH_PLAY; t->p[P_VOICE] = V_MONO;
    kdown(key_of(1, 1)); kdown(w); ok = ngated(t) == 1u; kup(w); kup(key_of(1, 1));
    t->p[P_VOICE] = V_POLY; t->p[P_CHRD] = CH_OFF;
    kdown(key_of(1, 1)); ok &= ngated(t) == 1u; kup(key_of(1, 1));              /* a black key sounds again in the other modes */
    bad += check("PLAY: a MONO part plays the last of the chord; the other CHRD modes play their black keys as ever", ok);

    /* the packs */
    ok = CPK_NF >= 4u && cpk_valid(&CPK_F[0].p) && cpk_valid(&CPK_F[1].p) && cpk_valid(&CPK_F[CPK_NF - 1u].p) &&
         str_eq(CPK_F[0].name, "POP");
    for (i = 0; i < CPK_NF; i++)
        ok &= cpk_valid(&CPK_F[i].p) && strlen(CPK_F[i].name) <= 5u;
    for (i = 0; i < CA_N; i++)
        ok &= strlen(CA_NAME[i]) <= 5u;
    {
        char nb[8];
        ok &= str_eq(cpk_name(CPK_NF, nb), "USER1") && str_eq(cpk_name(CPK_NF + 3, nb), "USER4");
    }
    cpk_defaults();
    ok &= !memcmp(&cpk, &CPK_F[0].p, sizeof cpk) && !memcmp(&cpk_user[2], &CPK_F[0].p, sizeof cpk);
    bad += check("PLAY: the built in packs are valid, named, and the user's start as the first", ok);
    return bad;
}

int main(void)
{
    int bad = theory() + shapes_voicings() + keys() + arp_rec() + midi_in() + kits() + strum() + play();
    printf("%s\n", bad ? "CHORD TEST FAILED" : "chord keys test passed");
    return bad != 0;
}
