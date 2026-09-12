#!/usr/bin/env python3
"""
Packs the individual HD PBR tile PNGs into a contiguous binary cache
for lightning-fast (sub-millisecond) GPU upload to GL_TEXTURE_2D_ARRAY.
"""

import os
import glob
import struct
import zlib

def read_png_raw(filename):
    if not os.path.exists(filename):
        return None
    with open(filename, "rb") as f:
        sig = f.read(8)
        if sig != b"\x89PNG\r\n\x1a\n":
            return None
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
        if not idat_parts or w != 128 or h != 128:
            return None
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
                    curr_row[x] = (curr_row[x] + ((a + prev_row[x]) >> 1)) & 0xff
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
        return rgba

def main():
    out_bin = "data/hd/tiles_pbr.bin"
    NUM_SLICES = 512
    SLICE_W = 128
    SLICE_H = 128
    BYTES_PER_MAP = SLICE_W * SLICE_H * 4
    
    print(f"[PBR Packer] Packing {NUM_SLICES} slices into {out_bin}...")
    with open(out_bin, "wb") as f:
        # Header: Magic(8), Version(4), Width(4), Height(4), Slices(4)
        f.write(b"ABUSEPBR")
        f.write(struct.pack("<IIII", 1, SLICE_W, SLICE_H, NUM_SLICES))
        
        packed_count = 0
        for tid in range(NUM_SLICES):
            albedo_file = f"data/hd/tiles/{tid:04d}_albedo.png"
            normal_file = f"data/hd/tiles/{tid:04d}_normal.png"
            orm_file    = f"data/hd/tiles/{tid:04d}_orm.png"
            emiss_file  = f"data/hd/tiles/{tid:04d}_emission.png"
            
            albedo = read_png_raw(albedo_file)
            normal = read_png_raw(normal_file)
            orm    = read_png_raw(orm_file)
            emiss  = read_png_raw(emiss_file)
            
            if albedo and normal and orm and emiss:
                f.write(struct.pack("B", 1))
                f.write(albedo)
                f.write(normal)
                f.write(orm)
                f.write(emiss)
                packed_count += 1
            else:
                f.write(struct.pack("B", 0))
                
    print(f"[PBR Packer] Successfully packed {packed_count} complete PBR tile slices into {out_bin}!")

if __name__ == "__main__":
    main()
