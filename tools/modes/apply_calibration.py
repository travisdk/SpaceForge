# Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Puts the "TABLE name mode value" lines that `sf2_render --calibrate-mode N PASS` prints into
source/dsp/ModeTables.h.
Usage: python3 -I tools/modes/apply_calibration.py <output files ...>"""
import os, re, sys

path = os.path.join(os.path.dirname(__file__), "..", "..", "source", "dsp", "ModeTables.h")
text = open(path).read()
for name in sys.argv[1:]:
    for line in open(name):
        m = re.match(r"TABLE (\w+) (\d+) (.+)$", line.strip())
        if not m:
            continue
        table, mode, value = m.group(1), int(m.group(2)), m.group(3)
        head = re.search(r"(%s \{+)" % table, text)
        if value.startswith("{"):            # a row of 11: the mode-th row under the table
            start = head.end()
            rows = list(re.finditer(r"\n    \{[^}]*\},", text[start:]))
            r = rows[mode]
            text = text[:start + r.start()] + "\n    " + value + "," + text[start + r.end():]
        else:                                # one number per Mode on the table's line
            end = text.index("}", head.end())
            numbers = [v.strip() for v in text[head.end():end].split(",")]
            numbers[mode] = value
            text = text[:head.end()] + " " + ", ".join(numbers) + " " + text[end:]
open(path, "w").write(text)
