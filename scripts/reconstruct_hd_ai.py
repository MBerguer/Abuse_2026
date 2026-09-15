#!/usr/bin/env python3
"""
Abuse AI HD PBR Reconstructor
Synthesizes 128x128 4-channel PBR tiles (Albedo, Normal, ORM, Emission)
using the 5 AI-generated 1024x1024 photorealistic master materials.
"""

import os
import sys
import glob
import math
import struct
import zlib

def read_raw_ai_materials(bin_path="data/hd/ai_materials.bin"):
    with open(bin_path, "rb") as f:
        sig = f.read(8)
        assert sig == b"AIMAT1.0"
        count, w, h = struct.unpack("<III", f.read(12))
        layers = []
        layer_bytes = w * h * 4
        for i in range(count):
            data = f.read(layer_bytes)
            layers.append((w, h, data))
        return layers

def sample_mat(data, mw, mh, u, v):
    u = u % 1.0
    v = v % 1.0
    x = int(u * (mw - 1))
    y = int(v * (mh - 1))
    idx = (y * mw + x) * 4
    return (data[idx], data[idx+1], data[idx+2], data[idx+3])

def main():
    print("[AI HD Reconstructor] Loading AI Master Materials from data/hd/ai_materials.bin...")
    layers = read_raw_ai_materials("data/hd/ai_materials.bin")
    print(f"[AI HD Reconstructor] Loaded {len(layers)} AI master material layers (1024x1024).")

    os.makedirs("data/hd/tiles", exist_ok=True)
    os.makedirs("abuse.app/Contents/Resources/data/hd/tiles", exist_ok=True)

    NUM_SLICES = 512
    TW, TH = 128, 128
    BYTES_PER_SLICE = TW * TH * 4

    out_bin = "data/hd/tiles_pbr.bin"
    app_bin = "abuse.app/Contents/Resources/data/hd/tiles_pbr.bin"

    print(f"[AI HD Reconstructor] Synthesizing {NUM_SLICES} HD PBR tile slices...")

    with open(out_bin, "wb") as f_out:
        f_out.write(b"ABUSEPBR")
        f_out.write(struct.pack("<IIII", 1, TW, TH, NUM_SLICES))

        for tid in range(NUM_SLICES):
            # Classify tile into corresponding AI material
            mat_idx = 0
            if 10 <= tid <= 20:
                mat_idx = 1 # Console
            elif 50 <= tid <= 80:
                mat_idx = 2 # Vents
            elif (30 <= tid <= 49) or (220 <= tid <= 245):
                mat_idx = 3 # Floor / Catwalk
            elif tid >= 350:
                mat_idx = 4 # Dark BG
            else:
                mat_idx = 0 # Wall / Beams

            mw, mh, mdata = layers[mat_idx]

            # Tile-specific UV offset based on tile ID for seamless modular variety
            u_base = ((tid * 37) % 8) / 8.0
            v_base = ((tid * 53) % 8) / 8.0
            uv_scale = 1.0 / 8.0 # 8x8 tiles per 1024x1024 master texture

            albedo = bytearray(BYTES_PER_SLICE)
            normal = bytearray(BYTES_PER_SLICE)
            orm    = bytearray(BYTES_PER_SLICE)
            emiss  = bytearray(BYTES_PER_SLICE)

            # Sample AI master material across 128x128
            for y in range(TH):
                v = v_base + (y / float(TH)) * uv_scale
                for x in range(TW):
                    u = u_base + (x / float(TW)) * uv_scale
                    r, g, b, a = sample_mat(mdata, mw, mh, u, v)
                    idx = (y * TW + x) * 4
                    albedo[idx+0] = r
                    albedo[idx+1] = g
                    albedo[idx+2] = b
                    albedo[idx+3] = 255

            # Compute high-precision tangent space normals & ORM & Emission
            lum = [0.0] * (TW * TH)
            for i in range(TW * TH):
                idx = i * 4
                lum[i] = (0.2126 * albedo[idx] + 0.7152 * albedo[idx+1] + 0.0722 * albedo[idx+2]) / 255.0

            for y in range(TH):
                for x in range(TW):
                    idx = (y * TW + x) * 4
                    r, g, b = albedo[idx], albedo[idx+1], albedo[idx+2]

                    # Sobel filter for crisp normal relief
                    x0 = max(0, x - 1)
                    x1 = min(TW - 1, x + 1)
                    y0 = max(0, y - 1)
                    y1 = min(TH - 1, y + 1)

                    dx = (lum[y * TW + x0] - lum[y * TW + x1]) * 2.5
                    dy = (lum[y0 * TW + x] - lum[y1 * TW + x]) * 2.5

                    inv_len = 1.0 / math.sqrt(dx * dx + dy * dy + 1.0)
                    nx = -dx * inv_len
                    ny = -dy * inv_len
                    nz = 1.0 * inv_len

                    normal[idx+0] = int(round((nx * 0.5 + 0.5) * 255.0))
                    normal[idx+1] = int(round((ny * 0.5 + 0.5) * 255.0))
                    normal[idx+2] = int(round((nz * 0.5 + 0.5) * 255.0))
                    normal[idx+3] = 255

                    # Material PBR parameters
                    is_screen = (mat_idx == 1 and (g > 140 or b > 150) and r < 120)
                    is_led = (mat_idx == 1 or mat_idx == 4) and ((r > 190 and g < 90 and b < 90) or (g > 190 and r < 90))

                    if is_screen or is_led:
                        emiss[idx+0] = min(255, int(r * 2.2))
                        emiss[idx+1] = min(255, int(g * 2.2))
                        emiss[idx+2] = min(255, int(b * 2.2))
                        emiss[idx+3] = 255
                        ao = 255
                        rough = 20
                        metal = 0
                    else:
                        emiss[idx+0] = 0
                        emiss[idx+1] = 0
                        emiss[idx+2] = 0
                        emiss[idx+3] = 255

                        ao = int(round(max(0.3, min(1.0, 1.0 - abs(dx + dy) * 0.5)) * 255.0))
                        if mat_idx == 3: # Floor catwalk
                            rough = 75
                            metal = 220
                        elif mat_idx == 2: # Vents
                            rough = 60
                            metal = 235
                        elif mat_idx == 0: # Wall plates
                            rough = 45
                            metal = 210
                        else: # Dark conduits
                            rough = 90
                            metal = 160

                    orm[idx+0] = ao
                    orm[idx+1] = rough
                    orm[idx+2] = metal
                    orm[idx+3] = 255

            # Write PBR tile slice to binary pack
            f_out.write(struct.pack("B", 1)) # has_pbr = 1
            f_out.write(albedo)
            f_out.write(normal)
            f_out.write(orm)
            f_out.write(emiss)

    # Copy to app bundle
    with open(out_bin, "rb") as f_in, open(app_bin, "wb") as f_out:
        f_out.write(f_in.read())

    print(f"[AI HD Reconstructor] Successfully generated & packed {NUM_SLICES} AI PBR tiles into {out_bin} and {app_bin}!")

if __name__ == "__main__":
    main()
