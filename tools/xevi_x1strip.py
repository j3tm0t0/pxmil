#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xevi_x1strip.py - ゼビウス地形を X1 横スクロール用の GRAM STRIP バイナリに変換。

tools/emmscroll.asm のデモ用。map_x1_nearest.png (既に X1 8色・最近傍減色済み)
から 200 ライン x (W*8)px のスライスを取り出し、X1 の GRAM セル/ラスタ/プレーン
配置でバイト列化する。周期ループ前提で W 列ぶんを出力。

出力 world.bin のレイアウト (unshifted):
  for col in 0..W-1:              # 画面の縦1列 = 横8px, 高さ200px(=25セル)
    for cellrow in 0..24:        # セル行 (各8ライン)
      for plane in (B,R,G):      # 3プレーン
        for raster in 0..7:      # セル内ラスタ (上から)
          byte                   # 8px (bit7=左端) のそのプレーンのビット

色→プレーン: B=青成分, R=赤成分, G=緑成分 (各 >127 で 1)。
X1 標準8色 index = B + R*2 + G*4。

使い方:
  python3 tools/xevi_x1strip.py [--width 80] [--y0 412] \
      [--src roms/arcade/xevious-out/map_x1_nearest.png] \
      [--out roms/world.bin] [--preview roms/world_preview.png]
"""
import argparse
import os
import sys

from PIL import Image

ROWS = 25            # セル行数 (200px / 8)
RASTERS = 8
PLANES = 3           # B, R, G


def color_bits(rgb):
    """RGB -> (B,R,G) ビット (各 0/1)。"""
    r, g, b = rgb[0], rgb[1], rgb[2]
    return (1 if b > 127 else 0,
            1 if r > 127 else 0,
            1 if g > 127 else 0)


def build_strip(src_path, width_cells, y0):
    im = Image.open(src_path).convert("RGB")
    W, H = im.size
    px = im.load()
    wpx = width_cells * 8
    if y0 + 200 > H:
        y0 = max(0, H - 200)
    out = bytearray()
    # plane ビットマスク: B=0, R=1, G=2
    for col in range(width_cells):
        for cellrow in range(ROWS):
            for plane in range(PLANES):       # 0=B,1=R,2=G
                for raster in range(RASTERS):
                    y = y0 + cellrow * 8 + raster
                    byte = 0
                    for p in range(8):        # 8px 横 (bit7=左)
                        x = (col * 8 + p) % wpx
                        bit = color_bits(px[x, y])[plane]
                        if bit:
                            byte |= (0x80 >> p)
                    out.append(byte)
    return bytes(out), (wpx, y0)


def render_preview(data, width_cells, out_png):
    """world.bin を 320x200(先頭40列=1画面幅) にレンダして確認用に保存。"""
    cols = min(40, width_cells)
    im = Image.new("RGB", (cols * 8, 200), (0, 0, 0))
    pal = [(0, 0, 0), (0, 0, 255), (255, 0, 0), (255, 0, 255),
           (0, 255, 0), (0, 255, 255), (255, 255, 0), (255, 255, 255)]
    for col in range(cols):
        base = col * (ROWS * PLANES * RASTERS)
        for cellrow in range(ROWS):
            cbase = base + cellrow * (PLANES * RASTERS)
            for raster in range(RASTERS):
                bB = data[cbase + 0 * RASTERS + raster]
                bR = data[cbase + 1 * RASTERS + raster]
                bG = data[cbase + 2 * RASTERS + raster]
                for p in range(8):
                    m = 0x80 >> p
                    idx = ((1 if bB & m else 0)
                           | (2 if bR & m else 0)
                           | (4 if bG & m else 0))
                    im.putpixel((col * 8 + p, cellrow * 8 + raster), pal[idx])
    im.save(out_png)


def main(argv=None):
    ap = argparse.ArgumentParser()
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap.add_argument("--width", type=int, default=80, help="ワールド幅 (セル数)")
    ap.add_argument("--y0", type=int, default=412, help="スライス開始Y")
    ap.add_argument("--src", default=os.path.join(
        root, "roms/arcade/xevious-out/map_x1_nearest.png"))
    ap.add_argument("--out", default=os.path.join(root, "roms/world.bin"))
    ap.add_argument("--preview", default=os.path.join(root, "roms/world_preview.png"))
    args = ap.parse_args(argv)

    data, (wpx, y0) = build_strip(args.src, args.width, args.y0)
    with open(args.out, "wb") as f:
        f.write(data)
    render_preview(data, args.width, args.preview)
    print("wrote %s: %d bytes (W=%d cells=%dpx, y0=%d, %d bytes/col)"
          % (args.out, len(data), args.width, wpx, y0,
             ROWS * PLANES * RASTERS))
    print("preview: %s" % args.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
