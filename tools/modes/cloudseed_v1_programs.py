# Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Prints Cloud Seed v1's factory programs (third_party/cloudseed-programs, MIT) in real units,
with v1's own scaling (ReverbController.h and AudioLib/ValueTables.cpp at commit 13a625e). The
Modes in source/dsp/Modes.cpp start from these numbers.
Usage: python3 -I tools/modes/cloudseed_v1_programs.py"""
import glob, json, os, re

def response(x, base):          # ValueTables: (base^x - 1) / base + offset, rescaled to 0..1
    raw = lambda v: base ** v / base
    lo = raw(0.0)
    return 0.0 if x <= 0 else (raw(x) - lo) / (1 - lo)

def oct4(x):
    raw = lambda v: (16 ** v - 1.0) / 16.0 + 0.125 / 2.0
    lo = raw(0.0)
    return 0.0 if x <= 0 else (raw(x) - lo) / (1 - lo)

dec2 = lambda x: response(x, 100.0)
dec3 = lambda x: response(x, 1000.0)
db = lambda g: -120.0 if g <= 0 else 20 * __import__("math").log10(g)

def real(p):
    on = lambda k: p.get(k, 0) >= 0.5
    return {
        "crossFeed": p["InputMix"],
        "preDelayMs": int(p["PreDelay"] * 1000),
        "lowCutHz": 20 + oct4(p["HighPass"]) * 980 if on("HiPassEnabled") else None,
        "highCutHz": 400 + oct4(p["LowPass"]) * 19600 if on("LowPassEnabled") else None,
        "taps": 1 + int(p["TapCount"] * 49), "tapLengthMs": int(p["TapLength"] * 500),
        "tapGainDb": db(dec2(p["TapGain"])), "tapDecay": p["TapDecay"],
        "earlyOn": on("DiffusionEnabled"), "earlyStages": 1 + int(p["DiffusionStages"] * 7.999),
        "earlyDelayMs": int(10 + p["DiffusionDelay"] * 90), "earlyFeedback": p["DiffusionFeedback"],
        "lines": 1 + int(p["LineCount"] * 11.999), "lineMs": int(20 + dec2(p["LineDelay"]) * 980),
        "decayS": 0.05 + dec3(p["LineDecay"]) * 59.95,
        "lateOn": on("LateDiffusionEnabled"), "lateStages": 1 + int(p["LateDiffusionStages"] * 7.999),
        "lateDelayMs": int(10 + p["LateDiffusionDelay"] * 90), "lateFeedback": p["LateDiffusionFeedback"],
        "lowShelfDb": db(dec2(p["PostLowShelfGain"])) if on("LowShelfEnabled") else None,
        "lowShelfHz": 20 + oct4(p["PostLowShelfFrequency"]) * 980,
        "highShelfDb": db(dec2(p["PostHighShelfGain"])) if on("HighShelfEnabled") else None,
        "highShelfHz": 400 + oct4(p["PostHighShelfFrequency"]) * 19600,
        "cutoffHz": 400 + oct4(p["PostCutoffFrequency"]) * 19600 if on("CutoffEnabled") else None,
        "earlyModMs": p["EarlyDiffusionModAmount"] * 2.5, "earlyModHz": dec2(p["EarlyDiffusionModRate"]) * 5,
        "lineModMs": p["LineModAmount"] * 2.5, "lineModHz": dec2(p["LineModRate"]) * 5,
        "lateModMs": p["LateDiffusionModAmount"] * 2.5, "lateModHz": dec2(p["LateDiffusionModRate"]) * 5,
        "crossSeed": p["CrossSeed"],
        "earlyOutDb": db(dec2(p["EarlyOut"])), "mainOutDb": db(dec2(p["MainOut"])),
        "lateStageTap": on("LateStageTap"),
    }

folder = os.path.join(os.path.dirname(__file__), "..", "..", "third_party", "cloudseed-programs")
for path in sorted(glob.glob(os.path.join(folder, "*.json"))):
    # two of the files write the keys without quotes (JavaScript style)
    text = re.sub(r'^(\s*)([A-Za-z]\w*)\s*:', r'\1"\2":', open(path).read(), flags=re.M)
    r = real(json.loads(text))
    print(os.path.basename(path)[:-5])
    print("   " + ", ".join(f"{k}={v:.3g}" if isinstance(v, float) else f"{k}={v}" for k, v in r.items()))
