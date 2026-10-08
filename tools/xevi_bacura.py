#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Xevious バキュラ(Bacura)回転アニメの ROM 確定抽出。

アーケード主 CPU の handle_01_Bacura(@0x2B23)と bacura_sprite_tbl(@0x2B5D)を
解析して得た確定仕様(tcdev42/re の注釈付き逆アセンブルで関数位置のみ参照、
ソース非取り込み)。

確定事項(ROM で確認):
  - スプライトは 16x16 ではなく **16幅 x 32高(1x2 = double-height)**。
    属性バイト=0x82 (bank=1, size=1x2)。MAME draw_sprites: code(上セル) と
    code+2(下セル)を縦に積む。
  - 回転は **8コマ・フルサイクル(ping-pong ではない)**。コマ順(colour, code):
      idx0 (0x2B,0x20) edge  w4    idx4 (0x2B,0x28) flat w16
      idx1 (0x2C,0x21)       w8    idx5 (0x2C,0x29)      w14  (裏面)
      idx2 (0x2B,0x24)       w12   idx6 (0x2B,0x2C)      w12  (裏面)
      idx3 (0x2C,0x25)       w14   idx7 (0x2C,0x2D)      w8   (裏面)
    実タイル = code+0x100(拡張バンク)。上記 code は各コマの上セル、下セル=code+2。
  - コマ index = (travel_pos >> 2) & 7。travel_pos は move_object_dX(dX=16)で
    **毎フレーム +1**(HL += 2*dX = +32 を下位バイトへ=pos+1)。よって
    **4フレームごとに 1コマ進み、8コマ一周 = 32フレーム(≈0.53秒@60Hz)**。
    位置依存なので「スクロール/移動で回る」= タイマ不要。
  - 色は **金属グレー(シルバー)2色の交互**(前任者の 0x07=青 は誤り):
      colour 0x2B: pen1=0xAE pen2=0x62 pen3=0x2D (明→暗グレー)
      colour 0x2C: pen1=0x8F pen2=0x43 pen3=0x00
  - 破壊不可(ショット貫通)・接触死は既知。dX=16 で自走スクロール。

出力(非コミット, roms/arcade/xevious-out/):
  bacura_rotN.bin (N=0..7) : 各コマ 16x32 を PCG パレットD 192B(8セル)
    セル順 = 上半 TL,TR,BL,BR, 下半 TL,TR,BL,BR(各 B/R/G プレーン x8 = 24B)。
  bacura_rot.inc           : 全8コマの db ブロック。
注: X1 は画面 90°回転。バキュラの長軸(32=native縦=スクロール方向)は X1 の
    横(スクロール方向)に対応。実装側の向き決定は emm-scroll に委ねる(本ツールは
    native 16x32 を出力)。
クレジット: tcdev42/re (tcdev/jotd)。ROM/出力は非コミット。
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xevi_extract as X
from xevi_sprites import gen96, inc_block

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "roms", "arcade", "xevious-out")

# bacura_sprite_tbl @ 0x2B5D : (colour, code_lo). 実タイル上セル = code_lo+0x100, 下セル=+2
BAC_TBL = [(0x2B, 0x20), (0x2C, 0x21), (0x2B, 0x24), (0x2C, 0x25),
           (0x2B, 0x28), (0x2C, 0x29), (0x2B, 0x2C), (0x2C, 0x2D)]
BAC_STEP_FRAMES = 4          # index = (pos>>2)&7, pos +1/frame
BAC_CYCLE_FRAMES = 32        # 8 コマ x 4 フレーム


def main():
    g1, g2, g3, g4, pr = X.build_regions()
    rgb, bg_pen, sp_pen, fg_pen = X.build_palette(pr)
    os.makedirs(OUT, exist_ok=True)
    inc = []
    for idx, (colour, lo) in enumerate(BAC_TBL):
        top = 0x100 + lo          # 上セル
        bot = 0x100 + lo + 2      # 下セル(縦ミラー相当)
        d_top = gen96(X, g3, sp_pen, rgb, top, colour)   # 16x16 -> 96B(4セル)
        d_bot = gen96(X, g3, sp_pen, rgb, bot, colour)
        frame = d_top + d_bot                            # 16x32 -> 192B(8セル)
        open(os.path.join(OUT, "bacura_rot%d.bin" % idx), "wb").write(frame)
        inc.append("; idx%d colour=0x%02X code=0x%02X (tiles 0x%03X/0x%03X)\n%s\n%s" %
                   (idx, colour, lo, top, bot,
                    inc_block(d_top, "bac%d_T " % idx),
                    inc_block(d_bot, "bac%d_B " % idx)))
        print("  idx%d colour=0x%02X code=0x%02X -> bacura_rot%d.bin (192B/16x32)" %
              (idx, colour, lo, idx))
    with open(os.path.join(OUT, "bacura_rot.inc"), "w") as f:
        f.write("; Bacura 回転 8コマ 16x32 PCG パレットD(192B/コマ). 非コミット.\n")
        f.write("; コマ順 = フルサイクル. step=%d フレーム, 一周=%d フレーム.\n" %
                (BAC_STEP_FRAMES, BAC_CYCLE_FRAMES))
        f.write("\n".join(inc) + "\n")
    print("rotate: step=%df, cycle=%df(8コマ). 色=金属グレー(0x2B/0x2C交互)。" %
          (BAC_STEP_FRAMES, BAC_CYCLE_FRAMES))
    print("出力: roms/arcade/xevious-out/bacura_rot0..7.bin, bacura_rot.inc")


if __name__ == "__main__":
    main()
