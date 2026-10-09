#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""スプライト PCG(96B/コマ=16x16, 2x2×3plane パレットD)を 180°回転して出力。

用途([[xevi-scroll-mirror]] スプライト向き監査): 外部提供の ext.inc の一部が
rot0(=X1 縦画面で 180°ズレ)で抽出されていた(Jara 確定: ROM tile0xA0 無回転と
dist=0)。元ファイルは上書きせず、180°反転した**新ファイル**を出し、ROT180 ビルド
から参照する(非回転ビルドは元ファイルのまま=混在防止)。

PCG 96B/コマ: 4セル(TL,TR,BL,BR)×3plane(bit1,2,4)×8byte。gen96(xevi_sprites)と同形式。
180°回転 = 16x16 slot を s[15-y][15-x] にして再エンコード(セル並びも反転される)。

使い方:
  python3 tools/xevi_sprite_flip180.py roms/jara_ext.inc roms/jara_rot180.inc [label]
  入力/出力が .inc なら db テキスト、.bin なら生バイナリ。コマ数はデータ長/96 で自動。
"""
import os, sys, re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_bytes(path):
    if path.endswith(".inc"):
        txt = open(path).read()
        return bytes(int(m.group(1), 16) for m in re.finditer(r'0x([0-9A-Fa-f]{2})', txt))
    return open(path, "rb").read()


def decode_cells(d96):
    s = [[0] * 16 for _ in range(16)]
    cells = [(0, 0), (8, 0), (0, 8), (8, 8)]
    i = 0
    for (ox, oy) in cells:
        pl = [d96[i + p * 8:i + p * 8 + 8] for p in range(3)]
        i += 24
        for yy in range(8):
            for xx in range(8):
                v = 0
                for p in range(3):
                    if pl[p][yy] & (0x80 >> xx):
                        v |= (1 << p)
                s[oy + yy][ox + xx] = v
    return s


def encode_cells(s):
    cells = [(0, 0), (8, 0), (0, 8), (8, 8)]
    d = bytearray()
    for (ox, oy) in cells:
        for pbit in (1, 2, 4):
            for yy in range(8):
                b = 0
                for xx in range(8):
                    if s[oy + yy][ox + xx] & pbit:
                        b |= (0x80 >> xx)
                d.append(b)
    return bytes(d)


def flip180_frame(d96):
    s = decode_cells(d96)
    s = [[s[15 - y][15 - x] for x in range(16)] for y in range(16)]
    return encode_cells(s)


def emit_inc(frames, label):
    out = ["; 180°反転版(xevi_sprite_flip180)。元 ext.inc は上書きせず ROT180 ビルドから参照。\n"]
    cellnm = ["TL", "TR", "BL", "BR"]
    plnm = ["B", "R", "G"]
    for fi, fr in enumerate(frames):
        out.append("; frame %d\n" % fi)
        i = 0
        for c in range(4):
            for p in range(3):
                row = ",".join("0x%02X" % b for b in fr[i:i + 8])
                out.append("\tdb\t%s\t; f%d %s %s\n" % (row, fi, cellnm[c], plnm[p]))
                i += 8
    return "".join(out)


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: xevi_sprite_flip180.py <in .inc|.bin> <out .inc|.bin> [label]")
    inp, outp = sys.argv[1], sys.argv[2]
    label = sys.argv[3] if len(sys.argv) > 3 else os.path.splitext(os.path.basename(outp))[0]
    data = read_bytes(os.path.join(ROOT, inp) if not os.path.isabs(inp) else inp)
    if len(data) % 96 != 0:
        sys.exit("データ長 %d が 96 の倍数でない(16x16 PCG コマ単位でない)" % len(data))
    frames = [flip180_frame(data[i:i + 96]) for i in range(0, len(data), 96)]
    outpath = os.path.join(ROOT, outp) if not os.path.isabs(outp) else outp
    if outp.endswith(".inc"):
        open(outpath, "w").write(emit_inc(frames, label))
    else:
        open(outpath, "wb").write(b"".join(frames))
    print("flip180: %s (%d コマ) -> %s" % (inp, len(frames), outp))


if __name__ == "__main__":
    main()
