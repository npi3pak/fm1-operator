#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 npi3pak
"""OPERATOR presets: Live Operator .adv files and our own JSON presets -> fm_patch_t (firmware/apps/operator/fm.h).

  tools/oppreset.py FILE.adv|FILE.json ...     print each preset as JSON (our units)
  tools/oppreset.py --check DIR                convert every .adv under DIR, report values outside the ranges

A JSON preset is a partial patch over INIT: {"name": "Bell", "main": {"algorithm": 3}, "osc": [{"coarse": 3}]}.
Units are fm.h's: time codes, cB, pct * 10, unit * 1000, cents.
"""
import copy
import gzip
import json
import math
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

HARMONICS = 64
NAME_LEN = 32
TIME_MAX = 5778
SOURCES = ("VelDst", "KeyDst", "MidiCtrl.2", "MidiCtrl.1", "MidiCtrl.0")  # SRC_VEL, KEY, AFTERTOUCH, BEND, WHEEL


def time_code(ms):
    return max(0, min(TIME_MAX, round(1000 * math.log10(max(ms, 0.1) / 0.1))))


def cb(gain, top=0):
    return max(-7000, min(top, round(2000 * math.log10(max(gain, 0.0003162277571)))))


def cents(hz):
    return round(100 * (69 + 12 * math.log2(hz / 440)))


def env(attack=0.1, decay=600, release=50, loop=100, initial=-7000, peak=0, sustain=0, end=-7000, slopes=(0, 1000, 1000)):
    return {"attack": time_code(attack), "decay": time_code(decay), "release": time_code(release),
            "loop_time": time_code(loop), "initial": initial, "peak": peak, "sustain": sustain, "end": end,
            "a_slope": slopes[0], "d_slope": slopes[1], "r_slope": slopes[2], "loop_mode": 0, "repeat": 3,
            "time_vel": 0}


def osc(level, decay=1000, sustain=0):
    return {"on": 1, "wave": 0, "coarse": 1, "fine": 0, "fixed": 0, "fixed_freq": 1000, "fixed_multi": 4,
            "vel_coarse": 0, "vel_coarse_q": 1, "level": level, "feedback": 0, "phase": 0, "retrigger": 1,
            "vel_scale": 500, "key_scale": 0, "lfo_on": 1, "pitch_on": 1, "harm_count": 0, "harm_repeat": 0,
            "harm_normalize": 1, "harm": [255] + [0] * (HARMONICS - 1),
            "env": env(attack=0.1, decay=decay, release=400, sustain=sustain, end=-7000)}


# the defaults of a new Operator in Live 12: oscillator A alone, a sine
INIT = {
    "name": "Init",
    "main": {"algorithm": 0, "voices": 5, "retrigger": 0, "interpolation": 1, "antialias": 1, "transpose": 0,
             "bend_range": 5, "volume": -1800, "pan": 0, "pan_key": 0, "pan_rnd": 0, "tone": 700, "spread": 0,
             "glide": 0, "glide_time": time_code(50), "time": 0, "time_key": 0, "time_attack": 1},
    "osc": [osc(0), osc(-7000, 400, -2400), osc(-7000, 400, -2400), osc(-7000, 400, -2400)],
    "filter": {"on": 1, "type": 0, "circuit": 0, "circuit_bp": 0, "slope24": 1, "legacy_type": 1, "legacy_q": 1000,
               "freq": 12623, "res": 278, "morph": 0, "drive": 0, "freq_vel": 0, "freq_key": 1000,
               "env_amount": 0, "lfo_on": 0, "shaper": 0, "shaper_drive": 0, "shaper_wet": 1000,
               "env": env(attack=0.1, decay=600, release=50, initial=0, peak=1000, sustain=0, end=0)},
    "lfo": {"on": 0, "type": 0, "range": 0, "rate": 1000, "rate_synced": 3, "rate_key": 0, "retrigger": 1,
            "amount": 100, "amount_a": 1000, "amount_vel": 0, "pitch_on": 1, "dst_b": 0, "amount_b": 1000,
            "env": env(attack=0.1, decay=600, release=50, initial=-7000, peak=0, sustain=0, end=-7000)},
    "pitch": {"on": 0, "amount_a": 1000, "dst_b": 0, "amount_b": 1000, "env_amount": 1000,
              "env": env(attack=0.1, decay=400, release=50, initial=1200, peak=1200, sustain=0, end=0)},
    "route": [[{"dst": 0, "amount": 0}, {"dst": 0, "amount": 0}] for _ in SOURCES],
}

RANGES = {
    "time": (0, TIME_MAX), "level": (-7000, 0), "pct": (-1000, 1000), "unit": (0, 1000), "bool": (0, 1),
}


def merge(base, over):
    if isinstance(base, dict):
        unknown = set(over) - set(base)
        if unknown:
            raise ValueError(f"unknown fields: {sorted(unknown)}")
        return {k: merge(v, over[k]) if k in over else copy.deepcopy(v) for k, v in base.items()}
    if isinstance(base, list) and isinstance(over, list) and base and isinstance(base[0], (dict, list)):
        return [merge(b, o) for b, o in zip(base, over + base[len(over):])]
    return copy.deepcopy(over)


def load_json(path):
    return merge(INIT, json.loads(Path(path).read_text()))


# ---- .adv

def load_xml(path):
    data = Path(path).read_bytes()
    if data[:2] == b"\x1f\x8b":
        data = gzip.decompress(data)
    root = ET.fromstring(data)
    op = root.find("Operator")
    if op is None:
        raise ValueError(f"{path}: not an Operator preset")
    return op


def value(node, path):
    el = node.find(path)
    if el is None:
        raise ValueError(f"missing {path}")
    manual = el.find("Manual")
    v = (manual if manual is not None else el).get("Value")
    if v in ("true", "false"):
        return 1 if v == "true" else 0
    return float(v)


def adv_env(node, kind):
    def level(name):
        v = value(node, name)
        if kind == "pitch":
            return round(v * 100)
        if kind == "filter":
            return round(v * 1000)
        return cb(v)
    return {"attack": time_code(value(node, "AttackTime")), "decay": time_code(value(node, "DecayTime")),
            "release": time_code(value(node, "ReleaseTime")), "loop_time": time_code(value(node, "LoopTime")),
            "initial": level("AttackLevel"), "peak": level("DecayLevel"), "sustain": level("SustainLevel"),
            "end": level("ReleaseLevel"), "a_slope": round(value(node, "AttackSlope") * 1000),
            "d_slope": round(value(node, "DecaySlope") * 1000), "r_slope": round(value(node, "ReleaseSlope") * 1000),
            "loop_mode": int(value(node, "LoopMode")), "repeat": int(value(node, "RepeatTime")),
            "time_vel": round(value(node, "TimeVelScale") * 10)}


def adv_osc(o):
    h = o.find("UserHarmonics")
    return {"on": value(o, "IsOn"), "wave": int(value(o, "WaveForm")), "coarse": int(value(o, "Tune/Coarse")),
            "fine": round(value(o, "Tune/Fine")), "fixed": value(o, "Tune/FixedFrequencyOn"),
            "fixed_freq": round(value(o, "Tune/FixedFrequency") * 10),
            "fixed_multi": int(value(o, "Tune/FixedFrequencyMulti")),
            "vel_coarse": round(value(o, "Tune/VelCoarseScale") * 10),
            "vel_coarse_q": value(o, "Tune/VelCoarseScaleQuantized"), "level": cb(value(o, "Volume")),
            "feedback": round(value(o, "Feedback") * 10), "phase": round(value(o, "Phase") * 10),
            "retrigger": value(o, "Retrigger"), "vel_scale": round(value(o, "VelScale") * 10),
            "key_scale": round(value(o, "KeyScale") * 10), "lfo_on": value(o, "LfoOn"),
            "pitch_on": value(o, "PitchEnvOn"), "harm_count": int(h.find("NumHarmonics").get("Value")),
            "harm_repeat": int(h.find("ExpansionMode").get("Value")),
            "harm_normalize": 1 if h.find("Normalize").get("Value") == "true" else 0,
            "harm": [max(0, min(255, round(float(h.find(f"Harmonics.{k}").get("Value")) * 255)))
                     for k in range(HARMONICS)],
            "env": adv_env(o.find("Envelope"), "osc")}


def adv_route(m, src):
    def one(j):
        c = m.find(f"{src}/ModConnections.{j}")
        return {"dst": int(c.find("Connection").get("Value")), "amount": round(float(c.find("Amount").get("Value")) * 10)}
    return [one(0), one(1)]


def load_adv(path):
    o = load_xml(path)
    g, f, s, lfo, p = o.find("Globals"), o.find("Filter"), o.find("Shaper"), o.find("Lfo"), o.find("PitchEnv")
    name = o.find("UserName").get("Value") or Path(path).stem
    return {
        "name": name,
        "main": {"algorithm": int(value(g, "Algorithm")), "voices": int(g.find("NumVoices").get("Value")),
                 "retrigger": 1 if g.find("RetriggerMode").get("Value") == "true" else 0,
                 "interpolation": 1 if g.find("Interpolation").get("Value") == "true" else 0,
                 "antialias": 1 if g.find("AntiAlias").get("Value") == "true" else 0,
                 "transpose": int(value(g, "Transpose")), "bend_range": int(value(g, "PitchBendRange")),
                 "volume": cb(value(g, "Volume"), 600), "pan": round(value(g, "Panorama") * 1000),
                 "pan_key": round(value(g, "PanoramaKeyScale") * 10), "pan_rnd": round(value(g, "PanoramaRndScale") * 10),
                 "tone": round(value(g, "Tone") * 1000), "spread": round(value(g, "SpreadAmount") * 10),
                 "glide": value(g, "PortamentoOn"), "glide_time": time_code(value(g, "PortamentoTime")),
                 "time": round(value(o, "EnvScale/EnvTime") * 10),
                 "time_key": round(value(o, "EnvScale/EnvTimeKeyScale") * 10),
                 "time_attack": value(o, "EnvScale/EnvTimeIncludeAttack")},
        "osc": [adv_osc(o.find(f"Operator.{i}")) for i in range(4)],
        "filter": {"on": value(f, "OnOff"), "type": int(value(f, "Type")), "circuit": int(value(f, "CircuitLpHp")),
                   "circuit_bp": int(value(f, "CircuitBpNoMo")), "slope24": value(f, "Slope"),
                   "legacy_type": int(value(f, "LegacyType")), "legacy_q": round(value(f, "LegacyQ") * 1000),
                   "freq": cents(value(f, "Frequency")), "res": round(value(f, "Resonance") * 1000),
                   "morph": round(value(f, "Morph") * 1000), "drive": round(value(f, "Drive") * 100),
                   "freq_vel": round(value(f, "FrequencyVelScale") * 10),
                   "freq_key": round(value(f, "FrequencyKeyScale") * 10),
                   "env_amount": round(value(f, "EnvelopeAmount") * 10), "lfo_on": value(f, "LfoOn"),
                   "shaper": int(value(s, "Type")), "shaper_drive": round(value(s, "Drive") * 100),
                   "shaper_wet": round(value(s, "DryWet") * 10), "env": adv_env(f.find("Envelope"), "filter")},
        "lfo": {"on": value(lfo, "LfoOn"), "type": int(value(lfo, "LfoType")), "range": int(value(lfo, "LfoRange")),
                "rate": round(value(lfo, "LfoRate") * 10), "rate_synced": int(value(lfo, "LfoRateSynced")),
                "rate_key": round(value(lfo, "LfoRateKeyScale") * 1000), "retrigger": value(lfo, "LfoRetrigger"),
                "amount": round(value(lfo, "LfoAmount") * 1000), "amount_a": round(value(lfo, "LfoAmountA") * 10),
                "amount_vel": round(value(lfo, "LfoAmountVelScale") * 1000), "pitch_on": value(lfo, "LfoPitchEnvOn"),
                "dst_b": int(value(lfo, "ModDst/Connection")), "amount_b": round(value(lfo, "ModDst/Amount") * 10),
                "env": adv_env(lfo.find("LfoEnv"), "lfo")},
        "pitch": {"on": value(p, "PitchEnvOn"), "amount_a": round(value(p, "PitchEnvAmountA") * 10),
                  "dst_b": int(value(p, "ModDst/Connection")), "amount_b": round(value(p, "ModDst/Amount") * 10),
                  "env_amount": round(value(p, "PitchEnv/EnvelopeAmount") * 1000),
                  "env": adv_env(p.find("PitchEnv"), "pitch")},
        "route": [adv_route(o.find("MidiCtrl"), src) for src in SOURCES],
    }


def load(path):
    return load_json(path) if str(path).endswith(".json") else load_adv(path)


# ---- checks and C

def env_problems(e, kind, where):
    lv = {"osc": (-7000, 0), "lfo": (-7000, 0), "filter": (0, 1000), "pitch": (-4800, 4800)}[kind]
    out = []
    for k in ("attack", "decay", "release", "loop_time"):
        out += check(e[k], RANGES["time"], f"{where}.{k}")
    for k in ("initial", "peak", "sustain", "end"):
        out += check(e[k], lv, f"{where}.{k}")
    for k in ("a_slope", "d_slope", "r_slope"):
        out += check(e[k], (-1000, 1000), f"{where}.{k}")
    return out + check(e["loop_mode"], (0, 4), f"{where}.loop_mode") + check(e["repeat"], (0, 14), f"{where}.repeat")


def check(v, rng, where):
    return [] if rng[0] <= v <= rng[1] else [f"{where} = {v} outside {rng}"]


def problems(p):
    m, f, lfo, pe = p["main"], p["filter"], p["lfo"], p["pitch"]
    out = check(m["algorithm"], (0, 10), "algorithm") + check(m["voices"], (0, 13), "voices")
    out += check(m["volume"], (-7000, 600), "volume") + check(m["transpose"], (-48, 48), "transpose")
    for i, o in enumerate(p["osc"]):
        w = f"osc{i}"
        out += check(o["wave"], (0, 22), f"{w}.wave") + check(o["coarse"], (0, 48), f"{w}.coarse")
        out += check(o["fine"], (0, 1000), f"{w}.fine") + check(o["fixed_multi"], (0, 5), f"{w}.fixed_multi")
        out += check(o["fixed_freq"], (100, 20000), f"{w}.fixed_freq") + check(o["harm_count"], (0, 2), f"{w}.harm_count")
        out += env_problems(o["env"], "osc", w)
    out += check(f["type"], (0, 4), "filter.type") + check(f["circuit"], (0, 4), "filter.circuit")
    out += check(f["legacy_type"], (0, 13), "filter.legacy_type") + check(f["freq"], (cents(30) - 1, cents(18500) + 1), "filter.freq")
    out += check(f["res"], (0, 1250), "filter.res") + check(f["shaper"], (0, 4), "filter.shaper")
    out += env_problems(f["env"], "filter", "filter") + env_problems(lfo["env"], "lfo", "lfo")
    out += env_problems(pe["env"], "pitch", "pitch")
    out += check(lfo["type"], (0, 6), "lfo.type") + check(lfo["range"], (0, 2), "lfo.range")
    out += check(lfo["dst_b"], (0, 23), "lfo.dst_b") + check(pe["dst_b"], (0, 23), "pitch.dst_b")
    for s, pair in enumerate(p["route"]):
        for j, r in enumerate(pair):
            out += check(r["dst"], (0, 23), f"route{s}.{j}.dst")
    return out


def latin1(s):
    b = s.encode("latin-1", "replace")[:NAME_LEN - 1]
    return "".join(f"\\{c:03o}" if c < 32 or c > 126 or c in (34, 92) else chr(c) for c in b)


def c_value(v):
    if isinstance(v, dict):
        return "{" + ", ".join(f".{k} = {c_value(x)}" for k, x in v.items()) + "}"
    if isinstance(v, list):
        return "{" + ", ".join(c_value(x) for x in v) + "}"
    return str(int(v))


def c_patch(p):
    body = {k: v for k, v in p.items() if k != "name"}
    return "{.name = \"" + latin1(p["name"]) + "\", " + c_value(body)[1:]


def main():
    args = sys.argv[1:]
    if args[:1] == ["--check"]:
        files = sorted(Path(args[1]).rglob("*.adv"))
        bad = 0
        for f in files:
            issues = problems(load_adv(f))
            bad += bool(issues)
            for i in issues:
                print(f"{f.name}: {i}")
        print(f"{len(files)} presets, {bad} with values outside the ranges")
        return 1 if bad else 0
    for a in args:
        print(json.dumps(load(a)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
