#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""敵スプライト(敵弾/Zoshi/Bacura)を 16x16 PCG 96B パレットD で抽出。

code->tile は敵ごとに直接/拡張バンク((code&0x3f)+0x100)が異なる(描画で確認):
  敵弾(スパリオ/Brag Spario) = tile 0x115(丸弾), colour 0x26
  Zoshi(棘付き球, countup_timer+0x28 アニメ) = tile 0x28-0x2A(直接), colour=pulsing
  Bacura(破壊不可の回転板) = tile 0x120(エッジ)/0x12B(平板)(拡張バンク), 2フレーム回転
出力: roms/arcade/xevious-out/ (非コミット)。色は代表値(地上物同様、実機照合で精緻化可)。
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xevi_extract as X
from xevi_sprites import gen96, inc_block
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "roms", "arcade", "xevious-out")

# (name, tile, colour)
ENEMIES = [
    ("ebullet",     0x115, 0x26),   # 敵弾(丸弾)
    ("zoshi_0",     0x28,  0x0F),   # Zoshi アニメ
    ("zoshi_1",     0x29,  0x0F),
    ("zoshi_2",     0x2A,  0x0F),
    ("bacura_edge", 0x120, 0x07),   # Bacura エッジ(回転 edge-on)
    ("bacura_flat", 0x12B, 0x07),   # Bacura 平板
]

def main():
    g1, g2, g3, g4, pr = X.build_regions()
    rgb, bg_pen, sp_pen, fg_pen = X.build_palette(pr)
    os.makedirs(OUT, exist_ok=True)
    inc = []
    prev = Image.new("RGB", (len(ENEMIES) * 16 * 6, 16 * 6), (255, 0, 255))
    ppx = prev.load()
    for i, (name, tile, colour) in enumerate(ENEMIES):
        d = gen96(X, g3, sp_pen, rgb, tile, colour)
        open(os.path.join(OUT, "enemy_%s.bin" % name), "wb").write(d)
        inc.append(inc_block(d, "enemy_%s" % name))
        # preview(decode して描画)
        px = X.decode_sprite(g3, tile)
        for y in range(16):
            for x in range(16):
                pen = sp_pen[colour * 8 + px[y][x]]
                if pen == 0x80:
                    continue
                c = rgb[pen]
                for dy in range(6):
                    for dx in range(6):
                        ppx[(i * 16 + x) * 6 + dx, y * 6 + dy] = tuple(c)
        print("  %-12s tile=0x%03X colour=0x%02X -> enemy_%s.bin(96B)" % (name, tile, colour, name))
    with open(os.path.join(OUT, "enemy_sprites.inc"), "w") as f:
        f.write("; 敵スプライト(敵弾/Zoshi/Bacura) 96B PCG パレットD。非コミット。\n")
        f.write("\n".join(inc))
    prev.save(os.path.join(OUT, "enemy_sprites_preview.png"))
    print("出力: roms/arcade/xevious-out/enemy_*.bin, enemy_sprites.inc, enemy_sprites_preview.png")

if __name__ == "__main__":
    main()
