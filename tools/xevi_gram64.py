#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
xevi_gram64.py - ゼビウス地形を X1turboZ 15kHz 320x200 「64色」形式へ変換する。

出力 (roms/arcade/xevious-out/):
  terrain64_preview.png : 320x200 スライスを 4bit/ch 量子化した 64色プレビュー(2倍)
  terrain64_gram.bin    : GRAM 6プレーン (B0,R0,G0,B1,R1,G1) 各 8000 byte = 48000 byte。
                          各プレーン = cell(row*40+col, 0..999) ごとに scanline0..7 の 8 byte。
                          pixel index bit: bit0->B0 bit1->R0 bit2->G0 bit3->B1 bit4->R1 bit5->G1。
                          byte の MSB=左端ピクセル (表示 mixgrph64 と同じ並び)。
  terrain64_pal.bin     : 64 エントリ × [addr_lo, addr_hi, Bnib, Rnib, Gnib] = 320 byte。
                          addr = pal4096banktbl[0][index] (grph4096 の 12bit アドレス)。
                          未使用 index は黒 (0,0,0)。tzterrain.asm がこれを読んで
                          grph4096 を設定する。

地形スライスは map_arcade_rot90.png (アーケード縦向き) から切り出す。
"""
import os
import struct

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT_DIR = os.path.join(ROOT, "roms", "arcade", "xevious-out")

# pal4096banktbl[0] (xmil palettes.c と一致): 64色 index -> grph4096 12bit アドレス
BANKTBL0 = [
    0x000, 0x008, 0x080, 0x088, 0x800, 0x808, 0x880, 0x888,
    0x004, 0x00C, 0x084, 0x08C, 0x804, 0x80C, 0x884, 0x88C,
    0x040, 0x048, 0x0C0, 0x0C8, 0x840, 0x848, 0x8C0, 0x8C8,
    0x044, 0x04C, 0x0C4, 0x0CC, 0x844, 0x84C, 0x8C4, 0x8CC,
    0x400, 0x408, 0x480, 0x488, 0xC00, 0xC08, 0xC80, 0xC88,
    0x404, 0x40C, 0x484, 0x48C, 0xC04, 0xC0C, 0xC84, 0xC8C,
    0x440, 0x448, 0x4C0, 0x4C8, 0xC40, 0xC48, 0xCC0, 0xCC8,
    0x444, 0x44C, 0x4C4, 0x4CC, 0xC44, 0xC4C, 0xCC4, 0xCCC,
]

W, H = 320, 200
# 切り出し位置 (rot90 マップ 1024x2048 内、地形の変化に富む領域)
CROP_X, CROP_Y = 352, 900


def q4(v):
    """8bit -> 4bit (0..15)。"""
    return v * 15 // 255


def main():
    src = Image.open(os.path.join(OUT_DIR, "map_arcade_rot90.png")).convert("RGB")
    slice_img = src.crop((CROP_X, CROP_Y, CROP_X + W, CROP_Y + H))

    px = slice_img.load()
    # 4bit/ch 量子化した色 -> index (最大64)
    color_index = {}       # (R4,G4,B4) -> index
    index_color = []       # index -> (R4,G4,B4)
    idx_map = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            r, g, b = px[x, y]
            key = (q4(r), q4(g), q4(b))
            i = color_index.get(key)
            if i is None:
                if len(index_color) >= 64:
                    # 64色超過: 最近傍の既存色へ (稀)
                    i = min(range(len(index_color)),
                            key=lambda j: sum((a - c) ** 2 for a, c in
                                              zip(key, index_color[j])))
                else:
                    i = len(index_color)
                    color_index[key] = i
                    index_color.append(key)
            idx_map[y][x] = i
    ncol = len(index_color)

    # プレビュー PNG (量子化色、2倍)
    prev = Image.new("RGB", (W, H))
    pp = prev.load()
    for y in range(H):
        for x in range(W):
            r4, g4, b4 = index_color[idx_map[y][x]]
            pp[x, y] = (r4 * 0x11, g4 * 0x11, b4 * 0x11)
    prev.resize((W * 2, H * 2), Image.NEAREST).save(
        os.path.join(OUT_DIR, "terrain64_preview.png"))

    # GRAM 6プレーン。plane p の bit = (index>>p)&1。
    # byte[plane][cell*8 + s], cell=row*40+col, MSB=左端ピクセル。
    planes = [bytearray(40 * 25 * 8) for _ in range(6)]
    for cr in range(25):
        for cc in range(40):
            cell = cr * 40 + cc
            for s in range(8):
                y = cr * 8 + s
                for p in range(6):
                    byte = 0
                    for pxi in range(8):
                        idx = idx_map[y][cc * 8 + pxi]
                        bit = (idx >> p) & 1
                        byte |= bit << (7 - pxi)
                    planes[p][cell * 8 + s] = byte
    gram = b"".join(bytes(pl) for pl in planes)
    with open(os.path.join(OUT_DIR, "terrain64_gram.bin"), "wb") as f:
        f.write(gram)

    # パレット: 64 エントリ [addr_lo, addr_hi, Bnib, Rnib, Gnib]
    palbin = bytearray()
    for i in range(64):
        addr = BANKTBL0[i]
        if i < ncol:
            r4, g4, b4 = index_color[i]
        else:
            r4 = g4 = b4 = 0
        palbin += struct.pack("<H", addr)
        palbin += bytes((b4, r4, g4))
    with open(os.path.join(OUT_DIR, "terrain64_pal.bin"), "wb") as f:
        f.write(palbin)

    print("地形スライス (%d,%d)-(%d,%d) %dx%d" %
          (CROP_X, CROP_Y, CROP_X + W, CROP_Y + H, W, H))
    print("使用色数 (4bit/ch 量子化後): %d / 64" % ncol)
    print("出力: terrain64_preview.png, terrain64_gram.bin (%d byte), "
          "terrain64_pal.bin (%d byte)" % (len(gram), len(palbin)))


if __name__ == "__main__":
    main()
