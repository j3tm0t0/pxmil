#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""未実装の空中敵(Kapi/Terrazi/Zakato系/Garu Zakato/Giddo)の PCG 抽出。

アーケード主 CPU の各 handler で確定した sprite code(bank0=直接 tile, bank1=
+0x100)を 16x16 PCG 96B パレットD(rot180 規約 = X1 縦画面)で出力。
(tcdev42/re 参照、ソース非取り込み。挙動は xevi-enemy.md 参照。)

sprite tile(確定):
  Kapi(type0x10)      = tile 0x20  (自機狙い1.0px/f, 射撃, 300pts)
  Terrazi(type0x11)   = tile 0x01  (自機狙い1.5px/f, 射撃, 700pts)
  Zakato(0x12-0x15)   = tile 0x11  (ワープ出現→自機狙い, 射撃, 100-300pts)
  BragZakato(0x16/17) = tile 0x12  (ワープ→Brag Spario加速ホーミング弾, 600/1500)
  GaruZakato(0x18)    = tile 0x13  (時間で破裂=拡散弾 type7 1.5px/f, 1000pts)
  Giddo Spario(0x08)  = tile 0x100-0x103 (bank1, 4コマ, 自機狙い2.0px/f, 10pts)
  warp sparkle        = tile 0x0C/0x83 (zakato_teleport_sprite_tbl)
出力(非コミット, roms/arcade/xevious-out/aerial/):
  <name>.bin / aerial_sprites.inc
注: 色は pulsing(点滅)。代表色 colour7(既存 jara/torkan と同)で焼込。Giddo は 0x26。
    向きは team-lead 指示の rot180。既存 jara_ext.inc は rot=0 だったので、実機で
    どちらが正かは emm-scroll 側で確認を(ROT で切替可)。
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xevi_extract as X
from xevi_sprites import gen96, inc_block

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "roms", "arcade", "xevious-out", "aerial")

# (name, tile, colour, rot)
SPR = [
    ("kapi",        0x20, 7, 180),
    ("terrazi",     0x01, 7, 180),
    ("zakato",      0x11, 7, 180),
    ("bragzakato",  0x12, 7, 180),
    ("garuzakato",  0x13, 7, 180),
    ("warp_spark0", 0x0C, 0x24, 180),
    ("warp_spark1", 0x83, 0x24, 180),
]
GIDDO = [0x100, 0x101, 0x102, 0x103]


def main():
    g1, g2, g3, g4, pr = X.build_regions()
    rgb, bg, sp, fg = X.build_palette(pr)
    os.makedirs(OUT, exist_ok=True)
    inc = []
    for name, tile, colour, rot in SPR:
        d = gen96(X, g3, sp, rgb, tile, colour, rot=rot)
        open(os.path.join(OUT, "%s.bin" % name), "wb").write(d)
        inc.append("; %s tile0x%03X colour0x%02X rot%d\n%s" %
                   (name, tile, colour, rot, inc_block(d, name + " ")))
        print("  %-12s tile0x%03X -> %s.bin" % (name, tile, name))
    for i, t in enumerate(GIDDO):
        d = gen96(X, g3, sp, rgb, t, 0x26, rot=180)
        open(os.path.join(OUT, "giddo_f%d.bin" % i), "wb").write(d)
        inc.append("; giddo f%d tile0x%03X\n%s" % (i, t, inc_block(d, "giddo%d " % i)))
        print("  giddo_f%d     tile0x%03X -> giddo_f%d.bin" % (i, t, i))
    with open(os.path.join(OUT, "aerial_sprites.inc"), "w") as f:
        f.write("; 未実装空中敵 PCG 96B パレットD rot180. 非コミット.\n")
        f.write("\n".join(inc) + "\n")
    print("出力: roms/arcade/xevious-out/aerial/ (*.bin, aerial_sprites.inc)")


if __name__ == "__main__":
    main()
