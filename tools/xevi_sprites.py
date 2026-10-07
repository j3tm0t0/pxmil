#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Xevious の自機/空中敵スプライトを 16x16 PCG 96B(パレットD)で生成。

アーケードのオブジェクトコード(tcdev42/re 逆アセンブル参照)→ MAME の
spritelayout で tile を特定し、decode_sprite の色をパレットD スロットへ
マッピング(青翼端は cyan=slot6 を強制保持、赤=slot5、灰body=白/灰/暗灰)。

出力(非コミット, roms/):
  shipdata_ext.inc / solvalou_ship.inc : Solvalou (tile80/code1, 機首右=rot180)
  jara_ext.inc   : Jara   (tile160-163, 空中敵。旧「誤認Solvalou」)
  torkan_ext.inc : Torkan (tile16-17,  空中敵スカウト)
各 .inc は 96B/フレームの db ブロック(TL/TR/BL/BR x B/R/G x8)。
クレジット: tcdev42/re (tcdev/jotd)。ROM/出力は非コミット。

自機の向き: engine(ship.inc gen_ship)は shipdata を回転せず描画するため、
  機首右になる向きで保存する。tile80 raw=機首左なので rot180。
  (emmscroll64 を -DSHIP -DSHIP_EXTDATA でビルド→headless描画で機首右を確認済)
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xevi_extract as X

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def slot_of(r, g, b):
    if r > 200 and g < 90 and b < 90:
        return 5                              # 赤
    if b > 150 and r < 120 and g < 120:
        return 6                              # 青 -> cyan(翼端保持)
    lum = (r + g + b) / 3
    return 1 if lum >= 190 else (2 if lum >= 120 else 3)   # 白/灰/暗灰

def gen96(ex, g3, sp, rgb, tile, code, rot=0):
    px = ex.decode_sprite(g3, tile)
    s = [[0]*16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            pen = sp[code*8 + px[y][x]]
            if pen != 0x80:
                s[y][x] = slot_of(*rgb[pen])
    if rot == 180:
        s = [[s[15-y][15-x] for x in range(16)] for y in range(16)]
    cells = [(0, 0), (8, 0), (0, 8), (8, 8)]
    d = bytearray()
    for (ox, oy) in cells:
        for pbit in (1, 2, 4):
            for yy in range(8):
                byte = 0
                for xx in range(8):
                    if s[oy+yy][ox+xx] & pbit:
                        byte |= (0x80 >> xx)
                d.append(byte)
    return bytes(d)

def inc_block(d, lab=''):
    cells = ['TL', 'TR', 'BL', 'BR']; planes = ['B', 'R', 'G']; L = []
    for ci, c in enumerate(cells):
        for pi, p in enumerate(planes):
            off = (ci*3+pi)*8
            L.append('\tdb\t%s\t; %s%s %s' %
                     (','.join('0x%02X' % b for b in d[off:off+8]), lab, c, p))
    return '\n'.join(L)

def main():
    ex = X
    g1, g2, g3, g4, pr = ex.build_regions()
    rgb, bg, sp, fg = ex.build_palette(pr)
    rd = os.path.join(ROOT, 'roms')

    # 自機 Solvalou: tile80 / code1 / 機首右(rot180)
    ship = gen96(ex, g3, sp, rgb, 80, 1, rot=180)
    hdr = ('; Solvalou (arcade code=$50 -> tile80, code1=灰body+赤+青翼端). 機首右=rot180.\n'
           '; slot1=白 2=灰 3=暗灰 5=赤 6=cyan(青翼端)\n')
    open(os.path.join(rd, 'shipdata_ext.inc'), 'w').write(hdr + inc_block(ship) + '\n')
    open(os.path.join(rd, 'solvalou_ship.inc'), 'w').write(hdr + inc_block(ship) + '\n')
    open(os.path.join(rd, 'solvalou_ship.bin'), 'wb').write(ship)

    # Jara (tile160-163, 4コマ) / Torkan (tile16-17, 2コマ)
    with open(os.path.join(rd, 'jara_ext.inc'), 'w') as f:
        f.write('; Jara (空中敵, arcade tile160-165 の前4コマ). 旧「誤認Solvalou」。\n')
        for i, t in enumerate([160, 161, 162, 163]):
            f.write(inc_block(gen96(ex, g3, sp, rgb, t, 7), 'f%d ' % i) + '\n')
    with open(os.path.join(rd, 'torkan_ext.inc'), 'w') as f:
        f.write('; Torkan (空中敵スカウト, arcade code=0x10 -> tile16/17).\n')
        for i, t in enumerate([16, 17]):
            f.write(inc_block(gen96(ex, g3, sp, rgb, t, 7), 'f%d ' % i) + '\n')
    with open(os.path.join(rd, 'grobda_ext.inc'), 'w') as f:
        f.write('; Grobda (動く地上物=戦車, arcade code=0x4C -> tile76/77). 道沿い移動+照準接近で前進。\n')
        for i, t in enumerate([76, 77]):
            f.write(inc_block(gen96(ex, g3, sp, rgb, t, 7), 'f%d ' % i) + '\n')
    print('wrote shipdata_ext.inc, solvalou_ship.inc/.bin, jara_ext.inc, torkan_ext.inc, grobda_ext.inc')

if __name__ == '__main__':
    main()
