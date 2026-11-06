#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""More factory presets for DRUM (DRUM-X), SHDRUM, SWARM, FMTONE and WAVETONE: the data files firmware/src/presets_*.inc / .h.

The sounds are not designed by ear: they are found. Each engine's existing presets are the seeds; candidates are crossbreeds of two seeds
(a knob between them, a waveform from either, the envelope and the sends of one) and bounded perturbations of one (a knob scaled, the
envelope's decay and release longer or shorter, the sends moved). Every candidate is rendered through the firmware's own engine
(tests/preset_probe.c, through hostsim.c) and measured: level, peak, DC, the tail after the note, the spectrum. Candidates that clip, have DC, ring on,
are too quiet or loud for the engine's seeds are dropped; of the rest the most different from everything already kept (the seeds and the
earlier picks, in a normalised feature space) are kept, up to a cap per engine. The names say what the sound is by its own measure (BRT / DRK,
SHT / LNG, SOFT / HARD) against its parent. Run:  python3 tools/gen_presets.py   (from the repository root; needs numpy and a C compiler).
Everything is seeded: the same firmware gives the same files. Audition them: this picks by measurement, not by ear."""
import json, os, random, re, subprocess, sys
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "firmware", "src")
TMP = "/tmp/gen_presets"
FS = 44100
CAPS = {"SWARM": 40, "FMTONE": 40, "WAVETONE": 40, "SHDRUM": 24, "DRUM": 40}
NCAND = {"SWARM": 200, "FMTONE": 200, "WAVETONE": 200, "SHDRUM": 120, "DRUM": 200}   # (engine_t.npresets is a uint8_t: base + candidates < 255)
FILES = {"SWARM": ("presets_swarm.inc", "presets_swarm_x.inc"), "FMTONE": ("presets_fmtone.inc", "presets_fmtone_x.inc"),
         "WAVETONE": ("presets_wavetone.inc", "presets_wavetone_x.inc"), "SHDRUM": ("presets_shdrum.inc", "presets_shdrum_x.inc"),
         "DRUM": ("presets_drum.inc", None)}
XSRC = {"SWARM": ("eng_swarm.c", r"static const int8_t SWM_X\[NELEM\(SWARM_PRESETS\)\]\[3\] = \{", "sparse"),
        "FMTONE": ("eng_fmtone.c", r"static const fmt_x_t FMT_X\[NELEM\(FMT_PRESETS\)\] = \{", "pos"),
        "WAVETONE": ("eng_wavetone.c", r"static const wvt_x_t WVT_X\[NELEM\(WVT_PRESETS\)\] = \{", "pos"),
        "SHDRUM": ("eng_shdrum.c", r"static const int8_t SHD_PRESET_X\[SHD_NPRESETS\]\[SHP_N\] = \{", "pos")}


def sh(cmd, **kw):
    r = subprocess.run(cmd, shell=True, cwd=ROOT, capture_output=True, text=True, **kw)
    if r.returncode:
        sys.exit("failed: %s\n%s" % (cmd, r.stderr[-2000:]))
    return r.stdout


def build_tool():
    inc = "-Ifirmware/hal -Ifirmware/bsp/chip -Ifirmware/bsp/boards/fm1 -Ifirmware/src -Ibuild/gen -Itests"
    sh("gcc -O2 -w %s -o %s/preset_probe tests/preset_probe.c -lm" % (inc, TMP))


def write_files(extras, cats, xrows, nbase):
    """extras: engine -> list of preset dicts; the .inc/.h/cat files from them"""
    for eng, (pf, xf) in FILES.items():
        ex = extras.get(eng, [])
        with open(os.path.join(SRC, pf), "w") as f:
            for p in ex:
                f.write('    {"%s", {%s}, {%s}, %d, %d, FX(%s), PAT(%d)},   /* %s */\n' % (
                    p["name"], ", ".join(map(str, p["e"])), ", ".join(map(str, p["env"])), p["fenv"], p["mono"],
                    ", ".join(str(v - 1) for v in p["fx"]), p["pat"], p["why"]))
        if xf:
            with open(os.path.join(SRC, xf), "w") as f:
                for k, p in enumerate(ex):
                    row = xrows[eng].get(p["parent"]) if XSRC[eng][2] == "sparse" else xrows[eng][p["parent"]]
                    if XSRC[eng][2] == "sparse":
                        if row is not None:
                            f.write("    [%d] = %s,   /* %s */\n" % (nbase[eng] + k, row, p["name"]))
                    else:
                        f.write("    %s,   /* %s */\n" % (row, p["name"]))
    with open(os.path.join(SRC, "presets_drum.h"), "w") as f:
        f.write("#define DRUM_NX %d\n" % len(extras.get("DRUM", [])))
    with open(os.path.join(SRC, "presets_cat.h"), "w") as f:
        for eng in ("SWARM", "FMTONE", "WAVETONE", "SHDRUM", "DRUM"):
            f.write('#define PCAT_%s "%s"\n' % (eng, "".join(p["cat"] for p in extras.get(eng, []))))


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return re.sub(r"//[^\n]*", "", s)


def parse_x(eng):
    fn, decl, kind = XSRC[eng]
    s = open(os.path.join(SRC, fn), encoding="utf-8").read()
    m = re.search(decl, s)
    i = m.end()
    j = s.index("\n};", i)
    body = strip_comments(s[i:j])
    body = "\n".join(l for l in body.split("\n") if not l.strip().startswith("#include"))
    if kind == "sparse":
        return {int(a): "{%s}" % b.strip() for a, b in re.findall(r"\[(\d+)\]\s*=\s*\{([^}]*)\}", body)}
    rows, depth, cur = [], 0, ""
    for ch in body:
        if ch == "{":
            depth += 1
        if depth > 0:
            cur += ch
        if ch == "}":
            depth -= 1
            if depth == 0:
                rows.append(cur)
                cur = ""
    return {k: r for k, r in enumerate(rows)}


def features(raw, drum):
    x = np.frombuffer(raw, dtype="<i4").astype(np.float64)
    n = len(x)
    peak = float(np.max(np.abs(x)))
    win = x[int(0.02 * FS):int(min(1.55 if drum else 1.0, n / FS) * FS)]
    rms = float(np.sqrt(np.mean(win ** 2))) + 1e-9
    dc = abs(float(np.mean(x))) / (peak + 1e-9)
    tail = float(np.sqrt(np.mean(x[-int(0.25 * FS):] ** 2))) / (rms + 1e-9)
    seg = x[int(0.03 * FS):int(0.03 * FS) + 8192] if not drum else x[0:8192]
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) + 1e-9
    fr = np.fft.rfftfreq(len(seg), 1 / FS)
    centroid = float(np.sum(fr * spec) / np.sum(spec))
    edges = np.geomspace(80, 12000, 9)
    bands = np.array([20 * np.log10(np.mean(spec[(fr >= edges[b]) & (fr < edges[b + 1])]) + 1e-9) for b in range(8)])
    bands -= bands.mean()
    env = np.sqrt(np.convolve(x ** 2, np.ones(441) / 441, mode="same"))
    pk = float(np.max(env[:int(1.2 * FS)])) + 1e-9
    below = np.where(env[int(0.02 * FS):] < pk * 10 ** (-30 / 20))[0]
    decay = (below[0] / FS + 0.02) if len(below) else n / FS
    return dict(rms_all=float(np.sqrt(np.mean(x ** 2))), peak=peak, rms=rms, dc=dc, tail=tail, centroid=centroid, bands=bands, decay=decay)


def vec(f):
    return np.concatenate([[np.log10(f["rms"]) * 2.0, np.log10(f["centroid"]) * 3.0, np.log10(f["decay"] + 0.05) * 2.5, min(f["tail"], 2) * 1.0], f["bands"] / 6.0])


def main():
    os.makedirs(TMP, exist_ok=True)
    for eng in FILES:                                   # seeds: the firmware without the extras
        write_files({}, {}, {e: {} for e in FILES}, {})
    build_tool()
    seeds = json.loads(sh("%s/preset_probe dump" % TMP))
    desc = json.load(open(os.path.join(ROOT, "build", "host", "desc.json")))
    edesc = {e["name"]: e["edit"] for e in desc["ENG"]}
    rng = random.Random(20261102)
    xrows = {e: parse_x(e) for e in XSRC}
    xrows["DRUM"] = {}
    base = {e: sum(1 for s in seeds if s["eng"] == e) for e in FILES}
    extras = {}
    cands = {}
    for eng in FILES:
        S = [s for s in seeds if s["eng"] == eng]
        if eng == "DRUM":
            S = [s for s in S if s["e"][0] <= 8]        # the DRUM-X kits (the SH kits load another way)
        ed = edesc[eng]
        out = []
        tries = 0
        while len(out) < NCAND[eng] and tries < NCAND[eng] * 10:
            tries += 1
            A = rng.choice(S)
            B = rng.choice(S)
            kind = rng.choice(["x", "p", "p", "e"]) if A is not B else rng.choice(["p", "p", "e"])
            e = list(A["e"]); env = list(A["env"]); fx = list(A["fx"]); fenv = A["fenv"]; mono = A["mono"]
            why = ""
            for k in range(8):
                d = ed[k]
                is_enum = d["fmt"] == 8 or (eng == "DRUM" and k == 0)
                lo, hi = d["min"], d["max"]
                if kind == "x":
                    if is_enum:
                        e[k] = A["e"][k] if rng.random() < 0.5 else B["e"][k]
                    else:
                        t = rng.choice([0.25, 0.5, 0.75])
                        e[k] = int(round(A["e"][k] * (1 - t) + B["e"][k] * t))
                    why = "x %s %s" % (A["name"], B["name"])
                elif kind == "p" and not is_enum and rng.random() < 0.55:
                    span = hi - lo
                    e[k] = int(min(hi, max(lo, round(A["e"][k] + rng.uniform(-0.28, 0.28) * span))))
                    why = "p %s" % A["name"]
            if kind == "x":
                if rng.random() < 0.5: env = list(B["env"])
                if rng.random() < 0.5: fx = list(B["fx"])
                fenv = A["fenv"] if rng.random() < 0.5 else B["fenv"]
            if kind in ("p", "e", "x") and rng.random() < 0.7:
                env[1] = int(min(127, max(0, env[1] * rng.choice([0.55, 0.75, 1.3, 1.7])))) if rng.random() < 0.6 else env[1]
                env[3] = int(min(127, max(0, env[3] * rng.choice([0.5, 0.75, 1.4, 1.8])))) if rng.random() < 0.6 else env[3]
                if rng.random() < 0.3: env[0] = int(min(127, env[0] + rng.choice([0, 6, 14, 30])))
                why = why or "e %s" % A["name"]
            if rng.random() < 0.5:
                fx = [int(min(127, max(0, v - 1 + rng.choice([-10, 0, 0, 12, 24])))) + 1 for v in fx]
            if eng == "DRUM":
                e[0] = A["e"][0]
            out.append(dict(eng=eng, name="", e=[int(v) for v in e], env=[int(v) for v in env], fenv=int(fenv), mono=int(mono), fx=[int(v) for v in fx],
                            pat=int(A["pat"]), cat=A["cat"], parent=A["pi"], why=why, parent_name=A["name"], b_name=B["name"], kind=kind))
        cands[eng] = out
        extras[eng] = out
    # pass 1: every candidate in the firmware, rendered
    write_files(extras, {}, xrows, base)
    build_tool()
    feats = {}
    for eng in FILES:
        ei = next(s["ei"] for s in seeds if s["eng"] == eng)
        n0 = base[eng]
        tot = n0 + len(cands[eng])
        d = "%s/%s" % (TMP, eng)
        os.makedirs(d, exist_ok=True)
        sh("%s/preset_probe probe %d 0 %d %s" % (TMP, ei, tot, d))
        drum = eng in ("DRUM", "SHDRUM")
        feats[eng] = [features(open("%s/%d_%d.raw" % (d, ei, i), "rb").read(), drum) for i in range(tot)]
        for i in range(tot):
            feats[eng][i]["live"] = int(open("%s/%d_%d.live" % (d, ei, i)).read())
        for line in sh("%s/preset_probe level %d 0 %d" % (TMP, ei, tot)).split("\n"):
            if line.strip():
                pi_, db = line.split()
                feats[eng][int(pi_)]["level_db"] = float(db)
        print("%s: %d seeds, %d candidates rendered" % (eng, n0, len(cands[eng])), flush=True)
    # selection
    final = {}
    allvec = [vec(f) for e in feats for f in feats[e]]
    mu, sd = np.mean(allvec, 0), np.std(allvec, 0) + 1e-6
    for eng in FILES:
        n0 = base[eng]
        F = feats[eng]
        seedF = F[:n0] if eng != "DRUM" else F[:9]
        r = np.array([f["rms"] for f in seedF]); dc_ok = 0.03
        rms_lo, rms_hi = r.min() * 0.6, r.max() * 1.6
        pk_ok = 52000.0
        tail_ok = max(0.06, max(f["tail"] for f in seedF) * 1.3)
        dec_hi = max(f["decay"] for f in seedF) * 1.4 + 0.2
        melodic = eng in ("SWARM", "FMTONE", "WAVETONE")   # the engine tests hold every preset to -6 .. +4 dB of ANALOG's SAW LEAD (flat envelope)
        pool = [(i, c) for i, c in enumerate(cands[eng])
                if (lambda f: f["peak"] < pk_ok and f["dc"] < dc_ok and rms_lo <= f["rms"] <= rms_hi and f["tail"] <= tail_ok and f["decay"] <= dec_hi
                    and (not melodic or -5.3 <= f["level_db"] <= 3.3) and (melodic or f["live"] == 0) and (eng != "SHDRUM" or f["rms_all"] >= 150.0))(F[n0 + i])]
        kept = [(vec(f) - mu) / sd for f in F[:n0]]
        chosen = []
        while len(chosen) < CAPS[eng] and pool:
            best, bd = None, 0.0
            for i, c in pool:
                v = (vec(F[n0 + i]) - mu) / sd
                dmin = min(float(np.linalg.norm(v - k)) for k in kept)
                if dmin > bd: best, bd = (i, c, v), dmin
            if best is None or bd < 1.15:
                break
            i, c, v = best
            kept.append(v); chosen.append((i, c, bd))
            pool = [(j, d) for j, d in pool if j != i]
        # names, by the measure against the parent; the list grouped by the parent's family
        def label(nm, n=5):
            w = [x for x in nm.replace("-", " ").split() if len(x) >= 3] or nm.split()
            return w[0][:n]
        used = set(s["name"] for s in seeds if s["eng"] == eng)
        count = {}
        out = []
        for i, c, bd in sorted(chosen, key=lambda t: (t[1]["parent"], t[1]["kind"], t[0])):
            f = F[n0 + i]
            pf = F[c["parent"]]
            if c["kind"] == "x":
                base_nm = "%s/%s" % (label(c["parent_name"], 4), label(c["b_name"], 4))
            else:
                dc_ = f["centroid"] / max(pf["centroid"], 1)
                dd = f["decay"] / max(pf["decay"], 0.05)
                tag = "BRT" if dc_ > 1.25 else "DRK" if dc_ < 0.8 else "LNG" if dd > 1.5 else "SHT" if dd < 0.67 else "SOFT" if f["rms"] < pf["rms"] * 0.8 else "HARD"
                base_nm = "%s %s" % (label(c["parent_name"], 5), tag)
            k = count.get(base_nm, 0) + 1
            count[base_nm] = k
            nm = base_nm if k == 1 and base_nm not in used else "%s %d" % (base_nm, max(k, 2))
            while nm in used:
                k += 1
                nm = "%s %d" % (base_nm, k)
            nm = nm[:11]
            used.add(nm)
            c["name"] = nm
            c["why"] = "%s, %s: %.0f Hz, %.2f s, %.0f dB; d %.1f" % (c["kind"], c["why"].split(" ", 1)[1] if " " in c["why"] else "", f["centroid"], f["decay"], 20 * np.log10(f["rms"]), bd)
            out.append(c)
        final[eng] = out
        print("%s: kept %d of %d candidates (%d passed the gates)" % (eng, len(out), len(cands[eng]), len(pool) + len(chosen)))
    write_files(final, {}, xrows, base)
    build_tool()
    json.dump({e: [p["name"] for p in final[e]] for e in final}, open("%s/names.json" % TMP, "w"), indent=1)
    print("written:", {e: len(final[e]) for e in final})


if __name__ == "__main__":
    main()
