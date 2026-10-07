#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
xevi2x1_64.py - ゼビウス地形を「タイルID方式 + X1turboZ 64色」でエンジン用に出力。

xevi2x1.py(8色hp2網点・24B/タイル)の 64色版。各タイル = 8x8px を
6プレーン(bank0 B/R/G + bank1 B/R/G)×8ラスタ = 48バイト。各ピクセルは
area 全体で作る 64色パレット(4bit/ch量子化)の index(0..63)で、
6bit を plane bit0..5 (B0,R0,G0,B1,R1,G1) に分解。網点は使わない(64色で十分)。

出力 (roms/, 非コミット):
  xtiles64.bin   : ユニークタイル表 48B/個。ID = tilebase + idx*48 (絶対RAMアドレス)。
  xtilemap64.bin : タイルID列 256列 x 25行 x 2B。
  xpal64.bin     : 64エントリ [addr_lo,addr_hi,Bnib,Rnib,Gnib]。addr=pal4096banktbl[0][i]。

使い方:
  python3 -P tools/xevi2x1_64.py --area 1 --emit [--tilebase 0x0103]
"""
import argparse
import os
import struct
import importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT_DIR = os.path.join(ROOT, "roms")
AREA_TBL_ADDR = 0x3eb3

BS0_LEN = 256
ACROSS_TOTAL = 28
ACROSS_USE = 25
ACROSS_SKIP = (ACROSS_TOTAL - ACROSS_USE) // 2
BS0_START = 0x0d

# pal4096banktbl[0] (xmil palettes.c と一致)
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


def load_extract():
    spec = importlib.util.spec_from_file_location(
        "xevi_extract", os.path.join(HERE, "xevi_extract.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def load_area_table():
    main = bytearray(0x4000)
    romdir = os.path.join(ROOT, "roms", "arcade", "xevious")
    for i, f in enumerate(["xvi_1.3p", "xvi_2.3m", "xvi_3.2m", "xvi_4.2l"]):
        with open(os.path.join(romdir, f), "rb") as fp:
            main[i * 0x1000:(i + 1) * 0x1000] = fp.read()
    return [main[AREA_TBL_ADDR + a] for a in range(16)]


def q4(v):
    return v * 15 // 255


def tile_pattern_64(ex, gfx2, rgb, bg_pen, code, color, fx, fy, cmap):
    """(code,color,flip) -> 48B (B0[8],R0[8],G0[8],B1[8],R1[8],G1[8])。
    cmap: (B4,R4,G4) -> index(0..63)。MSB=左端ピクセル。"""
    raw = ex.decode_bg_tile(gfx2, code)
    pal = [rgb[bg_pen[color * 4 + v]] for v in range(4)]
    planes = [bytearray(8) for _ in range(6)]   # B0,R0,G0,B1,R1,G1
    for y in range(8):
        sy = 7 - y if fy else y
        for x in range(8):
            sx = 7 - x if fx else x
            r, g, b = pal[raw[sy][sx]]
            idx = cmap[(q4(b), q4(r), q4(g))]
            for p in range(6):
                if (idx >> p) & 1:
                    planes[p][y] |= (0x80 >> x)
    return bytes(b"".join(planes))


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--area", type=int, default=1)
    ap.add_argument("--emit", action="store_true")
    ap.add_argument("--tilebase", type=lambda s: int(s, 0), default=0x0103)
    args = ap.parse_args(argv)

    ex = load_extract()
    g1, g2, g3, g4, pr = ex.build_regions()
    rgb, bg_pen, sp_pen, fg_pen = ex.build_palette(pr)
    table = load_area_table()
    bs1_base = table[args.area - 1]
    bs1_list = [(bs1_base - 1 + ACROSS_SKIP + k) & 0x7f for k in range(ACROSS_USE)]

    # --- pass1: area 全体の distinct 色 (4bit/ch, key=(B4,R4,G4)) を収集 ---
    cmap = {}
    order = []
    for bs0 in range(BS0_LEN):
        for bs1 in bs1_list:
            code, color, fx, fy = ex.bg_cell(g4, bs0, bs1)
            raw = ex.decode_bg_tile(g2, code)
            pal = [rgb[bg_pen[color * 4 + v]] for v in range(4)]
            for y in range(8):
                for x in range(8):
                    r, g, b = pal[raw[y][x]]
                    key = (q4(b), q4(r), q4(g))
                    if key not in cmap:
                        cmap[key] = len(order)
                        order.append(key)
    ncol = len(order)
    if ncol > 64:
        # 64超過は最近傍へ丸める(area1 では通常起きない)
        base = order[:64]
        newmap = {}
        for k in order:
            if cmap[k] < 64:
                newmap[k] = cmap[k]
            else:
                newmap[k] = min(range(64), key=lambda j: sum(
                    (a - c) ** 2 for a, c in zip(k, base[j])))
        cmap = newmap
        order = base
        ncol = 64

    # --- pass2: タイル展開 + ID列 ---
    pat_to_id = {}
    tiles = []
    tilemap = []
    for bs0 in range(BS0_LEN):
        col = []
        for bs1 in bs1_list:
            code, color, fx, fy = ex.bg_cell(g4, bs0, bs1)
            pat = tile_pattern_64(ex, g2, rgb, bg_pen, code, color, fx, fy, cmap)
            tid = pat_to_id.get(pat)
            if tid is None:
                tid = len(tiles)
                pat_to_id[pat] = tid
                tiles.append(pat)
            col.append(tid)
        tilemap.append(col)

    nuniq = len(tiles)
    tbl_size = nuniq * 48
    last = args.tilebase + (nuniq - 1) * 48
    print("=== Area %d (bs1_base=%d) 64色タイルID方式 ===" % (args.area, bs1_base))
    print("使用色数: %d / 64" % ncol)
    print("ユニークタイル: %d個 x 48B = %dB (RAM, 末尾0x%04X)" % (nuniq, tbl_size, last))
    print("タイルID列(EMM): %d列 x %d行 x 2B = %dB" %
          (BS0_LEN, ACROSS_USE, BS0_LEN * ACROSS_USE * 2))
    print("新規1列の展開量: %d行 x 48B = %dB" % (ACROSS_USE, ACROSS_USE * 48))
    if last > 0xffff:
        print("WARN: タイル表末尾が 16bit 超")

    if args.emit:
        os.makedirs(OUT_DIR, exist_ok=True)
        with open(os.path.join(OUT_DIR, "xtiles64.bin"), "wb") as f:
            for t in tiles:
                f.write(t)
        with open(os.path.join(OUT_DIR, "xtilemap64.bin"), "wb") as f:
            for col in tilemap:
                for tid in col:
                    addr = args.tilebase + tid * 48
                    f.write(bytes([addr & 0xff, (addr >> 8) & 0xff]))
        # パレット: 64エントリ [addr_lo,addr_hi,Bnib,Rnib,Gnib]
        palbin = bytearray()
        for i in range(64):
            addr = BANKTBL0[i]
            if i < ncol:
                b4, r4, g4v = order[i]
            else:
                b4 = r4 = g4v = 0
            palbin += struct.pack("<H", addr)
            palbin += bytes((b4, r4, g4v))
        with open(os.path.join(OUT_DIR, "xpal64.bin"), "wb") as f:
            f.write(palbin)
        print("出力: roms/xtiles64.bin (%dB @0x%04X), xtilemap64.bin, xpal64.bin(320B)"
              % (tbl_size, args.tilebase))
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
