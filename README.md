# Felucca

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

![Felucca 1.0](docs/felucca-1.0.png)

Multi-engine synthesizer firmware for the M-VAVE FM-1, **version 1.6**. Connect the FM-1 to a computer by USB, open the
[web installer](https://hugelton.github.io/Felucca/) in Chrome or Edge and press Install; no extra hardware is needed.
Installing is at your own risk: M-VAVE's updater or the installer's **Return to official V15** takes you back.
Want to look around first? [Try it in your browser](https://hugelton.github.io/Felucca/webapp/try/), no FM-1 needed.

- Install: [web installer](https://hugelton.github.io/Felucca/), or `tools/fm1_install.py` from a terminal
- Edit: [web editor](https://hugelton.github.io/Felucca/webapp/editor/) (developed in [Felucca-WebApp](https://github.com/hugelton/Felucca-WebApp))
- Read: the manual is in the [wiki](https://github.com/hugelton/Felucca/wiki/Home): [Installation](https://github.com/hugelton/Felucca/wiki/Installation) ·
  [Controls](https://github.com/hugelton/Felucca/wiki/Controls) · [Menu](https://github.com/hugelton/Felucca/wiki/Menu) ·
  [Engines](https://github.com/hugelton/Felucca/wiki/Engines) · [Release notes](https://github.com/hugelton/Felucca/wiki/Release-Notes) ·
  [Troubleshooting](https://github.com/hugelton/Felucca/wiki/Troubleshooting)
- Build: [BUILDING.md](BUILDING.md)

<a href="https://hugelton.itch.io/felucca"><img src="https://static.itch.io/images/badge-color.svg" alt="Available on itch.io" width="74"></a>

## What it is

Thirteen engines with factory presets, four tracks (8 voices shared), a 64-step sequencer (chords, ties, chance, conditions,
ratchets, nudge, locks, automation, live and step recording), SEQ TOOLS, songs, effects, a MOD matrix, MIDI (USB and TRS,
per-track channels, MIDI LEARN) and USB audio. More: [Features](https://github.com/hugelton/Felucca/wiki/Features).

## New in 1.6

- **Your own UI colours:** COLOR has an eleventh value, CUSTOM, set in the web editor with a live preview on the FM-1
- **The SEQ piano roll follows the play position** when the pattern is longer than one screen
- **Conditional steps** (1:2 … 4:4, FILL), **EUCLID, DOUBLE and HALF**, **a MIDI channel per track**, PHASE FOLD, three more drum kits and humanize in the MOD matrix (1.5.5 and 1.5.6 were never released: 1.6 carries them)
- ⚠ A project with conditional steps does not open on 1.5 or 1.5.1; projects saved by 1.4 or later do not open on 1.1.5.x

All changes and what to know before going back: [Release notes](https://github.com/hugelton/Felucca/wiki/Release-Notes) · [Upgrading from 1.1.5](https://github.com/hugelton/Felucca/wiki/Upgrading-from-1.1.5).

## Support and contributing

Please report what you find in [Issues](https://github.com/hugelton/Felucca/issues) (reproducible bugs, one per issue). Ideas and
requests go to [Discussions](https://github.com/hugelton/Felucca/discussions). Pull requests are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md).
Felucca is GPLv3 and you are free to fork it; before starting yet another fork, please consider whether a PR would do.

## AI disclaimer

Felucca is developed with the assistance of AI coding agents (code, tests, documentation, translation, maintenance). The
design, features, sound design and direction are the maintainer's or the community's, and no generative AI is used for music,
icons or artwork. See [On AI-Assisted Development and Responsibility](https://github.com/hugelton/Felucca/discussions/166).

## Credits and licence

Free software: [GPL-3.0-only](LICENSE), the Hügelton Sample Pack included; the bundled fonts and ported DSP keep their own licences
([LICENSES/](LICENSES/), [LICENSING.md](LICENSING.md)). Thanks to everyone whose work and contributions are in it: [CREDITS.md](CREDITS.md).

M-VAVE and FM-1 are trademarks of their respective owners. Felucca is not affiliated with or endorsed by them.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments

## Sequences (eight per track) — this branch, v1.6_sequences

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
