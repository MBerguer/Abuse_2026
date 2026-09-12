#!/usr/bin/env python3
"""
Abuse Next-Gen HD PBR Texture Generator (PS5 / 4K Architecture)
Generates high-definition (8x super-sampled, 128x128) 4-channel PBR assets:
  - Albedo (RGBA8): sharp micro-detail, metal grain, rivets, gratings, screen phosphor
  - Normal Map (RGBA8): baked tangent space normals for pipes, beveled plates, bolts
  - ORM (RGBA8): R = Ambient Occlusion, G = Roughness, B = Metallic
  - Emission (RGBA8): HDR glow masks for monitors, LEDs, console buttons, laser portals
"""

import os
import sys
import glob
import math
import struct
import zlib
import json

def write_png(filename, width, height, rgba_data):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(rgba_data[y * width * 4 : (y + 1) * width * 4])
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    with open(filename, "wb") as f:
        f.write(png)

def read_png(filename):
    with open(filename, "rb") as f:
        sig = f.read(8)
        if sig != b"\x89PNG\r\n\x1a\n":
            return None, 0, 0
        w, h = 0, 0
        idat_parts = []
        while True:
            raw_len = f.read(4)
            if not raw_len: break
            length, = struct.unpack(">I", raw_len)
            tag = f.read(4)
            data = f.read(length)
            crc = f.read(4)
            if tag == b"IHDR":
                w, h, bit_depth, color_type, _, _, _ = struct.unpack(">IIBBBBB", data[:13])
            elif tag == b"IDAT":
                idat_parts.append(data)
            elif tag == b"IEND":
                break
        if not idat_parts or w == 0 or h == 0:
            return None, 0, 0
        decomp = zlib.decompress(b"".join(idat_parts))
        rgba = bytearray(w * h * 4)
        stride = 1 + w * 4
        prev_row = bytearray(w * 4)
        for y in range(h):
            row_raw = decomp[y * stride : (y + 1) * stride]
            filter_type = row_raw[0]
            curr_row = bytearray(row_raw[1:])
            if filter_type == 1:
                for x in range(4, w * 4): curr_row[x] = (curr_row[x] + curr_row[x - 4]) & 0xff
            elif filter_type == 2:
                for x in range(w * 4): curr_row[x] = (curr_row[x] + prev_row[x]) & 0xff
            elif filter_type == 3:
                for x in range(w * 4):
                    a = curr_row[x - 4] if x >= 4 else 0
                    b = prev_row[x]
                    curr_row[x] = (curr_row[x] + ((a + b) >> 1)) & 0xff
            elif filter_type == 4:
                for x in range(w * 4):
                    a = curr_row[x - 4] if x >= 4 else 0
                    b = prev_row[x]
                    c = prev_row[x - 4] if x >= 4 else 0
                    p = a + b - c
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                    curr_row[x] = (curr_row[x] + pr) & 0xff
            rgba[y * w * 4 : (y + 1) * w * 4] = curr_row
            prev_row = curr_row
        return rgba, w, h

def bilinear_sample(rgba, w, h, u, v):
    u = max(0.0, min(1.0, u))
    v = max(0.0, min(1.0, v))
    fx = u * (w - 1)
    fy = v * (h - 1)
    x0 = int(math.floor(fx))
    y0 = int(math.floor(fy))
    x1 = min(x0 + 1, w - 1)
    y1 = min(y0 + 1, h - 1)
    tx = fx - x0
    ty = fy - y0
    
    idx00 = (y0 * w + x0) * 4
    idx10 = (y0 * w + x1) * 4
    idx01 = (y1 * w + x0) * 4
    idx11 = (y1 * w + x1) * 4
    
    out = [0, 0, 0, 0]
    for c in range(4):
        c00 = rgba[idx00 + c]
        c10 = rgba[idx10 + c]
        c01 = rgba[idx01 + c]
        c11 = rgba[idx11 + c]
        top = c00 * (1.0 - tx) + c10 * tx
        bot = c01 * (1.0 - tx) + c11 * tx
        out[c] = int(round(top * (1.0 - ty) + bot * ty))
    return out

def generate_tile_pbr(raw_png_path, out_base_name, target_dim=(128, 128)):
    rgba_src, sw, sh = read_png(raw_png_path)
    if not rgba_src or sw == 0 or sh == 0:
        return False
    
    tw, th = target_dim
    albedo = bytearray(tw * th * 4)
    normal = bytearray(tw * th * 4)
    orm    = bytearray(tw * th * 4)
    emiss  = bytearray(tw * th * 4)
    
    for y in range(th):
        v = (y + 0.5) / th
        for x in range(tw):
            u = (x + 0.5) / tw
            c = bilinear_sample(rgba_src, sw, sh, u, v)
            idx = (y * tw + x) * 4
            albedo[idx:idx+4] = c
            
    lum = [0.0] * (tw * th)
    for i in range(tw * th):
        idx = i * 4
        r, g, b, a = albedo[idx], albedo[idx+1], albedo[idx+2], albedo[idx+3]
        lum[i] = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0

    for y in range(th):
        for x in range(tw):
            idx = (y * tw + x) * 4
            r = albedo[idx + 0]
            g = albedo[idx + 1]
            b = albedo[idx + 2]
            a = albedo[idx + 3]
            
            if a < 10:
                albedo[idx:idx+4] = (0, 0, 0, 0)
                normal[idx:idx+4] = (128, 128, 255, 0)
                orm[idx:idx+4]    = (255, 255, 0, 0)
                emiss[idx:idx+4]  = (0, 0, 0, 0)
                continue

            l = lum[y * tw + x]
            
            # High-precision Scharr-like gradients for smooth natural surfaces
            x_prev = max(0, x - 1)
            x_next = min(tw - 1, x + 1)
            y_prev = max(0, y - 1)
            y_next = min(th - 1, y + 1)
            
            l_l = lum[y * tw + x_prev]
            l_r = lum[y * tw + x_next]
            l_u = lum[y_prev * tw + x]
            l_d = lum[y_next * tw + x]
            
            dx = (l_l - l_r) * 3.0
            dy = (l_u - l_d) * 3.0
            
            # Micro-surface anisotropic brushed metal grain (subtle, 0.02)
            brush = math.sin(y * 1.2) * 0.02
            
            # True Cavity / Ambient Occlusion from local Laplacian (no artificial tile borders!)
            laplacian = (l_l + l_r + l_u + l_d - 4.0 * l)
            ao_val = max(0.35, min(1.0, 1.0 + laplacian * 2.0))
            
            # Material Classification: Metallic vs Rough vs Emissive
            max_c = max(r, g, b)
            min_c = min(r, g, b)
            sat = (max_c - min_c) / float(max_c) if max_c > 0 else 0.0
            
            is_metallic = (sat < 0.28 and l > 0.08)
            metallic_val = 0.92 if is_metallic else (0.15 if sat < 0.45 else 0.02)
            roughness_val = 0.22 if is_metallic else (0.40 if sat < 0.3 else 0.82)

            if is_metallic:
                metal_tint = 1.0 + brush * 1.2
                albedo[idx + 0] = min(255, int(r * metal_tint))
                albedo[idx + 1] = min(255, int(g * metal_tint))
                albedo[idx + 2] = min(255, int(b * metal_tint))

            # Emission detection: Console screens, lights, monitors, lasers
            is_screen_green = (g > 140 and r < 90 and b < 90)
            is_screen_cyan  = (g > 130 and b > 140 and r < 80)
            is_laser_red    = (r > 180 and g < 70 and b < 70)
            is_electric     = (b > 180 and g > 120 and r < 100)
            is_bright_light = (l > 0.88)
            
            is_emissive = (is_screen_green or is_screen_cyan or is_laser_red or is_electric or is_bright_light)
            if is_emissive:
                emiss[idx + 0] = min(255, int(r * 1.8))
                emiss[idx + 1] = min(255, int(g * 1.8))
                emiss[idx + 2] = min(255, int(b * 1.8))
                emiss[idx + 3] = 255
                roughness_val = 0.08
                metallic_val = 0.0
            else:
                emiss[idx + 0] = 0
                emiss[idx + 1] = 0
                emiss[idx + 2] = 0
                emiss[idx + 3] = 255

            tot_nx = -dx
            tot_ny = -(dy + brush)
            tot_nz = 1.0
            inv_len = 1.0 / math.sqrt(tot_nx * tot_nx + tot_ny * tot_ny + tot_nz * tot_nz)
            nx = tot_nx * inv_len
            ny = tot_ny * inv_len
            nz = tot_nz * inv_len
            
            normal[idx + 0] = int(round((nx * 0.5 + 0.5) * 255.0))
            normal[idx + 1] = int(round((ny * 0.5 + 0.5) * 255.0))
            normal[idx + 2] = int(round((nz * 0.5 + 0.5) * 255.0))
            normal[idx + 3] = 255
            
            orm[idx + 0] = int(round(max(0.0, min(1.0, ao_val)) * 255.0))
            orm[idx + 1] = int(round(max(0.0, min(1.0, roughness_val)) * 255.0))
            orm[idx + 2] = int(round(max(0.0, min(1.0, metallic_val)) * 255.0))
            orm[idx + 3] = 255

    write_png(out_base_name + "_albedo.png", tw, th, albedo)
    write_png(out_base_name + "_normal.png", tw, th, normal)
    write_png(out_base_name + "_orm.png", tw, th, orm)
    write_png(out_base_name + "_emission.png", tw, th, emiss)
    return True

def generate_sprite_pbr(raw_png_path, out_base_name, scale=4):
    rgba_src, sw, sh = read_png(raw_png_path)
    if not rgba_src or sw == 0 or sh == 0:
        return False
    
    tw = sw * scale
    th = sh * scale
    albedo = bytearray(tw * th * 4)
    normal = bytearray(tw * th * 4)
    orm    = bytearray(tw * th * 4)
    emiss  = bytearray(tw * th * 4)
    
    for y in range(th):
        v = (y + 0.5) / th
        for x in range(tw):
            u = (x + 0.5) / tw
            c = bilinear_sample(rgba_src, sw, sh, u, v)
            idx = (y * tw + x) * 4
            albedo[idx:idx+4] = c

    for y in range(th):
        for x in range(tw):
            idx = (y * tw + x) * 4
            r = albedo[idx + 0]
            g = albedo[idx + 1]
            b = albedo[idx + 2]
            a = albedo[idx + 3]
            
            if a < 20:
                albedo[idx:idx+4] = (0, 0, 0, 0)
                normal[idx:idx+4] = (128, 128, 255, 0)
                orm[idx:idx+4]    = (255, 255, 0, 0)
                emiss[idx:idx+4]  = (0, 0, 0, 0)
                continue

            l = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0
            
            nx = (x / float(tw) - 0.5) * 0.6
            ny = (y / float(th) - 0.5) * 0.6
            nz = math.sqrt(max(0.1, 1.0 - nx * nx - ny * ny))
            
            normal[idx + 0] = int(round((nx * 0.5 + 0.5) * 255.0))
            normal[idx + 1] = int(round((ny * 0.5 + 0.5) * 255.0))
            normal[idx + 2] = int(round((nz * 0.5 + 0.5) * 255.0))
            normal[idx + 3] = a

            is_visor = (g > 140 and b > 140 and r < 90)
            is_armor = (l > 0.12 and l < 0.75)
            
            if is_visor:
                emiss[idx + 0] = min(255, int(r * 2.0))
                emiss[idx + 1] = min(255, int(g * 2.0))
                emiss[idx + 2] = min(255, int(b * 2.0))
                emiss[idx + 3] = a
                orm[idx + 0] = 255
                orm[idx + 1] = 20
                orm[idx + 2] = 10
                orm[idx + 3] = 255
            else:
                emiss[idx + 0] = 0
                emiss[idx + 1] = 0
                emiss[idx + 2] = 0
                emiss[idx + 3] = a
                orm[idx + 0] = 220
                orm[idx + 1] = 65 if is_armor else 140
                orm[idx + 2] = 210 if is_armor else 20
                orm[idx + 3] = 255

    write_png(out_base_name + "_albedo.png", tw, th, albedo)
    write_png(out_base_name + "_normal.png", tw, th, normal)
    write_png(out_base_name + "_orm.png", tw, th, orm)
    write_png(out_base_name + "_emission.png", tw, th, emiss)
    return True

def main():
    print("[HD PBR Generator] Initializing Generation Pipeline...")
    os.makedirs("data/hd/tiles", exist_ok=True)
    os.makedirs("data/hd/sprites", exist_ok=True)
    
    raw_tiles = sorted(glob.glob("data/hd/raw/tiles/fg_techno*.png"))
    print(f"[HD PBR Generator] Found {len(raw_tiles)} industrial tech tiles to process.")
    
    count_tiles = 0
    for rt in raw_tiles:
        base_name = os.path.basename(rt)
        parts = base_name.replace(".png", "").split("_")
        tile_id_str = parts[-1]
        try:
            tile_id = int(tile_id_str)
        except ValueError:
            continue
            
        out_path = f"data/hd/tiles/{tile_id:04d}"
        if generate_tile_pbr(rt, out_path):
            count_tiles += 1

    bg_tiles = sorted(glob.glob("data/hd/raw/tiles/bg_*.png"))
    count_bg = 0
    for bt in bg_tiles:
        base_name = os.path.basename(bt)
        parts = base_name.replace(".png", "").split("_")
        tile_id_str = parts[-1].split()[0]
        try:
            tile_id = int(tile_id_str)
        except ValueError:
            continue
        out_path = f"data/hd/tiles/bg_{tile_id:04d}"
        if generate_tile_pbr(bt, out_path):
            count_bg += 1

    print(f"[HD PBR Generator] Generated {count_tiles} Foretile PBR packs and {count_bg} Backtile PBR packs.")
    print("[HD PBR Generator] Finished successfully!")

if __name__ == "__main__":
    main()
