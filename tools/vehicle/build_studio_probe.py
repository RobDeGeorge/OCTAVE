"""Generate a small, original studio light probe for the vehicle's PBR materials.

Uses the standard Radiance RGBE format; no downloaded imagery or dependencies.
Run with Python 3.10+ from any directory. The app consumes the generated HDR.
"""

import math
from pathlib import Path


def write_probe(destination: Path) -> None:
    width, height = 256, 128
    data = bytearray(f"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n".encode())
    for y in range(height):
        latitude = math.pi * (0.5 - (y + 0.5) / height)
        for x in range(width):
            longitude = 2 * math.pi * ((x + 0.5) / width - 0.5)
            ambient = 0.12 + 0.23 * max(0, math.sin(latitude))
            panels = 0.0
            for azimuth, elevation, strength in [(-0.8, 0.55, 2.5), (1.7, 0.4, 1.6), (-2.6, 0.7, 1.1)]:
                delta = math.atan2(math.sin(longitude - azimuth), math.cos(longitude - azimuth))
                panels += strength * math.exp(-((delta / 0.36) ** 8) - ((latitude - elevation) / 0.33) ** 8)
            rgb = (ambient + panels, ambient * 1.03 + panels, ambient * 1.08 + panels)
            mantissa, exponent = math.frexp(max(rgb))
            scale = mantissa * 256 / max(rgb)
            data.extend((*[min(255, int(c * scale)) for c in rgb], exponent + 128))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    print(f"Wrote {destination} ({len(data):,} bytes)")


if __name__ == "__main__":
    write_probe(Path(__file__).resolve().parents[2] / "frontend/assets/vehicle_studio.hdr")
