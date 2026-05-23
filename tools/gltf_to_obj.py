# =============================================================================
# gltf_to_obj.py — конвертация Poly Haven glTF → OBJ/MTL для PCG Lab 3
# =============================================================================
#
# Читает rock_07_1k.gltf + .bin, пишет:
#   rock_07.obj  — позиции, UV, нормали, индексы
#   rock_07.mtl  — map_Kd, map_Bump (normal), map_disp (displacement)
#
# Импортер C++ (Importer_Wavefront_ObjMtl) подхватывает эти map_* строки.
# =============================================================================

import json
import struct
import sys
from pathlib import Path

COMPONENT_FLOAT = 5126
COMPONENT_USHORT = 5123


def read_accessor(bin_data: bytes, accessor: dict, buffer_view: dict) -> bytes:
    offset = buffer_view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    length = buffer_view["byteLength"]
    return bin_data[offset : offset + length]


def unpack_vec3(data: bytes, count: int) -> list[tuple[float, float, float]]:
    out = []
    for i in range(count):
        x, y, z = struct.unpack_from("<3f", data, i * 12)
        out.append((x, y, z))
    return out


def unpack_vec2(data: bytes, count: int) -> list[tuple[float, float]]:
    out = []
    for i in range(count):
        u, v = struct.unpack_from("<2f", data, i * 8)
        out.append((u, v))
    return out


def unpack_indices(data: bytes, count: int) -> list[int]:
    return list(struct.unpack_from(f"<{count}H", data, 0))


def convert(gltf_path: Path, material_name: str = "rock_07") -> None:
    gltf_path = gltf_path.resolve()
    root = gltf_path.parent
    gltf = json.loads(gltf_path.read_text(encoding="utf-8"))
    bin_path = root / gltf["buffers"][0]["uri"]
    bin_data = bin_path.read_bytes()

    prim = gltf["meshes"][0]["primitives"][0]
    attrs = prim["attributes"]
    acc_pos = gltf["accessors"][attrs["POSITION"]]
    acc_nrm = gltf["accessors"][attrs["NORMAL"]]
    acc_uv = gltf["accessors"][attrs["TEXCOORD_0"]]
    acc_idx = gltf["accessors"][prim["indices"]]

    bv_pos = gltf["bufferViews"][acc_pos["bufferView"]]
    bv_nrm = gltf["bufferViews"][acc_nrm["bufferView"]]
    bv_uv = gltf["bufferViews"][acc_uv["bufferView"]]
    bv_idx = gltf["bufferViews"][acc_idx["bufferView"]]

    positions = unpack_vec3(read_accessor(bin_data, acc_pos, bv_pos), acc_pos["count"])
    normals = unpack_vec3(read_accessor(bin_data, acc_nrm, bv_nrm), acc_nrm["count"])
    uvs = unpack_vec2(read_accessor(bin_data, acc_uv, bv_uv), acc_uv["count"])
    indices = unpack_indices(read_accessor(bin_data, acc_idx, bv_idx), acc_idx["count"])

    obj_path = root / f"{material_name}.obj"
    mtl_path = root / f"{material_name}.mtl"

    with obj_path.open("w", encoding="utf-8") as f:
        f.write(f"mtllib {material_name}.mtl\n")
        f.write(f"o {material_name}\n")
        f.write(f"usemtl {material_name}\n")
        for x, y, z in positions:
            f.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
        for u, v in uvs:
            f.write(f"vt {u:.6f} {v:.6f}\n")
        for x, y, z in normals:
            f.write(f"vn {x:.6f} {y:.6f} {z:.6f}\n")
        for i in range(0, len(indices), 3):
            a, b, c = indices[i] + 1, indices[i + 1] + 1, indices[i + 2] + 1
            f.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")

    # MTL: пути относительно папки rock_07/ — совпадают с LoadMaterialTextureSets
    with mtl_path.open("w", encoding="utf-8") as f:
        f.write(f"newmtl {material_name}\n")
        f.write("Ka 0.15 0.15 0.15\n")
        f.write("Kd 0.85 0.85 0.85\n")
        f.write("Ks 0.35 0.35 0.35\n")
        f.write("Ns 72.0\n")
        f.write(f"map_Kd textures/{material_name}_diff_1k.jpg\n")
        f.write(f"map_Bump textures/{material_name}_nor_dx_1k.jpg\n")
        f.write(f"map_disp textures/{material_name}_disp_1k.jpg\n")

    print(f"Wrote {obj_path} ({len(indices)//3} tris, {len(positions)} verts)")
    print(f"Wrote {mtl_path}")


if __name__ == "__main__":
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent.parent / "content/models/rock_07/rock_07_1k.gltf"
    convert(path)
