# Writes content/models/white.dds — 1x1 RGBA fallback for materials without a texture.
import os
import struct

DDS_MAGIC = 0x20534444
DDS_HEADER_SIZE = 124
DDS_PIXELFORMAT_SIZE = 32
DDS_RGB = 0x00000040
DDSD_CAPS = 0x1
DDSD_HEIGHT = 0x2
DDSD_WIDTH = 0x4
DDSD_PITCH = 0x8
DDSD_PIXELFORMAT = 0x1000
DDSCAPS_TEXTURE = 0x1000


def main() -> None:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    path = os.path.join(root, "content", "models", "white.dds")
    os.makedirs(os.path.dirname(path), exist_ok=True)

    w, h = 1, 1
    pitch = w * 4
    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT
    pix = bytes([255, 255, 255, 255])

    ddspf = struct.pack(
        "<8I",
        DDS_PIXELFORMAT_SIZE,
        DDS_RGB,
        0,
        32,
        0x000000FF,
        0x0000FF00,
        0x00FF0000,
        0xFF000000,
    )
    reserved1 = b"\x00" * (11 * 4)
    body = struct.pack("<I", DDS_MAGIC)
    body += struct.pack("<7I", DDS_HEADER_SIZE, flags, h, w, pitch, 0, 1)
    body += reserved1
    body += ddspf
    body += struct.pack("<5I", DDSCAPS_TEXTURE, 0, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(body)
        f.write(pix)
    print("Wrote", path)


if __name__ == "__main__":
    main()
