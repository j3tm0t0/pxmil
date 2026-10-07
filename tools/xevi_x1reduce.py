#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
xevi_x1reduce.py - ゼビウス背景マップの X1 向け減色 比較ツール。

xevi_extract.py のマップ生成を再利用し、以下を出力する:
  1. 3方式比較 (最近傍 / 誤差拡散 / 色別固定パターン網点) を同一範囲で横並び
  2. 色別固定パターン網点でマップ全面を減色した版
  3. turboZ アナログパレット(各色4bit=4096色中)から最適8色を選んだ版 + パレット

「色別固定パターン」: 地形は25色しか無いので、元の各色に 4x4 の固定網点
(X1デジタル8色の組合せ)を1対1で割り当てる。タイル単位で必ず同じ模様に
なるため見た目が安定し、圧縮にも向く。

使い方: python3 -P tools/xevi_x1reduce.py
"""
import os
import importlib.util

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT_DIR = os.path.join(ROOT, "roms", "arcade", "xevious-out")


def _load_extract():
    spec = importlib.util.spec_from_file_location(
        "xevi_extract", os.path.join(HERE, "xevi_extract.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


# 4x4 Bayer 行列 (0..15)
BAYER4 = [
    [0, 8, 2, 10],
    [12, 4, 14, 6],
    [3, 11, 1, 9],
    [15, 7, 13, 5],
]


def x1_8():
    """X1 デジタル8色 (R,G,B 各1bit)。"""
    return [(255 * ((i >> 2) & 1), 255 * ((i >> 1) & 1), 255 * (i & 1))
            for i in range(8)]


def nearest8(rgb):
    r, g, b = rgb[:3]
    return (255 if r >= 128 else 0, 255 if g >= 128 else 0, 255 if b >= 128 else 0)


def build_color_patterns(colors):
    """各ソース色 -> 4x4 の固定網点パターン(各セル = X1 8色)。
    チャンネルごとに Bayer 閾値で on/off を決める(位置依存=固定)。"""
    pat = {}
    for c in colors:
        r, g, b = c[:3]
        # 0..255 を 0..16 のレベルに
        lr, lg, lb = r * 16 // 256, g * 16 // 256, b * 16 // 256
        grid = [[None] * 4 for _ in range(4)]
        for y in range(4):
            for x in range(4):
                t = BAYER4[y][x]
                grid[y][x] = (255 if lr > t else 0,
                              255 if lg > t else 0,
                              255 if lb > t else 0)
        pat[c] = grid
    return pat


def reduce_pattern(src, patterns):
    """色別固定パターンで減色。パターンは画像の絶対座標 %4 で位相を固定。"""
    out = Image.new("RGB", src.size)
    sp, op = src.load(), out.load()
    w, h = src.size
    for y in range(h):
        for x in range(w):
            c = sp[x, y][:3]
            g = patterns.get(c)
            if g is None:
                op[x, y] = nearest8(c)
            else:
                op[x, y] = g[y & 3][x & 3]
    return out


def reduce_nearest(src):
    out = Image.new("RGB", src.size)
    sp, op = src.load(), out.load()
    for y in range(src.height):
        for x in range(src.width):
            op[x, y] = nearest8(sp[x, y])
    return out


# ---- turboZ 4096色: 使用頻度重み付き k-means で最適8色 ----
def quant4(v):
    """8bit -> 4bit/ch 相当 (0,17,34,...,255 の17段ではなく 16段: v*15/255 を復元)"""
    q = round(v * 15 / 255)
    return q * 255 // 15


def kmeans8(color_counts, iters=40):
    import random
    pts = [(c, n) for c, n in color_counts.items()]
    random.seed(1)
    # k-means++ 風に頻度重みで初期化
    centers = [max(pts, key=lambda p: p[1])[0][:3]]
    while len(centers) < 8:
        best, bestd = None, -1
        for c, n in pts:
            d = min(sum((a - b) ** 2 for a, b in zip(c[:3], ce)) for ce in centers)
            if d * n > bestd:
                bestd, best = d * n, c[:3]
        centers.append(best)
    for _ in range(iters):
        acc = [[0, 0, 0, 0] for _ in range(8)]
        for c, n in pts:
            ci = min(range(8), key=lambda i: sum(
                (a - b) ** 2 for a, b in zip(c[:3], centers[i])))
            for k in range(3):
                acc[ci][k] += c[k] * n
            acc[ci][3] += n
        for i in range(8):
            if acc[i][3]:
                centers[i] = tuple(acc[i][k] // acc[i][3] for k in range(3))
    # 各center を 4bit/ch に量子化
    return [tuple(quant4(v) for v in ce) for ce in centers]


def reduce_palette8(src, pal):
    out = Image.new("RGB", src.size)
    sp, op = src.load(), out.load()
    for y in range(src.height):
        for x in range(src.width):
            c = sp[x, y][:3]
            op[x, y] = min(pal, key=lambda p: sum(
                (a - b) ** 2 for a, b in zip(c, p)))
    return out


def label_row(imgs, labels, scale=2):
    from PIL import ImageDraw
    imgs = [im.resize((im.width * scale, im.height * scale), Image.NEAREST)
            for im in imgs]
    gap, top = 12, 18
    H = max(im.height for im in imgs)
    W = sum(im.width for im in imgs) + gap * (len(imgs) + 1)
    out = Image.new("RGB", (W, H + top + 4), (30, 30, 30))
    dr = ImageDraw.Draw(out)
    x = gap
    for im, lb in zip(imgs, labels):
        out.paste(im, (x, top))
        dr.text((x, 3), lb, fill=(255, 255, 255))
        x += im.width + gap
    return out


def main():
    m = _load_extract()
    g1, g2, g3, g4, pr = m.build_regions()
    rgb, bgpen, sppen, fgpen = m.build_palette(pr)
    map_img, used_codes, tile_cs = m.render_map(g2, g4, rgb, bgpen)
    rot = map_img.rotate(-90, expand=True)  # 1024 x 2048

    # マップ内の色と使用頻度を集計
    counts = {}
    px = map_img.load()
    for y in range(map_img.height):
        for x in range(map_img.width):
            c = px[x, y]
            counts[c] = counts.get(c, 0) + 1
    colors = list(counts.keys())

    patterns = build_color_patterns(colors)

    # 比較用サンプル (森・砂漠・水・基地を含む縦ストリップ)
    strip = rot.crop((0, 1400, 224, 1400 + 360))
    cmp3 = label_row(
        [reduce_nearest(strip), m.reduce_x1_dither(strip),
         reduce_pattern(strip, patterns)],
        ["nearest", "error-diffusion", "fixed-pattern(4x4)"])
    cmp3.save(os.path.join(OUT_DIR, "x1_reduce_compare3.png"))

    # 固定パターンで全面減色 (native を減色後 回転)
    full = reduce_pattern(map_img, patterns)
    full.rotate(-90, expand=True).save(
        os.path.join(OUT_DIR, "map_x1_pattern.png"))

    # turboZ 4096色中 最適8色
    pal8 = kmeans8(counts)
    tz = reduce_palette8(map_img, pal8)
    tz.rotate(-90, expand=True).save(
        os.path.join(OUT_DIR, "map_turboz_best8.png"))
    # turboZ の比較ストリップ + パレット帯
    tz_strip = reduce_palette8(strip, pal8)
    cmp_tz = label_row([strip, reduce_nearest(strip), tz_strip],
                       ["original(arcade)", "X1 digital8", "turboZ best8(4096)"])
    cmp_tz.save(os.path.join(OUT_DIR, "x1_turboz_compare.png"))

    # パレット swatch
    sw = Image.new("RGB", (8 * 24, 28), (30, 30, 30))
    swp = sw.load()
    for i, c in enumerate(pal8):
        for y in range(24):
            for x in range(24):
                swp[i * 24 + x, y] = c
    sw.save(os.path.join(OUT_DIR, "turboz_best8_palette.png"))

    print("出力: x1_reduce_compare3.png / map_x1_pattern.png /")
    print("      map_turboz_best8.png / x1_turboz_compare.png /"
          " turboz_best8_palette.png")
    print("マップ使用色数: %d" % len(colors))
    print("turboZ best8 (4bit/ch):")
    for c in pal8:
        print("  #%02X%02X%02X  (G%d R%d B%d /15)"
              % (c[0], c[1], c[2], c[1] * 15 // 255, c[0] * 15 // 255,
                 c[2] * 15 // 255))


if __name__ == "__main__":
    main()
