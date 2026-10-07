#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Xevious の UI スプライト(ブラスター照準・弾)を 16x16 PCG 96B(パレットD)で生成。

アーケードのオブジェクトコード(tcdev42/re 逆アセンブル参照)から実スプライトを特定:
  crosshairs(照準) code=$14, bank=1 -> gfx tile (0x14&0x3f)+0x100 = 276 (破線四角)
  bomb(弾)        code=$1C, bank=1 -> gfx tile (0x1C&0x3f)+0x100 = 284。
                   照準へ落下しながら縮小: tile 284(大)/285(中)/286(小) の3コマ。
  (MAME draw_sprites: spriteram_3&0x80 で code=(val&0x3f)+0x100、size bit=0x2/0x1)
出力(非コミット, roms/):
  reticle_ext.inc  : 照準 1フレーム(slot6=cyan)。-DRETICLE_EXTDATA 用。
  blaster_ext.inc  : 弾 3フレーム(slot1=white)。-DBLASTER_EXTDATA 用。
各 .inc は 96B/フレームの db ブロック(TL/TR/BL/BR x B/R/G x8)。
クレジット: tcdev42/re (tcdev/jotd)。
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xevi_extract as X

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CODE = 54  # 透明判定用の sprite colour set (灰色フル表示)

def mask_96b(ex, g3, sp, tile, slot):
    """tile の非透明画素を slot(tc 1..7)で埋めた 96B PCG。"""
    px = ex.decode_sprite(g3, tile)
    cells = [(0, 0), (8, 0), (0, 8), (8, 8)]   # TL,TR,BL,BR
    out = bytearray()
    for (ox, oy) in cells:
        for pbit in (1, 2, 4):                 # B,R,G plane = slot bit0,1,2
            for y in range(8):
                byte = 0
                for x in range(8):
                    if sp[CODE*8 + px[oy+y][ox+x]] != 0x80 and (slot & pbit):
                        byte |= (0x80 >> x)
                out.append(byte)
    return bytes(out)

def inc_block(data, label=''):
    cells = ['TL', 'TR', 'BL', 'BR']; planes = ['B', 'R', 'G']; L = []
    for ci, c in enumerate(cells):
        for pi, p in enumerate(planes):
            off = (ci*3+pi)*8
            hexs = ','.join('0x%02X' % b for b in data[off:off+8])
            L.append('\tdb\t%s\t; %s%s %s' % (hexs, label, c, p))
    return '\n'.join(L)

def main():
    ex = X
    g1, g2, g3, g4, pr = ex.build_regions()
    rgb, bg, sp, fg = ex.build_palette(pr)
    rec = mask_96b(ex, g3, sp, 276, 6)         # 照準 cyan(slot6)
    with open(os.path.join(ROOT, 'roms', 'reticle_ext.inc'), 'w') as f:
        f.write('; Blaster reticle (arcade code=$14 -> tile 276, dashed square). slot6=cyan.\n')
        f.write('; lock 時は slot5(red) へ (emm-scroll 側で点滅/色変更)\n')
        f.write(inc_block(rec) + '\n')
    with open(os.path.join(ROOT, 'roms', 'blaster_ext.inc'), 'w') as f:
        f.write('; Blaster bomb (arcade code=$1C -> tile 284/285/286, shrinking). slot1=white. 3 frames.\n')
        for i, t in enumerate([284, 285, 286]):
            f.write(inc_block(mask_96b(ex, g3, sp, t, 1), 'f%d ' % i) + '\n')
    print('wrote roms/reticle_ext.inc (1 frame), roms/blaster_ext.inc (3 frames)')

if __name__ == '__main__':
    main()
