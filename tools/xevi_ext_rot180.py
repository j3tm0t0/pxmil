#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""外部敵スプライト(Jara/Torkan/Grobda)を ROM から rot180 規約で正規抽出。

背景([[xevi-scroll-mirror]] スプライト向き監査): 従来の roms/*_ext.inc は
- Jara: ROM tile0xA0 を rot0(=X1 縦画面で 180°ズレ)で抽出していた(dist=0 で確定)。
- Torkan/Grobda: ROM のどのタイル/向きにも一致しない hand-made(ROM 再現方針に反する)。
team-lead 方針: ROM から正規に抽出し直し、自機/aerial と同じ **rot180** 規約で生成。
元 ext.inc は上書きせず新ファイルを出力し、ROT180 ビルドから参照する。

ROM タイル(decode_sprite の index, 目視確認済):
- Jara   0xA0-0xA3(翼/回転 4コマ)      → 4コマ=384B(engine: jara_data 16セル)
- Torkan 0x10-0x11(脚付きスカウト 2コマ)→ 2コマ=192B(engine: TORKAN_BASE 2コマ)
- Grobda 0x4C-0x4D(赤い戦車 2コマ)      → 2コマ=192B(engine: GROB 2コマ)
色は暫定(比較画像で team-lead 承認後に確定)。PCG は gen96(rot180)=aerial と同形式。
出力: roms/jara_rom180.inc / torkan_rom180.inc / grobda_rom180.inc(非コミット, 不上書き)。
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xevi_extract as X
from xevi_sprites import gen96, inc_block

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (name, out, [tiles], colour)。出力名は emm-scroll 配線済の _rot180 に統一。
#   colour: Grobda=赤戦車(0x01 確定)。Jara/Torkan は比較画像で team-lead 確認(暫定)。
SPR = [
    ("Jara",   "roms/jara_rot180.inc",   [0xA0, 0xA1, 0xA2, 0xA3], 0x07),
    ("Torkan", "roms/torkan_rot180.inc", [0x10, 0x11],             0x01),
    ("Grobda", "roms/grobda_rot180.inc", [0x4C, 0x4D],             0x01),
]


def main():
    g1, g2, g3, g4, pr = X.build_regions()
    rgb, bg_pen, sp_pen, fg_pen = X.build_palette(pr)
    for name, out, tiles, colour in SPR:
        blocks = []
        for i, t in enumerate(tiles):
            d = gen96(X, g3, sp_pen, rgb, t, colour, rot=180)  # rot180 = X1 縦画面(自機/aerial と同じ)
            blocks.append("; %s f%d tile0x%03X colour0x%02X rot180\n%s"
                          % (name, i, t, colour, inc_block(d, "%s f%d " % (name, i))))
        hdr = ("; %s rot180 ROM 再抽出(xevi_ext_rot180)。元 *_ext.inc は不上書き。\n"
               "; tiles=%s colour=0x%02X %dコマ=%dB\n"
               % (name, ["0x%02X" % t for t in tiles], colour, len(tiles), len(tiles) * 96))
        open(os.path.join(ROOT, out), "w").write(hdr + "\n".join(blocks) + "\n")
        print("  %-7s -> %s (%d コマ, tiles=%s, colour=0x%02X)"
              % (name, out, len(tiles), ["0x%02X" % t for t in tiles], colour))
    print("出力: roms/{jara,torkan,grobda}_rom180.inc(ROT180 ビルドから参照)。色は比較画像で確認。")


if __name__ == "__main__":
    main()
