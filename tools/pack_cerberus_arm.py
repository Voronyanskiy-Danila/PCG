#!/usr/bin/env python3
"""Pack Cerberus R + M maps into ARM (R=AO, G=roughness, B=metallic)."""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent / "Stuff" / "Cerberus_by_Andrew_Maximov"
TEX = ROOT / "Textures"

rough = Image.open(TEX / "Cerberus_R.jpg").convert("L")
metal = Image.open(TEX / "Cerberus_M.jpg").convert("L")
if metal.size != rough.size:
    metal = metal.resize(rough.size, Image.Resampling.BILINEAR)

ao = Image.new("L", rough.size, 255)
arm = Image.merge("RGB", (ao, rough, metal))
out = TEX / "Cerberus_ARM.jpg"
arm.save(out, quality=95)
print(f"Wrote {out}")
