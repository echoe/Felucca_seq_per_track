/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Chord keys (SCL > CHORD: P_CHRD, P_VOIC): one key, MIDI note or arp input plays a chord. Included by seq.c
 * after kb_map; runs in the audio ISR with the rest of the note input.
 *   CHRD OFF; DIA3 / DIA7: the triad / seventh of the track's scale (ROOT, SCALE) built on the key, every tone
 *        in key (a key outside the scale takes the scale note below it, as QNT SNAP does; CHR builds on the
 *        major scale of ROOT); MAJ MIN DOM7 MAJ7 MIN7 SUS4 POW: that shape on the key, whatever the scale.
 *   VOIC CLOSE (root position); OPEN (the second tone an octave up: 1-5-3); INV1 / INV2 (the lowest tone, then
 *        the next, an octave up); +OCT (the root an octave down added; a seventh drops its fifth). At most
 *        CHORD_MAX notes, notes outside 0..127 left out.
 * The key is mapped first (kb_map: QNT SNAP / WHITE, TRN, the octave), the chord is built on the note it gives.
 * MONO / LEGATO / UNISON parts play the chord's root only; a kit (the DRUM engine, slices: an
 * engine that maps the keys itself) ignores CHRD. Every source keeps the notes it started (kb_chord for a
 * key, mchord for a MIDI note) and its release ends exactly those, so CHRD / VOIC changed while it is held
 * leave nothing hanging. A note several sources hold sounds once and ends with the last of them.
 * 1.6.1_sequences: STR3 / STR7, a chord to strum: the black keys choose the chord, the white keys are its strings (strum_*).
 * MIDI notes, the arp and a kit play as with CHRD OFF. */
enum { CH_OFF, CH_DIA3, CH_DIA7, CH_MAJ, CH_MIN, CH_DOM7, CH_MAJ7, CH_MIN7, CH_SUS4, CH_POW, CH_STR3, CH_STR7, CH_PLAY };
enum { VC_CLOSE, VC_OPEN, VC_INV1, VC_INV2, VC_BASS };
#define CHORD_MAX 6u                     /* (1.6.1_sequences: PLAY's chords up to six tones; the shapes below four) */
#define CHORD_SHAPE_N 4u
#define MCHORD_N 24u                     /* MIDI notes held as chords at once (more: their root alone) */

static const int8_t CHORD_SHAPE[CH_POW - CH_MAJ + 1][CHORD_SHAPE_N] = {
    {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 10}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 5, 7, -1}, {0, 7, 12, -1},
};

static uint8_t kb_chord[27][CHORD_MAX], kb_chn[27];   /* the notes key k started (kb_chn 0: none) */
typedef struct { uint8_t id, ch, src, n, note[CHORD_MAX]; } mchord_t;   /* id: track + 1, 0 = free */
static mchord_t mchord[MCHORD_N];
/* the last chord played per track (the CHORD page shows it): its root, notes, and the shape's tones above the
 * root (bit i = i semitones, 0..11) */
static struct { uint8_t root, n, note[CHORD_MAX]; uint16_t mask; uint8_t gen; } chord_last[NTRK];

/* 1 = the track's keys are a kit: no chords */
static int chord_kit(const track_t *t)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    return e->oneshot || (e->keys && e->keys(t, 0) >= 0);
}

/* the tones of the chord (semitones above *root, ascending, iv[0] = 0) -> their number; DIA may move *root
 * down onto the scale */
static uint32_t chord_tones(const track_t *t, uint32_t mode, int32_t *root, int32_t *iv)
{
    uint32_t n = 0, i;
    if (mode >= CH_MAJ) {
        for (i = 0; i < CHORD_SHAPE_N && CHORD_SHAPE[mode - CH_MAJ][i] >= 0; i++)
            iv[n++] = CHORD_SHAPE[mode - CH_MAJ][i];
        return n;
    }
    {
        uint32_t mask = scale_mask(t), deg[12], c = 0, at = 0, guard = 12;
        int32_t r = t->p[P_ROOT];
        if (mask == 0xFFFu)
            mask = SCALE_MASK[1];                       /* CHR: no key of its own, the major scale of ROOT */
        while (guard-- && !((mask >> (uint32_t)((*root - r + 120) % 12)) & 1u))
            (*root)--;                                  /* outside the scale: the scale note below */
        for (i = 0; i < 12u; i++)
            if ((mask >> i) & 1u) {
                if (i == (uint32_t)((*root - r + 120) % 12))
                    at = c;
                deg[c++] = i;
            }
        for (i = 0; i < (mode == CH_DIA7 ? 4u : 3u); i++) {   /* every other note of the scale */
            uint32_t k = at + 2u * i;
            iv[n++] = (int32_t)deg[k % c] + 12 * (int32_t)(k / c) - (int32_t)deg[at];
        }
        return n;
    }
}

/* the chord on note `root` as track t plays it now -> out (ascending), the number of notes (1..CHORD_MAX);
 * *rp its root, *maskp its tones above the root (bit i: i semitones; 0 = no chord). The root alone when CHRD
 * is OFF, on a kit, or for MONO / LEGATO / UNISON (their chord's root) */
/* the voicing (VOIC) on the tones iv (semitones above r, n of them), ascending, each once inside 0..127: out, how many */
static uint32_t chord_finish(const track_t *t, int32_t *iv, uint32_t n, int32_t r, uint8_t *out)
{
    int32_t x;
    uint32_t i, j, m = 0;
    switch (t->p[P_VOIC]) {
    case VC_OPEN:                                       /* 1-5-3(-7): the second tone an octave up */
        if (n >= 3u)
            iv[1] += 12;
        break;
    case VC_INV2:
    case VC_INV1:
        for (j = 0; j < (t->p[P_VOIC] == VC_INV2 ? 2u : 1u); j++) {   /* the lowest tone an octave up */
            x = iv[0] + 12;
            for (i = 1; i < n; i++)
                iv[i - 1u] = iv[i];
            iv[n - 1u] = x;
        }
        break;
    case VC_BASS:                                       /* the root an octave down; a seventh drops its fifth */
        if (n == 4u) {
            iv[2] = iv[3];
            n--;
        }
        for (i = n; i > 0; i--)
            iv[i] = iv[i - 1u];
        iv[0] = -12;
        n++;
        break;
    default:
        break;
    }
    for (i = 1; i < n; i++)                             /* ascending */
        for (j = i; j > 0 && iv[j - 1u] > iv[j]; j--) {
            x = iv[j];
            iv[j] = iv[j - 1u];
            iv[j - 1u] = x;
        }
    for (i = 0; i < n && m < CHORD_MAX; i++) {          /* inside 0..127, each note once */
        x = r + iv[i];
        if (x < 0 || x > 127 || (m && out[m - 1u] == (uint8_t)x))
            continue;
        out[m++] = (uint8_t)x;
    }
    if (!m) {
        out[0] = (uint8_t)clamp(r, 0, 127);
        return 1;
    }
    return m;
}

static uint32_t chord_make_x(const track_t *t, uint32_t mode, int poly, uint32_t root, uint8_t *out, int32_t *rp, uint16_t *maskp)
{
    int32_t iv[CHORD_MAX + 1u], r = (int32_t)root;
    uint32_t n, i;
    uint16_t mask = 0;
    *rp = r;
    *maskp = 0;
    if (!mode || mode > CH_POW || chord_kit(t)) {
        out[0] = (uint8_t)root;
        return 1;
    }
    n = chord_tones(t, mode, &r, iv);
    for (i = 0; i < n; i++)
        mask |= (uint16_t)(1u << (iv[i] % 12));
    *rp = r;
    *maskp = mask;
    if (!poly) {                                        /* one voice: the root */
        out[0] = (uint8_t)clamp(r, 0, 127);
        return 1;
    }
    return chord_finish(t, iv, n, r, out);
}

static uint32_t chord_make(const track_t *t, uint32_t root, uint8_t *out, int32_t *rp, uint16_t *maskp)
{
    return chord_make_x(t, (uint32_t)t->p[P_CHRD], trk_vmode(t) == V_POLY, root, out, rp, maskp);
}

/* chord_make for a note played now: a chord is kept in chord_last (the CHORD page shows it) */
static uint32_t chord_build(track_t *t, uint32_t root, uint8_t *out)
{
    int32_t r;
    uint16_t mask;
    uint32_t m = chord_make(t, root, out, &r, &mask), i, k = trk_index(t);
    if (m > 1u) {
        chord_last[k].root = (uint8_t)clamp(r, 0, 127);
        chord_last[k].mask = mask;
        chord_last[k].n = (uint8_t)m;
        for (i = 0; i < CHORD_MAX; i++)
            chord_last[k].note[i] = i < m ? out[i] : 0u;
        chord_last[k].gen++;
    }
    return m;
}

/* ------------------------------------------------------------------------- STRUM --- */
/* CHRD STR3 / STR7: the black keys are the track's chords, the white keys the strings of the chord chosen, as on a guitar.
 *   black key b (0..10, F#3 .. F#5): the diatonic chord (a triad, STR7 a seventh) on degree b of the track's scale (ROOT, SCALE;
 *        CHR: the major scale of ROOT), the degrees past the scale's last one an octave up; VOIC voices it; TRN and the
 *        octave buttons move it. It makes no sound: it is what the strings play from now on (the chord's name on the CHORD page)
 *   white key w (0..15, F3 .. G5): string w: the chord's notes in order, then the same an octave up, and so on, so the keys
 *        from left to right walk up the chord and a strum is a slide over the keys. The key's release ends its note as any key
 *        (MONO / LEGATO / UNISON parts play it too: it is one string at a time).
 * Not on a kit (chord_kit) or over a layer, the DRUM grid or the NAME keys: those keys are theirs. */
static uint8_t strum_slot[NTRK];                        /* the black key (0..10) whose chord the strings play */

static int strum_on(const track_t *t) { return (t->p[P_CHRD] == CH_STR3 || t->p[P_CHRD] == CH_STR7) && !chord_kit(t); }

/* the chord of black key `slot` as the strings play it: out ascending, how many (1..CHORD_MAX); *rp its root, *maskp its tones */
static uint32_t strum_chord(const track_t *t, uint32_t slot, uint8_t *out, int32_t *rp, uint16_t *maskp)
{
    uint32_t mask = scale_mask(t), deg[12], c = 0, i, n;
    int32_t root;
    if (mask == 0xFFFu)
        mask = SCALE_MASK[1];                           /* CHR: the major scale of ROOT */
    for (i = 0; i < 12u; i++)
        if ((mask >> i) & 1u)
            deg[c++] = i;
    root = 48 + (int32_t)t->p[P_ROOT] + 12 * (int32_t)song.octave + 12 * (int32_t)(slot / c) + (int32_t)deg[slot % c];
    n = chord_make_x(t, t->p[P_CHRD] == CH_STR7 ? CH_DIA7 : CH_DIA3, 1, (uint32_t)clamp(root, 0, 127), out, rp, maskp);
    for (i = 0; i < n; i++)                             /* (the scale is built before TRN, as kb_pitch) */
        out[i] = (uint8_t)clamp((int32_t)out[i] + t->p[P_TRANS], 0, 127);
    *rp = clamp(*rp + t->p[P_TRANS], 0, 127);
    return n;
}
/* black key `slot` chosen: the chord is kept for the CHORD page (chord_last) as a chord played is */
static void strum_select(const track_t *t, uint32_t slot)
{
    uint8_t out[CHORD_MAX];
    int32_t r;
    uint16_t mask;
    uint32_t n, i, k = trk_index(t);
    strum_slot[k] = (uint8_t)slot;
    n = strum_chord(t, slot, out, &r, &mask);
    chord_last[k].root = (uint8_t)r;
    chord_last[k].mask = mask;
    chord_last[k].n = (uint8_t)n;
    for (i = 0; i < CHORD_MAX; i++)
        chord_last[k].note[i] = i < n ? out[i] : 0u;
    chord_last[k].gen++;
}
/* what key k (of the 27) does on a STR3 / STR7 track: a black key chooses (KB_SILENT: no sound), a white key is a string:
 * its note, KB_SILENT when it is above 127 */
static uint32_t strum_key(const track_t *t, uint32_t k)
{
    uint8_t out[CHORD_MAX];
    int32_t r;
    uint16_t mask;
    uint32_t n, w, note;
    if (key_black(k)) {
        strum_select(t, key_place(k));
        return KB_SILENT;
    }
    n = strum_chord(t, strum_slot[trk_index(t)], out, &r, &mask);
    w = key_place(k);
    note = out[w % n] + 12u * (w / n);
    return note > 127u ? KB_SILENT : note;
}
/* the key that is the chosen chord's (the LED): 27 when there is none */
static uint32_t strum_led_key(const track_t *t)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (key_black(k) && key_place(k) == strum_slot[trk_index(t)])
            return k;
    return 27u;
}

/* ------------------------------------------------------------------------- PLAY --- */
/* CHRD PLAY (1.6.1_sequences): a chord with every white key (its own note, on the track's scale as the keys are) and the black
 * keys held with it shape the chord: each black key has an action (MIN, 7, +9, STRUM ...: cpk, set on the SAVE > CHORDS page),
 * applied lowest key first, so a later one changes what an earlier one set (MIN then 7: a minor seventh; MIN7: one key for it;
 * 7 and +9 and +11 stack). The chord is the one held when the white key goes down. The black keys make no sound.
 *   quality  MIN DIM AUG SUS2 SUS4 POW      the triad (MAJ when none)
 *   seventh  6 7 MAJ7, and MIN7 DIM7 M7B5 MMAJ7 (a quality with it), 9 MAJ9 MIN9 (with the ninth)
 *   extra    +9 +11 +13 (added tones), INV (the lowest tone up an octave), BASS (the root an octave down)
 *   how      STRUM: the notes one after the other, (cpk.strum & CPK_SPEED) x 5 ms apart, all held until the key is let go
 *            ARP: one note at a time, round the chord, at the track's ARP RATE and GATE, until the key is let go
 * Up to CHORD_MAX (six) tones: past that the fifth goes first, then the highest. VOIC voices it as it does the other chord modes. */
enum { CA_NONE, CA_MIN, CA_DIM, CA_AUG, CA_SUS2, CA_SUS4, CA_POW, CA_6, CA_7, CA_MAJ7, CA_MIN7, CA_DIM7, CA_M7B5, CA_MMAJ7,
       CA_9, CA_MAJ9, CA_MIN9, CA_A9, CA_A11, CA_A13, CA_INV, CA_BASS, CA_STRUM, CA_ARP, CA_N };
static const char *const CA_NAME[CA_N] = {"--", "MIN", "DIM", "AUG", "SUS2", "SUS4", "POW", "6", "7", "MAJ7", "MIN7", "DIM7",
    "M7B5", "MMAJ7", "9", "MAJ9", "MIN9", "+9", "+11", "+13", "INV", "BASS", "STRM", "ARP"};
#define CPK_KEYS 11u                                    /* the black keys F#3 .. F#5 */
#define CPK_USER 4u                                     /* chord packs of the user's */
#define CPK_STRUM_MAX 40u                               /* cpk.strum: x 5 ms, 0..200 (the low seven bits) */
#define CPK_LATCH 0x80u                                 /* cpk.strum's top bit: the black keys toggle their action on and off instead of being held */
#define CPK_SPEED(p) ((uint32_t)(p)->strum & 0x7Fu)
typedef struct { uint8_t act[CPK_KEYS], strum; } cpack_t;
typedef struct { const char *name; cpack_t p; } cpack_f_t;
/* the chord packs: what each black key does (and how fast a strum goes) */
static const cpack_f_t CPK_F[] = {
    {"POP",   {{CA_MIN, CA_MIN7, CA_7, CA_MAJ7, CA_SUS4, CA_SUS2, CA_DIM, CA_A9, CA_6, CA_STRUM, CA_ARP}, 8}},
    {"JAZZ",  {{CA_MIN7, CA_MAJ7, CA_7, CA_M7B5, CA_DIM7, CA_9, CA_MIN9, CA_MAJ9, CA_A13, CA_STRUM, CA_INV}, 6}},
    {"SUS",   {{CA_SUS2, CA_SUS4, CA_A9, CA_A11, CA_POW, CA_6, CA_MAJ7, CA_MIN, CA_INV, CA_STRUM, CA_ARP}, 10}},
    {"EXT",   {{CA_A9, CA_A11, CA_A13, CA_9, CA_MAJ9, CA_MIN9, CA_7, CA_MAJ7, CA_MIN7, CA_BASS, CA_STRUM}, 8}},
    {"EASY",  {{CA_MIN, CA_7, CA_MAJ7, CA_SUS4, CA_DIM, CA_AUG, CA_POW, CA_NONE, CA_NONE, CA_STRUM, CA_ARP}, 12}},
    {"HARP",  {{CA_MIN, CA_MIN7, CA_MAJ7, CA_A9, CA_A11, CA_BASS, CA_INV, CA_NONE, CA_STRUM, CA_STRUM, CA_ARP}, 4}},
};
#define CPK_NF (sizeof CPK_F / sizeof CPK_F[0])
#define CPK_N (CPK_NF + CPK_USER)                       /* the packs the page lists: the built in ones, then the user's */
static cpack_t cpk;                                     /* what the black keys do now (saved with the music: patbank_store.c) */
static cpack_t cpk_user[CPK_USER];                      /* the user's packs (the same) */
static uint8_t cpk_sel;                                 /* the pack the page shows (0 .. CPK_N - 1) */
static void cpk_defaults(void)
{
    uint32_t i;
    cpk = CPK_F[0].p;
    for (i = 0; i < CPK_USER; i++)
        cpk_user[i] = CPK_F[0].p;
}
static const cpack_t *cpk_pack(uint32_t i) { return i < CPK_NF ? &CPK_F[i].p : &cpk_user[(i - CPK_NF) % CPK_USER]; }
static const char *cpk_name(uint32_t i, char *b)         /* b: 8 bytes for a user pack's name */
{
    if (i < CPK_NF)
        return CPK_F[i].name;
    str_cpy(b, "USER1", 8);
    b[4] = (char)('1' + (i - CPK_NF) % CPK_USER);
    return b;
}
static int cpk_valid(const cpack_t *p)
{
    uint32_t i;
    for (i = 0; i < CPK_KEYS; i++)
        if (p->act[i] >= CA_N)
            return 0;
    return CPK_SPEED(p) <= CPK_STRUM_MAX;
}

static uint8_t cpk_key;                                 /* the black key the page edits (0..10) */

typedef struct { uint8_t qual, sev, ext, inv, bass, strum, arp; } cshape_t;
enum { PQ_MAJ, PQ_MIN, PQ_DIM, PQ_AUG, PQ_SUS2, PQ_SUS4, PQ_POW };
static void play_apply(cshape_t *s, uint32_t a)
{
    switch (a) {
    case CA_MIN: s->qual = PQ_MIN; break;
    case CA_DIM: s->qual = PQ_DIM; break;
    case CA_AUG: s->qual = PQ_AUG; break;
    case CA_SUS2: s->qual = PQ_SUS2; break;
    case CA_SUS4: s->qual = PQ_SUS4; break;
    case CA_POW: s->qual = PQ_POW; break;
    case CA_6: s->sev = 9; break;
    case CA_7: s->sev = 10; break;
    case CA_MAJ7: s->sev = 11; break;
    case CA_MIN7: s->qual = PQ_MIN; s->sev = 10; break;
    case CA_DIM7: s->qual = PQ_DIM; s->sev = 9; break;
    case CA_M7B5: s->qual = PQ_DIM; s->sev = 10; break;
    case CA_MMAJ7: s->qual = PQ_MIN; s->sev = 11; break;
    case CA_9: s->sev = 10; s->ext |= 1u; break;
    case CA_MAJ9: s->sev = 11; s->ext |= 1u; break;
    case CA_MIN9: s->qual = PQ_MIN; s->sev = 10; s->ext |= 1u; break;
    case CA_A9: s->ext |= 1u; break;
    case CA_A11: s->ext |= 2u; break;
    case CA_A13: s->ext |= 4u; break;
    case CA_INV: s->inv = 1; break;
    case CA_BASS: s->bass = 1; break;
    case CA_STRUM: s->strum = 1; break;
    case CA_ARP: s->arp = 1; break;
    default: break;
    }
}
/* the black keys held (bit b = black key b, 0..10) -> the chord's shape */
static cshape_t play_shape(uint32_t held)
{
    cshape_t s = {0, 0, 0, 0, 0, 0, 0};
    uint32_t b;
    for (b = 0; b < CPK_KEYS; b++)
        if ((held >> b) & 1u)
            play_apply(&s, cpk.act[b]);
    return s;
}
/* the chord on root for the shape: out ascending (CHORD_MAX at most), how many; *rp the root, *maskp the tones above it */
static uint32_t play_chord(const track_t *t, const cshape_t *s, uint32_t root, uint8_t *out, int32_t *rp, uint16_t *maskp)
{
    static const int8_t THIRD[7] = {4, 3, 3, 4, 2, 5, -1}, FIFTH[7] = {7, 7, 6, 8, 7, 7, 7};
    int32_t iv[CHORD_MAX + 3u], r = (int32_t)root;
    uint32_t n = 0, i, fifth = 0, k;
    uint16_t mask = 0;
    iv[n++] = 0;
    if (THIRD[s->qual] >= 0)
        iv[n++] = THIRD[s->qual];
    fifth = n;
    iv[n++] = FIFTH[s->qual];
    if (s->qual == PQ_POW)
        iv[n++] = 12;
    if (s->sev)
        iv[n++] = s->sev;
    if (s->ext & 1u) iv[n++] = 14;
    if (s->ext & 2u) iv[n++] = 17;
    if (s->ext & 4u) iv[n++] = 21;
    if (s->bass)
        iv[n++] = -12;
    if (s->inv) {                                       /* the lowest tone of the chord, the root, an octave up */
        for (i = 0; i < n; i++)
            if (iv[i] == 0)
                iv[i] = 12;
    }
    while (n > CHORD_MAX) {                             /* too many: the fifth goes, then the highest tone */
        if (fifth && fifth < n && iv[fifth] == FIFTH[s->qual]) {
            for (k = fifth; k + 1u < n; k++)
                iv[k] = iv[k + 1u];
            n--;
            fifth = 0;
        } else {
            uint32_t hi = 0;
            for (i = 1; i < n; i++)
                if (iv[i] > iv[hi])
                    hi = i;
            for (k = hi; k + 1u < n; k++)
                iv[k] = iv[k + 1u];
            n--;
        }
    }
    for (i = 0; i < n; i++)
        mask |= (uint16_t)(1u << (((iv[i] % 12) + 12) % 12));
    *rp = r;
    *maskp = mask;
    return chord_finish(t, iv, n, r, out);
}
static int play_on(const track_t *t) { return t->p[P_CHRD] == CH_PLAY && !chord_kit(t); }
static int kb_is_black(uint32_t k) { return key_black(k); }
/* the black keys held now (fm1_in.notes) as bits 0..10 */
static uint32_t play_held(void)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++)
        if (((fm1_in.notes >> k) & 1u) && key_black(k))
            m |= 1u << key_place(k);
    return m;
}
/* LATCH (cpk.strum's top bit, the pack's): a black key pressed toggles its action on for the track, and it stays on until the key
 * is pressed again (or LATCH is turned off, the music is loaded); the chord takes the black keys that are on, not the ones held */
static uint16_t play_lat[NTRK];
static uint32_t play_mask(const track_t *t) { return (cpk.strum & CPK_LATCH) ? play_lat[trk_index(t)] : play_held(); }
static void play_latch_press(const track_t *t, uint32_t k)
{
    if (cpk.strum & CPK_LATCH)
        play_lat[trk_index(t)] ^= (uint16_t)(1u << key_place(k));
}
/* the keys whose action is on (the LEDs): bit k of the 27 */
static uint32_t play_latch_keys(const track_t *t)
{
    uint32_t k, m = 0;
    if (!(cpk.strum & CPK_LATCH))
        return 0;
    for (k = 0; k < 27u; k++)
        if (key_black(k) && ((play_lat[trk_index(t)] >> key_place(k)) & 1u))
            m |= 1u << k;
    return m;
}

static void input_on(track_t *t, uint32_t note, uint32_t vel);          /* (seq.c, below) */
static void input_off(track_t *t, uint32_t note);
static int midi_local_held(const track_t *t, uint32_t note);
/* the keys that play a chord one note at a time (STRUM, ARP): what each is doing */
static uint8_t pk_mode[27], pk_i[27], pk_n[27], pk_cur[27], pk_vel[27];     /* 0 none, 1 strum, 2 arp */
static int32_t pk_wait[27], pk_gate[27];                                      /* samples until the next note / the gate ends */

static void play_note(track_t *t, uint32_t k, uint32_t x)
{
    uint32_t mc = trk_midi_ch(trk_index(t));
    if (midi_local_held(t, x))
        return;
    in_key = (uint8_t)(k + 1u);
    input_on(t, x, pk_vel[k]);
    in_key = 0;
    midi_out_event(0x09u | (0x90u | mc) << 8 | x << 16 | (uint32_t)pk_vel[k] << 24);
}
static void play_note_off(track_t *t, uint32_t x)
{
    uint32_t mc = trk_midi_ch(trk_index(t));
    if (midi_local_held(t, x))
        return;
    input_off(t, x);
    midi_out_event(0x08u | (0x80u | mc) << 8 | x << 16);
}
/* the samples between a strum's notes */
static int32_t play_strum_samples(void) { return (int32_t)(CPK_SPEED(&cpk) * 5u * 44100u / 1000u); }
/* once an audio block (keyboard_block): the next notes of the strums and the arps held */
static void play_tick(void)
{
    uint32_t k;
    for (k = 0; k < 27u; k++) {
        track_t *t;
        if (!pk_mode[k])
            continue;
        t = &trk[kb_trk[k]];
        pk_wait[k] -= (int32_t)HALF_FRAMES;
        if (pk_mode[k] == 1u) {                         /* STRUM: the next note, held until the key goes */
            while (pk_mode[k] == 1u && pk_wait[k] <= 0) {
                play_note(t, k, kb_chord[k][pk_i[k]]);
                kb_chn[k] = (uint8_t)(pk_i[k] + 1u);
                if (++pk_i[k] >= pk_n[k])
                    pk_mode[k] = 0;
                else
                    pk_wait[k] += play_strum_samples() > 0 ? play_strum_samples() : 1;
            }
            continue;
        }
        if (pk_cur[k]) {                                /* ARP: the note sounding ends with its gate */
            pk_gate[k] -= (int32_t)HALF_FRAMES;
            if (pk_gate[k] <= 0) {
                play_note_off(t, kb_chord[k][pk_cur[k] - 1u]);
                pk_cur[k] = 0;
            }
        }
        if (pk_wait[k] <= 0) {                          /* ARP: the next note round the chord, one a step */
            uint32_t period = div_samples((uint32_t)t->p[P_ARATE]), i = pk_i[k] % pk_n[k];
            if (pk_cur[k])
                play_note_off(t, kb_chord[k][pk_cur[k] - 1u]);
            play_note(t, k, kb_chord[k][i]);
            pk_cur[k] = (uint8_t)(i + 1u);
            pk_i[k]++;
            pk_gate[k] = (int32_t)(period * (uint32_t)t->p[P_AGATE] / 128u);
            pk_wait[k] += (int32_t)period;
        }
    }
}
/* key k down on a PLAY track with the black keys held: the chord, how many of its notes sound at once (0 for an arp, 1 for a
 * strum: play_tick brings the rest); kb_chord[k] holds all of them, pk_* what is left to do */
static uint32_t play_build(track_t *t, uint32_t k, uint32_t root, uint32_t vel)
{
    cshape_t s = play_shape(play_mask(t));
    int32_t r;
    uint16_t mask;
    uint32_t n = play_chord(t, &s, root, kb_chord[k], &r, &mask), i = trk_index(t), j;
    chord_last[i].root = (uint8_t)clamp(r, 0, 127);
    chord_last[i].mask = mask;
    chord_last[i].n = (uint8_t)(n < CHORD_MAX ? n : CHORD_MAX);
    for (j = 0; j < CHORD_MAX; j++)
        chord_last[i].note[j] = j < n ? kb_chord[k][j] : 0u;
    chord_last[i].gen++;
    pk_vel[k] = (uint8_t)vel;
    return n | (uint32_t)(s.arp ? 2 : s.strum ? 1 : 0) << 8;
}

/* a chord's name from its root and tones (bit i: i semitones above the root): "C", "Cm7", "F#m7b5", "Gsus4",
 * "A5" -> b (12 bytes) */
static void chord_name(char *b, uint32_t root, uint32_t mask)
{
    uint32_t mi = (mask >> 3) & 1u, ma = (mask >> 4) & 1u, b5 = (mask >> 6) & 1u, p5 = (mask >> 7) & 1u;
    uint32_t s5 = (mask >> 8) & 1u, d7 = (mask >> 9) & 1u, m7 = (mask >> 10) & 1u, M7 = (mask >> 11) & 1u;
    const char *q, *x = "";
    str_cpy(b, N_NOTE[root % 12u], 12);
    if (mi && !ma && b5 && !p5)
        q = d7 ? "dim7" : m7 ? "m7b5" : "dim";
    else if (ma && !mi && s5 && !p5)
        q = m7 ? "+7" : M7 ? "+M7" : "aug";
    else if (ma && !mi)
        q = m7 ? "7" : M7 ? "maj7" : d7 ? "6" : "";
    else if (mi)
        q = m7 ? "m7" : M7 ? "mM7" : d7 ? "m6" : "m";
    else if ((mask >> 5) & 1u) {                        /* no third: suspended */
        q = m7 ? "7" : "";
        x = "sus4";
    } else if ((mask >> 2) & 1u) {
        q = m7 ? "7" : "";
        x = "sus2";
    } else
        q = "5";
    str_cpy(b + str_len(b), q, 12 - str_len(b));
    str_cpy(b + str_len(b), x, 12 - str_len(b));
}

/* the MIDI note (ch, src) of track id - 1 that plays a chord, 0 = none */
static mchord_t *mchord_of(uint32_t ch, uint32_t src, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < MCHORD_N; i++)
        if (mchord[i].id == id && mchord[i].ch == ch && mchord[i].src == src)
            return &mchord[i];
    return 0;
}

/* a MIDI-held chord of track id - 1 sounds note */
static int mchord_held(uint32_t id, uint32_t note)
{
    uint32_t i, j;
    for (i = 0; i < MCHORD_N; i++)
        if (mchord[i].id == id)
            for (j = 0; j < mchord[i].n; j++)
                if (mchord[i].note[j] == note)
                    return 1;
    return 0;
}

static void mchord_forget(uint32_t track)               /* panic: the track's MIDI chords are gone */
{
    uint32_t i;
    for (i = 0; i < MCHORD_N; i++)
        if (mchord[i].id == track + 1u)
            mchord[i].id = 0;
}
