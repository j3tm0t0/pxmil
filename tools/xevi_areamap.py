#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
xevi_areamap.py - ゼビウス 16 エリアの地形を進行順にレンダリングする。

エリア構造はメイン CPU プログラム (xvi_1.3p/xvi_4.2l) の逆アセンブルで確定:
  - エリア番号は RAM 0x8187 (0-based: 0=エリア1)。
  - エリア開始ルーチン @0x597:
        LD A,(0x8187) / LD HL,0x3eb3 / RST 10 (HL+=A) / LD A,(HL)
        LD (0x8013),A            ; bs1_base = AREA_TBL[area]  ← ROM 0x3eb3
        LD HL,0x0d00 / LD (0x8010),HL  ; 各エリア bs0 は 0x0d(13) から開始
  - 塗り込み @0x2d4-0x31d: 0x8010 上位バイト→bs0(進行列), 0x8013→bs1_base,
    1列あたり bs1 を 32 行(0x20)塗る。→ bs0=進行軸, bs1=横断(画面幅)。
  - スクロール速度 0x8014=0xf8。更新 @0x2d8 は RST8(=HL+=2*A)で、0xf8 により
    0x8010 を毎フレーム -16 する → bs0 は開始値 13 から「減少」方向に進む。
    よって地形は bs0 = 13,12,...,0,255,254,... と 256 周期で巡る。
  - エリア進行 @0x606: (bs0-14)<0x36 の窓 (bs0=14..67) で INC (0x8187);
    16 になったら 6 (=エリア7) へループ。bs0 は 13 から減少するため、この窓に
    入るのは約 200 列後 = 1 エリアはほぼ 256 列(全周期)の長旅になる。
    → エリア 1..16 の後は 7..16 を繰り返す (ゼビウスの既知仕様)。

bs1_base はエリア中は固定 (0x8013 書込みは開始時のみ)。地形は MAME xevious_bb_r
準拠 (xevi_extract.bg_cell)。ROM 由来データは出力せずツールにも埋め込まない。

使い方: python3 -P tools/xevi_areamap.py
"""
import os
import importlib.util

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ROM_DIR = os.path.join(ROOT, "roms", "arcade", "xevious")
OUT_DIR = os.path.join(ROOT, "roms", "arcade", "xevious-out")

AREA_TBL_ADDR = 0x3eb3   # メイン CPU 空間でのエリア→bs1_base テーブル
BS0_START = 0x0d         # 各エリアの bs0 開始列
ACROSS = 28              # 可視横断タイル数 (224dot)
JLEN = 256               # 1 エリアで描画する進行方向の列数 (背景は 256 周期)


def load_area_table():
    """maincpu イメージを組んで 0x3eb3 から 16 バイト読む。"""
    main = bytearray(0x4000)
    for i, f in enumerate(["xvi_1.3p", "xvi_2.3m", "xvi_3.2m", "xvi_4.2l"]):
        with open(os.path.join(ROM_DIR, f), "rb") as fp:
            main[i * 0x1000:(i + 1) * 0x1000] = fp.read()
    return [main[AREA_TBL_ADDR + a] for a in range(16)]


def main():
    spec = importlib.util.spec_from_file_location(
        "xevi_extract", os.path.join(HERE, "xevi_extract.py"))
    ex = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ex)
    g1, g2, g3, g4, pr = ex.build_regions()
    rgb, bgpen, sppen, fgpen = ex.build_palette(pr)
    table = load_area_table()

    cache = {}

    def tile(code, color, fx, fy):
        k = (code, color, fx, fy)
        t = cache.get(k)
        if t:
            return t
        raw = ex.decode_bg_tile(g2, code)
        pal = [rgb[bgpen[color * 4 + v]] for v in range(4)]
        t = Image.new("RGB", (8, 8))
        tp = t.load()
        for y in range(8):
            sy = 7 - y if fy else y
            for x in range(8):
                sx = 7 - x if fx else x
                tp[x, y] = pal[raw[sy][sx]]
        cache[k] = t
        return t

    def area_portrait(bs1_base, jlen):
        # native: 横=進行(bs0), 縦=横断(bs1)。これを縦スクロール向きへ回転。
        nat = Image.new("RGB", (jlen * 8, ACROSS * 8), (0, 0, 0))
        for col in range(jlen):
            bs0 = (BS0_START - col) & 0xff   # 進行は bs0 減少方向 (0x8014=0xf8)
            for j in range(ACROSS):
                bs1 = (bs1_base - 1 + j) & 0x7f
                code, color, fx, fy = ex.bg_cell(g4, bs0, bs1)
                nat.paste(tile(code, color, fx, fy), (col * 8, j * 8))
        # 進行を縦(上=開始, 下=前進)に。
        return nat.rotate(90, expand=True)   # 幅=224, 高=jlen*8

    # --- 1) 16 エリア montage (各 120 列プレビュー) ---
    prev = [area_portrait(table[a], 120) for a in range(16)]
    w, h = prev[0].size
    cols, pad, lab = 8, 10, 16
    mon = Image.new("RGB", (cols * (w + pad) + pad,
                            2 * (h + lab + pad) + pad), (50, 50, 50))
    dr = ImageDraw.Draw(mon)
    for a in range(16):
        r, c = a // cols, a % cols
        x, y = pad + c * (w + pad), pad + r * (h + lab + pad)
        dr.text((x, y), "Area %d (bs1=%d)" % (a + 1, table[a]),
                fill=(255, 255, 0))
        mon.paste(prev[a], (x, y + lab))
    mon.save(os.path.join(OUT_DIR, "areas16_montage.png"))

    # --- 2) 進行順 1 本帯 (エリア 1..16 を縦に連結, 各フル 256 列) ---
    full = [area_portrait(table[a], JLEN) for a in range(16)]
    fw, fh = full[0].size
    sep = 3
    strip = Image.new("RGB", (fw, 16 * (fh + sep) + lab), (0, 0, 0))
    dr2 = ImageDraw.Draw(strip)
    yy = 0
    for a in range(16):
        strip.paste(full[a], (0, yy))
        dr2.text((2, yy + 2), "AREA %d" % (a + 1), fill=(255, 255, 0))
        yy += fh + sep
    strip.save(os.path.join(OUT_DIR, "areas16_strip.png"))

    # --- 3) 表 ---
    print("エリア→bs1_base テーブル (ROM 0x%04x):" % AREA_TBL_ADDR)
    print(" area | bs1_base | bs0 進行 (開始→減少)")
    end_bs0 = (BS0_START - (JLEN - 1)) & 0xff
    for a in range(16):
        print("  %2d  |   %3d    | %d→%d (256列, 減少方向・背景周期)"
              % (a + 1, table[a], BS0_START, end_bs0))
    print("横断は bs1_base-1 から 28 タイル (scrolldy により ±1 行の誤差あり)")
    print("ループ: エリア16の次は7 (RAM 0x8187 が 16→6)")
    print("出力: areas16_montage.png / areas16_strip.png (%dx%d)"
          % strip.size)


if __name__ == "__main__":
    main()
