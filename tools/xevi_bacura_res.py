#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Bacura 回転8コマの常駐PCGデータを生成(重複除去+rot180)。

入力: roms/arcade/xevious-out/bacura_rot0..7.bin (xevi_bacura.py 生成、各192B=16x32=8セル)。
処理: 各コマを rot180(sprite全体を180°、実機スプライトROM→X1軸は Solvalou 同様 rot180 のみ)し、
      X1 描画順(2幅×4高, r0L,r0R,r1L,r1R,r2L,r2R,r3L,r3R)に並べ替え、
      全64セル(8コマ×8)を byte 完全一致で重複除去。
出力: roms/arcade/xevious-out/
      bacura_cells.bin      : ユニークセル(各24B=B/R/G×8ライン)を ID 順に連結
      bacura_frametab.inc   : 8コマ×8セル → 実PCGコード(BACURA_RES_BASE + ID)
自機dedup で空いた 0x67.. に常駐(sprite.inc BACURA_RES_BASE)。PCG書換ゼロで回転(perf安全)。
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "roms", "arcade", "xevious-out")
BASE = 0x67       # 常駐先頭コード(自機dedupで 0x67..0x9F が空く)
CELL = 24         # 24B = 3plane(B,R,G) x 8line

def rbits(b):
    r = 0
    for i in range(8):
        if b & (1 << i):
            r |= (0x80 >> i)
    return r

def rot180(cell):
    # 180°: 各プレーンで line 逆順 + 各 line の bit 反転
    out = bytearray(CELL)
    for p in range(3):
        for ln in range(8):
            out[p * 8 + ln] = rbits(cell[p * 8 + (7 - ln)])
    return bytes(out)

def main():
    frames = []
    for i in range(8):
        d = open(os.path.join(OUT, "bacura_rot%d.bin" % i), "rb").read()
        assert len(d) == 192, "bacura_rot%d.bin は 192B であるべき" % i
        # 旧セル順: index=r*2+c (row r 0-3, col c 0-1)
        cells = [d[c * CELL:(c + 1) * CELL] for c in range(8)]
        # rot180: new[r][c] = rot180(old[3-r][1-c])
        new = [None] * 8
        for r in range(4):
            for c in range(2):
                new[r * 2 + c] = rot180(cells[(3 - r) * 2 + (1 - c)])
        frames.append(new)
    # 重複除去
    uniq, order, ftab = {}, [], []
    def idof(cell):
        if cell not in uniq:
            uniq[cell] = len(order)
            order.append(cell)
        return uniq[cell]
    for fr in frames:
        ftab.append([idof(c) for c in fr])
    n = len(order)
    assert BASE + n - 1 <= 0x9F, "空き枠(0x67..0x9F)超過: %d セル必要" % n
    open(os.path.join(OUT, "bacura_cells.bin"), "wb").write(b"".join(order))
    with open(os.path.join(OUT, "bacura_frametab.inc"), "w") as f:
        f.write("; bacura_frametab: 8コマ×8セル(draw順 r0L,r0R,r1L,r1R,r2L,r2R,r3L,r3R)→実PCGコード。\n")
        f.write("; tools/xevi_bacura_res.py 生成(rot180適用)。bacura_cells.bin を BACURA_RES_BASE に常駐。\n")
        f.write("bacura_frametab:\n")
        for ids in ftab:
            f.write("\tdb\t" + ",".join("0x%02X" % (BASE + i) for i in ids) + "\n")
    print("ユニークセル=%d (0x%02X..0x%02X) → bacura_cells.bin(%dB) + bacura_frametab.inc"
          % (n, BASE, BASE + n - 1, n * CELL))

if __name__ == "__main__":
    main()
