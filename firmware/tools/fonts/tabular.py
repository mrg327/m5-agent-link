#!/usr/bin/env python3
"""Freeze the OpenType `tnum` feature into a font: the digits 0-9 in the cmap point at the tabular glyphs.

lv_font_conv cannot apply OpenType features, so gen.mjs runs this first and converts the result. Digits
then all have the same advance and a ticking time never moves its neighbours (UI-V3 2.1). Usage:
    tabular.py in.woff out.ttf
"""
import sys
from fontTools.ttLib import TTFont

f = TTFont(sys.argv[1])
gsub = f["GSUB"].table
subs = {}
for fr in gsub.FeatureList.FeatureRecord:
    if fr.FeatureTag != "tnum":
        continue
    for li in fr.Feature.LookupListIndex:
        for st in gsub.LookupList.Lookup[li].SubTable:
            st = getattr(st, "ExtSubTable", st)
            subs.update(getattr(st, "mapping", {}))
cmap = f["cmap"]
n = 0
for t in cmap.tables:
    for cp in range(0x30, 0x3A):
        g = t.cmap.get(cp)
        if g in subs:
            t.cmap[cp] = subs[g]
            n += 1
if not n:
    sys.exit("no tnum substitutions found")
f.flavor = None
f.save(sys.argv[2])
