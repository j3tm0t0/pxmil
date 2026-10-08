#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Xevious アンドアジェネシス(Andor Genesis 母艦)のパーツ構造・出現・タイル抽出。

アーケード主 CPU の handle_41..0x52_Andor_Genesis_obj_* と SUB fn_20
(andor_genesis_start)解析(tcdev42/re 参照、ソース非取り込み)。

確定構造(全パーツは アンカー obj$0F の spriteX/Y からの相対 (dX,dY) /8セル 配置。
色=andor_genesis_colour@0x8040):
  obj$01-$09 = hull 9ブロック, 各 **2x2(32x32)** 右列/中列/左列×上/中/下の 3x3 配置。
    code 0x9C,0x98,0x94,0x90,0x8C,0x88,0x84,0x80,0x58(各ブロックは code..code+3 の
    4タイル=16x16×4)。
  obj$0A-$0D = gun port 4基(内側四隅, ~16x16)。code 0x5F,0x5E,0x5D,0x5C。
    **射撃**(ffreq_mask_andor_genesis で自機狙い弾, core 破壊で停止)。
  obj$0E = CORE(中央, code 0x10)。**破壊=ボムで core を撃つ**と andor_genesis_core_hit
    → core は不可壊の Bragza 化、gun port 停止、母艦解体。hull は撃つと得点。
  obj$0F = アンカー/得点ダミー(spriteX=0xF8/8, spriteY=0x0E/8 初期)。
出現(SUB fn_20, col=(trig+0xFD)&0xFF): area4 col112 / area9 col128 / area14 col224,100。

X1 は大きいので PCG でなく GRAM 描画想定(地上物と同扱い)。本ツールは各パーツの
tile を パレットD GRAM セル(16x16=4セル×24B)で出力し、パーツ配置表を添える。
出力(非コミット, roms/arcade/xevious-out/andor/):
  andor_parts.txt   : パーツ表(obj,code,dX,dY,size,role,fire)+出現エリア
  andor_parts.bin   : 15×[obj,code,dX(s8),dY(s8),size,fire]
  andor_tiles.inc   : 使用タイル(hull/gunport/core)の 16x16 パレットD db
クレジット: tcdev42/re (tcdev/jotd)。ROM/出力は非コミット。
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xevi_extract as X
from xevi_sprites import gen96, inc_block

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "roms", "arcade", "xevious-out", "andor")

# obj idx -> (code, dX, dY, size2x2?, role, fires) : ROM 抽出値
PARTS = [
    (0x01, 0x9C,  3, -3, True,  "hull", False),
    (0x02, 0x98,  3,  0, True,  "hull", False),
    (0x03, 0x94,  3,  5, True,  "hull", False),
    (0x04, 0x90,  0, -3, True,  "hull", False),
    (0x05, 0x8C,  0,  0, True,  "hull", False),
    (0x06, 0x88,  0,  5, True,  "hull", False),
    (0x07, 0x84, -5, -3, True,  "hull", False),
    (0x08, 0x80, -5,  0, True,  "hull", False),
    (0x09, 0x58, -5,  5, True,  "hull", False),
    (0x0A, 0x5F,  2, -2, False, "gunport", True),
    (0x0B, 0x5E,  2,  2, False, "gunport", True),
    (0x0C, 0x5D, -2, -2, False, "gunport", True),
    (0x0D, 0x5C, -2,  2, False, "gunport", True),
    (0x0E, 0x10,  0,  0, False, "CORE", False),
    (0x0F, 0x00,  0,  0, False, "anchor/points", False),
]
APPEAR = [(4, 112), (9, 128), (14, 224), (14, 100)]


def main():
    g1, g2, g3, g4, pr = X.build_regions()
    rgb, bg, sp, fg = X.build_palette(pr)
    os.makedirs(OUT, exist_ok=True)

    lines = ["Andor Genesis parts (anchor obj$0F 相対, dX/dY=/8セル):",
             "obj  code   dX  dY  size   role           fire"]
    for obj, code, dx, dy, big, role, fire in PARTS:
        lines.append(" $%02X  0x%02X  %+3d %+3d  %-4s  %-13s %s" %
                     (obj, code, dx, dy, "2x2" if big else "1x1", role,
                      "YES" if fire else "-"))
    lines.append("")
    lines.append("出現(col=(trig+0xFD)&0xFF): " +
                 ", ".join("area%d col%d" % (a, c) for a, c in APPEAR))
    lines.append("CORE 破壊でクリア(→Bragza不可壊化, gun port停止)。hull は得点。")
    open(os.path.join(OUT, "andor_parts.txt"), "w").write("\n".join(lines) + "\n")

    with open(os.path.join(OUT, "andor_parts.bin"), "wb") as f:
        for obj, code, dx, dy, big, role, fire in PARTS:
            f.write(bytes([obj, code, dx & 0xFF, dy & 0xFF,
                           3 if big else 0, 1 if fire else 0]))

    # タイル: hull は 2x2 (code..code+3), gunport/core は 16x16(code)
    inc = []
    done = set()
    for obj, code, dx, dy, big, role, fire in PARTS:
        if role in ("anchor/points",):
            continue
        tiles = [code, code + 1, code + 2, code + 3] if big else [code]
        for t in tiles:
            if t in done:
                continue
            done.add(t)
            d = gen96(X, g3, sp, rgb, t, 7, rot=180)   # 代表色7, rot180
            open(os.path.join(OUT, "andor_tile_%02X.bin" % t), "wb").write(d)
            inc.append("; tile 0x%02X (%s obj$%02X)\n%s" %
                       (t, role, obj, inc_block(d, "andor%02X " % t)))
    with open(os.path.join(OUT, "andor_tiles.inc"), "w") as f:
        f.write("; Andor Genesis パーツtile 16x16 パレットD rot180. 非コミット.\n")
        f.write("\n".join(inc) + "\n")

    print("\n".join(lines))
    print("\n出力: roms/arcade/xevious-out/andor/ (andor_parts.txt/.bin, andor_tiles.inc, andor_tile_*.bin %d枚)" % len(done))


if __name__ == "__main__":
    main()
