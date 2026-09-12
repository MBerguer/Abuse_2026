#!/usr/bin/env python3
"""
Abuse Asset Extractor
Extracts Abuse SPEC (.spe) files (foretiles, backtiles, sprites, and images)
into high-fidelity PNG format using the authentic 256-color game palette.
"""

import os
import sys
import glob
import struct
import zlib
import json

def write_png(filename, width, height, rgba_data):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    raw = bytearray()
    for y in range(height):
        raw.append(0) # Filter: None
        raw.extend(rgba_data[y * width * 4 : (y + 1) * width * 4])
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    with open(filename, "wb") as f:
        f.write(png)

def load_palette(pal_path="data/art/back/backgrnd.spe"):
    with open(pal_path, "rb") as f:
        sig = f.read(8)
        if sig != b"SPEC1.0\x00":
            raise ValueError(f"Invalid SPEC signature in {pal_path}")
        count, = struct.unpack("<H", f.read(2))
        for _ in range(count):
            t, nl = struct.unpack("BB", f.read(2))
            raw_name = f.read(nl)
            name = raw_name.decode("ascii", "ignore").rstrip("\x00").strip()
            flags, = struct.unpack("B", f.read(1))
            if flags & 1:
                fnl, = struct.unpack("B", f.read(1))
                f.read(fnl)
            else:
                dsize, offset = struct.unpack("<II", f.read(8))
                if t == 2 and name == "palette":
                    pos = f.tell()
                    f.seek(offset)
                    ncol, = struct.unpack("<H", f.read(2))
                    raw_rgb = f.read(ncol * 3)
                    pal = []
                    for i in range(ncol):
                        r = raw_rgb[i * 3]
                        g = raw_rgb[i * 3 + 1]
                        b = raw_rgb[i * 3 + 2]
                        pal.append((r, g, b))
                    f.seek(pos)
                    return pal
    raise RuntimeError("Master palette not found in " + pal_path)

def extract_spec(file_path, master_pal, output_base="data/hd/raw"):
    spe_name = os.path.splitext(os.path.basename(file_path))[0]
    
    extracted = []
    with open(file_path, "rb") as f:
        sig = f.read(8)
        if sig != b"SPEC1.0\x00":
            return extracted
        count, = struct.unpack("<H", f.read(2))
        
        entries = []
        for _ in range(count):
            t, nl = struct.unpack("BB", f.read(2))
            raw_name = f.read(nl)
            name = raw_name.decode("ascii", "ignore").rstrip("\x00").strip()
            flags, = struct.unpack("B", f.read(1))
            if flags & 1:
                fnl, = struct.unpack("B", f.read(1))
                f.read(fnl)
            else:
                dsize, offset = struct.unpack("<II", f.read(8))
                entries.append((t, name, dsize, offset))
                
        for t, name, dsize, offset in entries:
            # Type 5: SPEC_FORETILE, Type 6: SPEC_BACKTILE, Type 4: SPEC_IMAGE, Type 7/21: SPEC_CHARACTER
            if t not in (4, 5, 6, 7, 21):
                continue
            
            f.seek(offset)
            try:
                w, h = struct.unpack("<HH", f.read(4))
            except Exception:
                continue
            
            if w <= 0 or h <= 0 or w > 2048 or h > 2048 or (w * h) > dsize:
                continue
            
            raw_pixels = f.read(w * h)
            if len(raw_pixels) != w * h:
                continue
            
            safe_name = "".join(c if c.isalnum() or c in "-_" else "_" for c in name)
            if not safe_name:
                safe_name = "unnamed"
                
            prefix = ""
            if t == 5:
                category = "tiles"
                prefix = f"fg_{spe_name}_"
            elif t == 6:
                category = "tiles"
                prefix = f"bg_{spe_name}_"
            elif t in (7, 21):
                category = "sprites"
                prefix = f"{spe_name}_"
            else:
                category = "images"
                prefix = f"{spe_name}_"
                
            out_filename = f"{prefix}{safe_name}.png"
            out_path = os.path.join(output_base, category, out_filename)
            
            rgba = bytearray(w * h * 4)
            is_transparent_type = (t in (5, 7, 21))
            
            for idx, p_idx in enumerate(raw_pixels):
                if is_transparent_type and p_idx == 0:
                    r, g, b, a = 0, 0, 0, 0
                else:
                    rgb = master_pal[p_idx] if p_idx < len(master_pal) else (0, 0, 0)
                    r, g, b, a = rgb[0], rgb[1], rgb[2], 255
                rgba[idx * 4 + 0] = r
                rgba[idx * 4 + 1] = g
                rgba[idx * 4 + 2] = b
                rgba[idx * 4 + 3] = a
                
            write_png(out_path, w, h, rgba)
            extracted.append({
                "type": t,
                "spe": file_path,
                "name": name,
                "category": category,
                "file": out_filename,
                "width": w,
                "height": h
            })
            
    return extracted

def main():
    print("[Extractor] Loading master palette...")
    pal = load_palette()
    print(f"[Extractor] Loaded palette with {len(pal)} colors.")
    
    spe_files = sorted(glob.glob("data/art/**/*.spe", recursive=True) + glob.glob("data/art/*.spe"))
    print(f"[Extractor] Found {len(spe_files)} SPEC files to process.")
    
    manifest = []
    for sf in spe_files:
        items = extract_spec(sf, pal)
        if items:
            manifest.extend(items)
            print(f"  Extracted {len(items):3d} assets from {sf}")
            
    manifest_path = "data/hd/raw/manifest.json"
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)
        
    print(f"[Extractor] Complete! Extracted {len(manifest)} assets into data/hd/raw/.")
    print(f"[Extractor] Manifest written to {manifest_path}.")

if __name__ == "__main__":
    main()
