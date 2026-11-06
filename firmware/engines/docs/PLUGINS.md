# Making a plugin engine

A plugin is a Felucca sound engine built on its own and loaded by Felucca at run time: on the FM-1 from a slot in
its flash, on a desktop from a shared library. Nobody needs to build a firmware to play it.

This guide covers writing, building, testing and sharing one. `src/felucca_plugin.h` is the contract (ABI 1), and
`src/felucca_engine.h` describes the engine interface in detail.

## What you need

- A C99 compiler (for the desktop build and the tests) and Python 3.
- For the FM-1: Docker (or Linux x86-64) and the JieLi toolchain, installed by Felucca's `tools/get_toolchain.sh`
  into `~/.jieli/toolchain`.

## Start from the example

`examples/fold/fold.c` is a whole engine of its own in about 90 lines. It has parameters, presets, HOME knobs,
`note_on`, `render` and an optional per-block hook. Copy the folder and rename the engine:

```
cp -r examples/fold examples/myengine
mv examples/myengine/fold.c examples/myengine/myengine.c
```

Then give it **your own id** (the last line, `FE_PLUGIN(...)`). An id is four printable characters with at least
one lower-case letter, for example `FE_ID('k', 'R', 'O', '1')`. List it in [PLUGIN_IDS.md](PLUGIN_IDS.md) by a pull
request so that no two engines share one.

The six engines in `src/` are larger examples. Each one also exists as a plugin in `plugins/` that takes the place of
the built-in engine of the same number.

## The engine

An engine is one `engine_t`:

| Field | What |
| --- | --- |
| `name` | Shown on the ENG knob and in the editor (up to 7 characters, upper case, as the built-in engines). |
| `page_title[2]` | The two EDIT pages' titles. |
| `edit[8]` | The eight parameters `P_E0` .. `P_E7`: label, format (`F_PCT`, `F_ENUM`, `F_CUTOFF`, …), range, default, value names, unit. |
| `presets`, `npresets` | Factory sounds: the eight values, the envelope, filter amount, mono, FX sends, a suggested pattern. |
| `knob[4]` | What HOME's four knobs turn (`P_E0` .. `P_E7`, `P_ATK`, `P_REL`). |
| `note_on(t, v)` | A voice starts or is retriggered. |
| `render(t, v, out, n, m)` | **Add** `n` samples (at most `CTL` = 32) of voice `v` to `out`. `m` carries the block's pitch increment and amplitude ramp; `voice_amp()` applies the ramp. |
| `block`, `amp`, `desc`, `keys`, `done`, `post` | Optional hooks (0 = none): per block, a custom amplitude, a mode-dependent label, a key map, an engine-owned envelope, an effect over the whole part. |
| `mem` | Bytes of per-part memory (`ENG_MEM(part)`, at most `FE_MEM_MAX`), zeroed when a part switches to the engine. |
| `keep`, `poly`, `sampled`, `oneshot`, `ownenv` | Flags; see `felucca_engine.h`. |

A plugin reads its parameters from `t->p[]` (`P_E0` .. `P_E7`, `P_ATK`, `P_REL`, `P_FTYPE`) and never writes them.
Keep a voice's state in `v->ph[]` and `v->s[]`, and a part's state in its engine memory. Static variables work too,
but the host does not reset them (give `FE_PLUGIN` a reset function if you use them).

### Rules for the code

- **Fixed point only**: no `float` or `double`. The FM-1's DSP has no floating point unit.
- **No library calls** and no allocation. `dsp.c` has what engines usually need: sines, polyBLEP oscillators, a
  state-variable filter, soft clipping, noise, `mulq15`, `clamp`.
- **No 64-bit products.** On the FM-1, inlined 64-bit multiplies among 16-bit loads can crash the chip (felucca-bsp
  `docs/CPU.md`).
- **Mind the cost.** A part renders up to 8 voices, and four parts share the CPU. A voice of ANALOG costs about 2 %
  of the FM-1's CPU. Felucca sheds voices when it runs out of time.

## Build

```
sh tools/build_plugin.sh examples/myengine/myengine.c         # build/plugins/myengine.{so,dylib,dll}
sh tools/build_plugin_fm1.sh examples/myengine/myengine.c 0   # build/fm1/myengine.s0.fep (slot 0)
sh tools/build_plugin_fm1.sh examples/myengine/myengine.c 1   # build/fm1/myengine.s1.fep (slot 1)
```

- `CC=x86_64-w64-mingw32-gcc` makes a Windows `.dll`.
- `SHARED=1` builds a desktop library on the host's tables, as the FM-1 build always does.
- An FM-1 blob is linked for one slot, so build both slots if you share it. The build checks the size:

| | Slot 0 | Slot 1 |
| --- | --- | --- |
| Code and constants | 8 KiB − 96 B | 7.5 KiB − 96 B |
| `.data` + `.bss` | 4 KiB | 4 KiB |

`tools/fep_pack.py` refuses a blob that the FM-1 would refuse.

## Test

`tests/plugin_test.c` is a small host that loads plugins and plays every preset through a phrase. `tests/run.sh`
uses it to check that the six plugins in `plugins/` render bit for bit like the built-in engines. For your engine:

- on a desktop, run Felucca's SDL2 build with `--plugins build/plugins` and play it;
- `--plugins-check` lists the plugin engines and checks that each one plays.

Then try it on the FM-1:

```
python3 tools/fm1_plugin.py install build/fm1/myengine.s0.fep      # Felucca's tools/fm1_plugin.py
```

If the FM-1 crashes inside your plugin, the next power-on skips the plugins. Erase the slot
(`fm1_plugin.py erase 0`) or install a fixed version.

## Versions and compatibility

- Raise your plugin's version (the third argument of `FE_PLUGIN`) when you release a new build. Keep the id.
- Keep your engine's parameters and their meaning from one version to the next. Saved sounds store the values,
  and they should sound the same after an update.
- ABI 1 is frozen (`felucca_plugin.h`, "Stability"). A plugin built for it loads in every Felucca with ABI 1.

## Sharing

Share the FM-1 blobs for both slots (`NAME.s0.fep`, `NAME.s1.fep`) and, if you like, the desktop libraries. Put them
in your own repository, with your own licence.

Felucca gives an extra permission under section 7 of the GPL: a plugin that talks to Felucca only through this
interface may use any licence (Felucca's `LICENSING.md`, "Plugin engines"). The interface files here are MIT.
