# Plugin ids

Each plugin engine has a 32-bit id, written as four printable characters with `FE_ID('a', 'b', 'c', 'd')`. A Felucca
host keeps an engine number for each id, so two engines must never share one.

- Ids of **upper-case letters and digits only** are Felucca's own engines.
- Pick yours with **at least one lower-case letter** in it, and add it to the table below by a pull request. The
  first come keeps it.
- Change the id only when you make a different engine. A new version of the same engine keeps its id and raises
  its version instead.

| Id | Engine | Author | Repository |
| --- | --- | --- | --- |
| `ANLG` | ANALOG (as a plugin of engine 0) | Hügelton Instruments | this repository, `plugins/analog.c` |
| `PHAS` | PHASE (engine 2) | Hügelton Instruments | `plugins/phase.c` |
| `LOFI` | LOFI (engine 3) | Hügelton Instruments | `plugins/lofi.c` |
| `TRIO` | TRIO (engine 6) | Hügelton Instruments | `plugins/trio.c` |
| `WHEL` | WHEEL (engine 7) | Hügelton Instruments | `plugins/wheel.c` |
| `NOIS` | NOISE (engine 11) | Hügelton Instruments | `plugins/noise.c` |
