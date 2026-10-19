# Felucca

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

## Maintenance Notice

What began as a personal experiment grew far beyond my expectations. I’m grateful to everyone who contributed, supported the project, and made music with Felucca.

I’ve decided to step back from active development to focus on creating new instruments and music.

Felucca will remain available with occasional maintenance, but new features, issue responses, and pull request reviews are not guaranteed.

Thank you for everything. I hope you’ll look forward to my next creations.

## Overview

Multi-engine synthesizer firmware for the M-VAVE FM-1, version 1.6.1. Connect the FM-1 to a computer by USB, open the
[web installer](https://hugelton.github.io/Felucca/) in Chrome or Edge and press Install; no extra hardware is needed.
Installing is at your own risk: M-VAVE's updater or the installer's **Return to official V15** takes you back.
[Try it in your browser](https://hugelton.github.io/Felucca/webapp/try/), no FM-1 needed.

- Install: [web installer](https://hugelton.github.io/Felucca/)
- Edit: [web editor](https://hugelton.github.io/Felucca/webapp/editor/) ([Felucca-WebApp](https://github.com/hugelton/Felucca-WebApp))
- Read: the manual and the release notes are in the [wiki](https://github.com/hugelton/Felucca/wiki/Home)
- Build: [BUILDING.md](BUILDING.md)
- Related: [Felucca-Engines](https://github.com/hugelton/Felucca-Engines) (the ANALOG, LOFI, TRIO, WHEEL and NOISE engines, MIT; still maintained) ·
  [felucca-bsp](https://github.com/hugelton/felucca-bsp) (the FM-1 hardware layer)

## AI disclaimer

Felucca is developed with the assistance of AI coding agents (code, tests, documentation, translation, maintenance). The
design, features, sound design and direction are the maintainer's or the community's, and no generative AI is used for music,
icons or artwork. See [On AI-Assisted Development and Responsibility](https://github.com/hugelton/Felucca/discussions/166).

## Credits and licence

Free software: [GPL-3.0-only](LICENSE); the Hügelton Sample Pack is CC0 1.0; the engines in `firmware/engines` are MIT
([Felucca-Engines](https://github.com/hugelton/Felucca-Engines)); the bundled fonts and ported DSP keep their own licences
([LICENSES/](LICENSES/), [LICENSING.md](LICENSING.md)). Thanks to everyone whose work and contributions are in it: [CREDITS.md](CREDITS.md).

M-VAVE and FM-1 are trademarks of their respective owners. Felucca is not affiliated with or endorsed by them.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments

## Sequences (eight per track) — this branch, v1.6.1_sequences

Each track plays one of eight sequences. A sequence has its own steps, its own LEN and its own automation and parameter
locks (up to 64 records while it is not the one playing); DIV, SWING, GATE, QUANTIZE and the sound stay the track's.

- **SEQ > SEQUENCE** (the fourth SEQ page, after STEP, DETAIL and AUTOMATION): KNOB 1..4 pick the sequence, 1..8, of tracks 1..4.
- **MENU > SYSTEM > SEQ SWITCH**: NOW (the default) switches at once, the playhead keeps its step; END, while the music
  plays, waits for the last step of the sequence playing, so the new one starts from its first step (the waiting one shows
  on the SEQUENCE page and "T1 NEXT 3" says it; choosing the one playing calls it off). Stopped, a switch is always at once.
  Not while a song plays.
- **CHAIN** (SEQ > SEQUENCE, PRESETS): turn PRESETS right to chain the selected track's sequence (the one playing, or the one
  waiting), left to take it out of the chain. Only the sequences you chained play one after the other, in order, the last back
  to the first; a sequence you did not chain loops by itself, and so does a single chained one. It advances on the last step of
  each pass while the music plays. A chained cell shows a bar at its left, the card "x3" (how many are chained), and the
  footer, where the sound's name is (PRESETS is not the sound on this page), "CHAIN ON" / "CHAIN OFF" for the selected sequence.
- **SONG**: PRESETS turns the sequence the selected cell plays: 0 (as saved: the one the project was saved on, as every song
  before) or 1..8 of that project's track. The PAT card says "S3", the line under the grid "NAME SEQ 3", and the footer says
  what PRESETS does ("PRESETS SEQ 3", or "SAVED"). A song holds up to six such chosen sequences ("TOO MANY SEQ" at PLAY
  otherwise). Older firmware reads a song with a sequence chosen as no song.
- SAVE keeps all of a project's sequences (the record holds the one playing, so an older firmware still opens it); RESTORE LAST
  keeps the music's. They live in the flash the user sample slots had, which builds with `FELUCCA_PATBANK=1` (the default) do not
  have. A song plays each project's sequence as it was saved.

### ARP > GEN: generative music (this branch)

The arp's MODE has a last value, **GEN**: the track plays by itself while the music plays (PLAY starts it on its first step,
STOP silences it), notes of its scale, and the keys play as they do with the arp OFF. **ARP > GEN** (the third ARP page) has
its knobs for the selected track:

- **DENS** (KNOB 1): how many steps of the loop play. A step plays when its own chance is below DENS, so turning it up adds
  notes to the same loop and turning it down takes them away; 0 plays nothing, 100 % every step.
- **CHORD** (KNOB 2): 0 plays single notes. From 1 a growing share of the steps are chords (all of them from about 50 %) and the
  chords get richer: a fifth or a third, then triads and their suspensions, sevenths, then 9ths and quartal stacks (up to four notes).
  All of them are built from the scale's own notes.
- **EVOLVE** (KNOB 3): how much the loop changes each time it comes round: 0 repeats it exactly; a little moves a few notes a step
  or two in the scale (now and then a new chance, chord or loudness); 100 % changes nearly every step each loop.
- **SCL** (KNOB 4): the scale (the track's SCL; ROOT and TRN, on the SCL page and layer, and the arp's OCT, which sets how many
  octaves it spans, pitch the same loop). The notes always follow them as they are turned.

LEN is the loop's length (up to 32 steps, at the arp's RATE; SWG, GATE and PROB apply too). The page draws the loop as it is
made: a bar a step, as high as its note, a mark over the chords, a line under the step playing. The knobs are saved with the
sequence banks (project SAVE, RESTORE LAST); the loop itself is made when GEN starts and when a project loads, and keeps through
STOP and PLAY. Record-arm a track to play GEN's notes into its sequence.

### SCL > CHORD: STR3 and STR7, a chord to strum (this branch)

The CHORD page's CHRD has two more values, **STR3** and **STR7**: the keys turn into a chord to strum, as on a guitar.

- **Black keys** (F#3 .. F#5, eleven) choose the chord and make no sound: the diatonic triad (STR3) or seventh (STR7) on the scale's
  1st, 2nd, 3rd ... degree (the track's ROOT and SCALE; a chromatic scale uses the major scale of ROOT), the keys past the scale's
  last degree the same chords an octave up. The chosen key lights, and the CHORD page names the chord ("G7").
- **White keys** (F3 .. G5, sixteen) are the strings of that chord: the chord's notes in order from the left, then the same an
  octave up, and so on, so sliding a finger over the keys strums up the chord, and a key held sounds its string until it is let go.
- **VOIC** voices the chord (CLOSE, OPEN, INV1, INV2, +OCT's bass string), **TRN** and the octave buttons move it. A MONO, LEGATO or
  UNISON part plays the strings one at a time. MIDI notes, the arp and kits (DRUM, slices) play as with CHRD OFF.

### SCL > CHORD: PLAY, chords with the white keys shaped by the black (this branch)

A third new value for CHRD, **PLAY**: every white key plays a chord (its own note, on the track's scale as the keys are; plain major
by default) and the **black keys held** with it shape the chord. The black keys make no sound. What each black key does is yours to
set (below); with the built in POP pack, for example: key 1 (F#3) MIN, key 2 MIN7, key 3 7, key 4 MAJ7, key 5 SUS4, key 6 SUS2,
key 7 DIM, key 8 +9, key 9 6, key 10 STRM, key 11 ARP. The chord is the one held when the white key goes down.

The actions (lowest black key first, so a later one changes what an earlier one set: MIN then 7 is a minor seventh; 7, +9 and +11
stack):

- **quality** MIN, DIM, AUG, SUS2, SUS4, POW; **seventh** 6, 7, MAJ7, and MIN7, DIM7, M7B5, MMAJ7 (a quality with it); 9, MAJ9, MIN9
  (with the ninth); **extras** +9, +11, +13 (added tones), INV (the root up an octave), BASS (the root an octave down).
- **STRM**: the notes of the chord one after the other, low to high, held until the key is let go. How fast is the **SPEED** below.
- **ARP**: one note at a time, round the chord, at the track's ARP RATE and GATE, until the key is let go.
- Up to six tones: past that the fifth goes first, then the highest. VOIC voices the chord as it does the other chord modes.

**SAVE > CHORDS** (the SAVE button's fifth page) is where the black keys are set: KNOB 1 PACK, 2 KEY (which black key), 3 ACT (what it
does), 4 SPEED (the strum, 0..200 ms between notes). The page shows all eleven keys as chips with what a C chord becomes with the one
edited. Packs are what the black keys do and the strum speed together: six are built in (POP, JAZZ, SUS, EXT, EASY, HARP) and four
are yours (USER1..4): turning KNOB 1 loads a pack at once (a "*" says it was changed since), and **OCT+ saves** what the black keys
do now into the user pack picked. The packs and the keys' setting are saved with the music (project SAVE, RESTORE LAST), in the
sequence banks' flash.
