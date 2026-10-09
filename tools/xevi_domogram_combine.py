#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""areaNN_domogram.bin(全16)を1つの domogram_all.bin に結合(EMM per-area ロード用)。

フォーマット:
  u16[17] offset  ; offset[area](=byte位置, file先頭から)。offset[0..15]=各エリアの
                  ;   データブロック先頭、offset[16]=file 末尾(area15 の長さ算出用)。
  concatenated     ; area1..16 の areaNN_domogram.bin(ndomo,[col,row,nseg,(dir,dur)×])を連結

asm: boot で allarea_load が EMM_DOMO(=0x064000, LZTEMP域, decode後は空き)へロード。
  area_switch で offset[area]/offset[area+1] を読み、そのブロックを domo_data(RAM,256B)へコピー。
使い方: python3 -I tools/xevi_domogram_combine.py
"""
import os, struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EN = os.path.join(ROOT, "roms", "arcade", "xevious-out", "enemies")
# [team-lead 方針] 180° は enemies_rot180/ を読み書き(本番 enemies/ と分離)。
ROT180 = bool(int(os.environ.get("XEVI_ROT180", "0")))
IN_DIR = os.path.join(ROOT, "roms", "arcade", "xevious-out", "enemies_rot180") if ROT180 else EN


def main():
    os.makedirs(IN_DIR, exist_ok=True)
    blocks = []
    for a in range(1, 17):
        p = os.path.join(IN_DIR, "area%02d_domogram.bin" % a)
        blocks.append(open(p, "rb").read())
    hdr = 17 * 2  # 17 u16 offsets
    offs = []
    pos = hdr
    for b in blocks:
        offs.append(pos)
        pos += len(b)
    offs.append(pos)  # end
    out = bytearray()
    for o in offs:
        if o > 0xFFFF:
            raise SystemExit("offset overflow %d" % o)
        out += struct.pack("<H", o)
    for b in blocks:
        out += b
    # X1 world 換算済みの移動ベクトル表(asm の domo_move がそのまま加算)。ROM は毎フレーム
    #   画面上で spriteY += 2dY(横断), spriteX += 2dX(自機方向, 単位 1/32px)、地形スクロール非加算。
    #   X1 world(画面 = world − 16/f, 自機方向 = scol 減少)では wcy += 2dY, wcx += 16 − 2dX。
    raw = open(os.path.join(EN, "domogram_vector_tbl.bin"), "rb").read()
    s8 = lambda v: v - 256 if v >= 128 else v
    x1 = bytearray()
    for i in range(0, len(raw), 2):
        dy, dx = s8(raw[i]), s8(raw[i + 1])
        x1 += bytes([(2 * dy) & 0xFF, (16 - 2 * dx) & 0xFF])
    open(os.path.join(IN_DIR, "domo_vec_x1.bin"), "wb").write(x1)
    maxblk = max(len(b) for b in blocks)
    path = os.path.join(IN_DIR, "domogram_all.bin")
    open(path, "wb").write(out)
    print("domogram_all.bin: %d bytes (hdr %d + data %d), max area block %d" %
          (len(out), hdr, pos - hdr, maxblk))


if __name__ == "__main__":
    main()
