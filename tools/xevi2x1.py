#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xevi2x1.py - タイルID方式データ生成/見積もり (emmscroll 次段用)。

ゼビウス 背景地形を「タイルID列 + ユニークタイル表」に分解する。横スクロール
デモ(tools/emmscroll.asm)が、エリア1全長(256列周期)を EMM 上のタイルID列で
持ち、RAM のユニークタイル表(X1 8色・網点済みの 8x8 パターン)から展開するための
前段。本スクリプトはまず「ユニークタイル数とデータ量の見積もり」を出す。

X1 GRAM パターン: 1タイル = 8x8px を 8ラスタ x 3プレーン(B,R,G) = 24バイト。
減色は tools/xevi_x1strip.py と同じ hp2 (横周期2網点)。タイル境界(8px)は網点周期
(横2/縦4)の倍数なので、タイルごとに位置非依存の固定パターンになる。

座標系: map_native と同じく bs0=進行(水平), bs1=横断。エリアは bs1_base 固定で
bs0=0..255 (256列周期)。横断は可視28タイルのうち中央 ACROSS_USE タイル(=200ライン)。

使い方:
  python3 tools/xevi2x1.py                 # 見積もりのみ
  python3 tools/xevi2x1.py --emit          # roms/ にタイルID列/タイル表を出力(非コミット)
"""
import argparse
import importlib.util
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ROM_DIR = os.path.join(ROOT, "roms", "arcade", "xevious")
OUT_DIR = os.path.join(ROOT, "roms")
AREA_TBL_ADDR = 0x3eb3

BS0_LEN = 256          # 1エリア周期 (進行列)
ACROSS_TOTAL = 28      # 可視横断タイル
ACROSS_USE = 25        # 使う中央タイル (=200ライン)
ACROSS_SKIP = (ACROSS_TOTAL - ACROSS_USE) // 2   # 上下スキップ

# 横2x縦4 網点 (xevi_x1strip.py と同じ)
BAYER2x4 = [[0, 4], [6, 2], [3, 7], [5, 1]]


def load_extract():
    spec = importlib.util.spec_from_file_location(
        "xevi_extract", os.path.join(HERE, "xevi_extract.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def load_area_table():
    main = bytearray(0x4000)
    for i, f in enumerate(["xvi_1.3p", "xvi_2.3m", "xvi_3.2m", "xvi_4.2l"]):
        with open(os.path.join(ROM_DIR, f), "rb") as fp:
            main[i * 0x1000:(i + 1) * 0x1000] = fp.read()
    return [main[AREA_TBL_ADDR + a] for a in range(16)]


def hp2_bits(rgb, lx, ly):
    """RGB -> (B,R,G) ビット。lx,ly はタイル内ローカル座標(位相用, タイル境界=0位相)。"""
    r, g, b = rgb[0], rgb[1], rgb[2]
    lr, lg, lb = r * 8 // 256, g * 8 // 256, b * 8 // 256
    t = BAYER2x4[ly & 3][lx & 1]
    return (1 if lb > t else 0, 1 if lr > t else 0, 1 if lg > t else 0)


def tile_pattern(ex, gfx2, rgb, bg_pen, code, color, fx, fy):
    """(code,color,flip) -> X1 GRAM 24バイト (cellrow内: B8,R8,G8 の順でラスタ0..7)。
       レイアウトは emmscroll の scatter 順 (plane内8ラスタ) に合わせ B[8],R[8],G[8]。"""
    raw = ex.decode_bg_tile(gfx2, code)
    pal = [rgb[bg_pen[color * 4 + v]] for v in range(4)]
    planes = [bytearray(8), bytearray(8), bytearray(8)]  # B,R,G, 各8ラスタ
    for y in range(8):                    # ラスタ
        sy = 7 - y if fy else y
        for x in range(8):                # 横8px (bit7=左)
            sx = 7 - x if fx else x
            bit = hp2_bits(pal[raw[sy][sx]], x, y)
            for p in range(3):
                if bit[p]:
                    planes[p][y] |= (0x80 >> x)
    return bytes(planes[0] + planes[1] + planes[2])


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--area", type=int, default=1, help="エリア番号 (1..16)")
    ap.add_argument("--emit", action="store_true",
                    help="roms/ にタイルID列/タイル表バイナリを出力")
    args = ap.parse_args(argv)

    ex = load_extract()
    g1, g2, g3, g4, pr = ex.build_regions()
    rgb, bg_pen, sp_pen, fg_pen = ex.build_palette(pr)
    table = load_area_table()
    bs1_base = table[args.area - 1]

    # 横断の中央 ACROSS_USE タイルの bs1 リスト (上から)
    bs1_list = [(bs1_base - 1 + ACROSS_SKIP + k) & 0x7f for k in range(ACROSS_USE)]

    pat_to_id = {}
    tiles = []              # id -> 24バイト
    tilemap = []            # [bs0][row] = id  (bs0=進行列, row=0..24 上から)
    cellset = set()
    for bs0 in range(BS0_LEN):
        col = []
        for bs1 in bs1_list:
            code, color, fx, fy = ex.bg_cell(g4, bs0, bs1)
            cellset.add((code, color, fx, fy))
            pat = tile_pattern(ex, g2, rgb, bg_pen, code, color, fx, fy)
            tid = pat_to_id.get(pat)
            if tid is None:
                tid = len(tiles)
                pat_to_id[pat] = tid
                tiles.append(pat)
            col.append(tid)
        tilemap.append(col)

    nuniq = len(tiles)
    idbytes = 1 if nuniq <= 256 else 2
    map_size = BS0_LEN * ACROSS_USE * idbytes
    tbl_size = nuniq * 24
    print("=== Area %d (bs1_base=%d) タイルID方式 見積もり ===" % (args.area, bs1_base))
    print("進行列 bs0: %d, 横断タイル: %d (中央, 上スキップ%d)" %
          (BS0_LEN, ACROSS_USE, ACROSS_SKIP))
    print("ユニーク (code,color,flip) 組: %d" % len(cellset))
    print("ユニーク X1パターン(網点後): %d  -> タイルID %dバイト/個" % (nuniq, idbytes))
    print("タイルID列 (EMM): %d列 x %dタイル x %dB = %d バイト" %
          (BS0_LEN, ACROSS_USE, idbytes, map_size))
    print("ユニークタイル表 (RAM): %d x 24B = %d バイト" % (nuniq, tbl_size))
    print("合計 (ID列+タイル表): %d バイト  (EMM 1MB に対し余裕)" %
          (map_size + tbl_size))
    print("参考: プリシフト4版ビットマップ方式なら %d バイト (4x256列x600B)" %
          (4 * BS0_LEN * 600))

    if args.emit:
        os.makedirs(OUT_DIR, exist_ok=True)
        with open(os.path.join(OUT_DIR, "xtiles.bin"), "wb") as f:
            for t in tiles:
                f.write(t)
        with open(os.path.join(OUT_DIR, "xtilemap.bin"), "wb") as f:
            for col in tilemap:
                for tid in col:
                    if idbytes == 1:
                        f.write(bytes([tid]))
                    else:
                        f.write(bytes([tid & 0xff, tid >> 8]))
        print("出力: roms/xtiles.bin (%dB), roms/xtilemap.bin (%dB)" %
              (tbl_size, map_size))
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
