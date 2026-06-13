#!/usr/bin/env python3
"""Prepare Cerberus OBJ/MTL for PCG Wavefront loader."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "Stuff" / "Cerberus_by_Andrew_Maximov"
OBJ = ROOT / "Cerberus.obj"
OUT = ROOT / "Cerberus_PBR.obj"

lines = OBJ.read_text(encoding="utf-8", errors="replace").splitlines()
out = ["# PCG Lab 8 — Cerberus (Andrew Maximov, teacher PBR textures)", "mtllib cerberus.mtl"]
inserted_use = False
for line in lines:
    if not inserted_use and line.startswith("f "):
        out.append("usemtl Cerberus")
        inserted_use = True
    if line.startswith("mtllib") or line.startswith("usemtl"):
        continue
    out.append(line)

OUT.write_text("\n".join(out) + "\n", encoding="utf-8")
print(f"Wrote {OUT} ({len(out)} lines)")
