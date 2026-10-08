#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Xevious バキュラ(Bacura)回転アニメの ROM 確定抽出 + X1 用セル化。

アーケード主 CPU の handle_01_Bacura(@0x2B23)と bacura_sprite_tbl(@0x2B5D)を
解析して得た確定仕様(tcdev42/re の注釈付き逆アセンブルで関数位置のみ参照、
ソース非取り込み)。

確定事項(ROM で確認):
  - スプライトは 16幅 x 32高(1x2 = double-height, 属性=0x82)。MAME draw_sprites:
    上セル code と 下セル code+2 を縦に積む。
  - 回転は 8コマ・フルサイクル(ping-pong でない)。(colour, code):
      idx0 (2B,20) idx1 (2C,21) idx2 (2B,24) idx3 (2C,25)
      idx4 (2B,28) idx5 (2C,29) idx6 (2B,2C) idx7 (2C,2D)  実タイル=code+0x100
  - コマ index = (travel_pos>>2)&7。move_object_dX(dX=16)で pos +1/frame →
    4フレーム毎に 1コマ・8コマ一周 = 32フレーム(位置依存=タイマ不要)。
  - 色は金属グレー2色交互: 0x2B(pen1=AE,2=62,3=2D) / 0x2C(pen1=8F,2=43,3=00)。
  - 8コマは pen パターン自体が全て異なる(反転/180°/色違いでも一致ペア無し)。

X1 向け(emm-scroll 仕様, team-lead が全8コマ常駐を選択):
  - 向き: アーケード 16(幅,ハードX=スクロール軸) x 32(高,ハードY) を、Solvalou と
    同じ **rot180** だけ適用(90°不要=スプライト ROM は既に X1 軸)。X1 上は
    16幅(横=スクロール方向) x 32高(縦)。2セル幅 x 4セル高 = 8セル/コマ。
  - **rot180 は 16x32 合成後に 1 回**適用し、その後セル分割する(セル個別回転でない)。
  - 色焼き込み(パレットD slot: index=G*4+R*2+B, プレーン順 B,R,G)。
  - 全8コマ×8セル=64 のうち **43 がユニーク**(21 がバイト重複)。

出力(非コミット, roms/arcade/xevious-out/):
  bacura_rot0..7.bin : 各コマ 16x32(rot180適用)を PCG パレットD 192B(8セル)
  bacura_cells.bin   : 43 ユニークセル(各24B=B/R/G×8ライン)を ID 順に連結
  bacura_ref.bin     : 64B。コマ major(idx0..7)× セル8個の順に、使うユニーク
                       セル ID(0..42)。セル順 = 上半[TL,TR,BL,BR]→下半[TL,TR,BL,BR]
                       (X1 で縦32×横16: 上半=縦0-15, 下半=縦16-31; 各 TL=横0-7縦上,
                        TR=横8-15縦上, BL=横0-7縦下, BR=横8-15縦下)。
  bacura_ref.inc     : 同参照表の db(1コマ8バイト×8行)。
クレジット: tcdev42/re (tcdev/jotd)。ROM/出力は非コミット。
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xevi_extract as X
from xevi_sprites import slot_of

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "roms", "arcade", "xevious-out")

# bacura_sprite_tbl @ 0x2B5D : (colour, code_lo). 上セル=code_lo+0x100, 下セル=+2
BAC_TBL = [(0x2B, 0x20), (0x2C, 0x21), (0x2B, 0x24), (0x2C, 0x25),
           (0x2B, 0x28), (0x2C, 0x29), (0x2B, 0x2C), (0x2C, 0x2D)]
BAC_STEP_FRAMES = 4
BAC_CYCLE_FRAMES = 32
# セル分割順(8x8 の左上原点, (ox,oy)): 上半 TL,TR,BL,BR → 下半 TL,TR,BL,BR
CELL_OFFSETS = [(0, 0), (8, 0), (0, 8), (8, 8), (0, 16), (8, 16), (0, 24), (8, 24)]


def frame_slots(g3, sp, rgb, colour, lo, rot180=True):
    """16x32 の slot(0..7, 0=透明)グリッドを返す。rot180 を合成後に1回適用。"""
    top = X.decode_sprite(g3, 0x100 + lo)
    bot = X.decode_sprite(g3, 0x100 + lo + 2)
    s = [[0] * 16 for _ in range(32)]
    for yy in range(16):
        for xx in range(16):
            for yoff, grid in ((0, top), (16, bot)):
                pen = sp[colour * 8 + grid[yy][xx]]
                if pen != 0x80:
                    s[yoff + yy][xx] = slot_of(*rgb[pen])
    if rot180:
        s = [[s[31 - y][15 - x] for x in range(16)] for y in range(32)]
    return s


def cell_bytes(s, ox, oy):
    """8x8 slot 領域 -> 24B(プレーン B,R,G 各8ライン, bit=0x80>>x)。"""
    d = bytearray()
    for pbit in (1, 2, 4):              # B, R, G
        for yy in range(8):
            byte = 0
            for xx in range(8):
                if s[oy + yy][ox + xx] & pbit:
                    byte |= (0x80 >> xx)
            d.append(byte)
    return bytes(d)


def main():
    g1, g2, g3, g4, pr = X.build_regions()
    rgb, bg_pen, sp_pen, fg_pen = X.build_palette(pr)
    os.makedirs(OUT, exist_ok=True)

    uniq = []            # ID順ユニークセル(24B)
    uindex = {}          # bytes -> ID
    ref = []             # 64 エントリ(コマ×セル)の ID

    for idx, (colour, lo) in enumerate(BAC_TBL):
        s = frame_slots(g3, sp_pen, rgb, colour, lo, rot180=True)
        frame = bytearray()
        for (ox, oy) in CELL_OFFSETS:
            cb = cell_bytes(s, ox, oy)
            frame += cb
            cid = uindex.get(cb)
            if cid is None:
                cid = len(uniq); uindex[cb] = cid; uniq.append(cb)
            ref.append(cid)
        open(os.path.join(OUT, "bacura_rot%d.bin" % idx), "wb").write(frame)

    with open(os.path.join(OUT, "bacura_cells.bin"), "wb") as f:
        for cb in uniq:
            f.write(cb)
    with open(os.path.join(OUT, "bacura_ref.bin"), "wb") as f:
        f.write(bytes(ref))
    with open(os.path.join(OUT, "bacura_ref.inc"), "w") as f:
        f.write("; Bacura 参照表: 8コマ×8セル=64, 値=ユニークセルID(0..%d)\n" % (len(uniq) - 1))
        f.write("; セル順/コマ: 上半TL,TR,BL,BR→下半TL,TR,BL,BR (X1縦32×横16, rot180済)\n")
        f.write("; 回転: %dフレーム毎に次コマ, 8コマ一周. 色=金属グレー(0x2B/0x2C交互).\n"
                % BAC_STEP_FRAMES)
        for idx in range(8):
            row = ref[idx * 8:idx * 8 + 8]
            f.write("\tdb\t%s\t; frame%d\n" % (",".join("%d" % v for v in row), idx))

    print("unique cells: %d (of 64)" % len(uniq))
    print("ref table (frame x 8 cells):")
    for idx in range(8):
        print("  frame%d: %s" % (idx, ref[idx * 8:idx * 8 + 8]))
    print("出力: bacura_cells.bin(%dB=%dセル×24), bacura_ref.bin(64B), bacura_ref.inc, "
          "bacura_rot0..7.bin" % (len(uniq) * 24, len(uniq)))
    print("回転: step=%df, cycle=%df(8コマ)。rot180適用済・金属グレー2色交互。"
          % (BAC_STEP_FRAMES, BAC_CYCLE_FRAMES))


if __name__ == "__main__":
    main()
