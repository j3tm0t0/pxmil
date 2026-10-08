#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Xevious Domogram(移動ドーム)の各エリア経路を SUB area stream から抽出。

Domogram(type0x2E, handle_2E_Domogram@0x2ED6)は経路追従で移動する地上物。
経路は SUB の fn_15 コマンド(area stream)が与える (duration, dir_index) の列。
dir_index → domogram_vector_tbl(@MAIN0x2FB1, 32エントリ)で (dY,dX)(符号付 1/32px)。
各セグメントを duration フレーム その速度で移動し、次セグメントへ。

fn_15 entry 形式(SUB): [trig, type, slot, spriteY, num, (dur,dir)×num]。
  開始位置: col=(trig+0xFD)&0xFF(xevi_allareas と共通), row=(spriteY>>3)-2。
  射撃あり(ffreq_mask_domogram, 自機狙い弾)。

出力(非コミット, roms/arcade/xevious-out/enemies/):
  domogram_vector_tbl.txt / .bin : 32×(dY,dX)
  area_domogram.txt              : 全エリアの Domogram 開始位置(col,row)+経路列
クレジット: tcdev42/re (tcdev/jotd)。ROM/出力は非コミット。
"""
import os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROMDIR = os.path.join(ROOT, "roms", "arcade", "xevious")
OUT = os.path.join(ROOT, "roms", "arcade", "xevious-out", "enemies")


def rd(n):
    return open(os.path.join(ROMDIR, n), "rb").read()


def s8(b):
    return b - 256 if b >= 128 else b


REMAP = 0x06AA
FNLEN = {0:3,1:4,2:3,3:2,4:2,5:2,6:3,7:2,8:3,9:3,10:3,11:3,12:3,13:3,
         14:5,15:None,16:3,17:3,18:2,19:2,20:2,21:2,22:3,23:2}


def main():
    sub = rd("xvi_5.3f") + rd("xvi_6.3j")
    main_rom = rd("xvi_1.3p") + rd("xvi_2.3m") + rd("xvi_3.2m") + rd("xvi_4.2l")
    os.makedirs(OUT, exist_ok=True)
    VT = 0x2FB1

    vlines = ["domogram_vector_tbl @0x2FB1 (dir_idx -> dY,dX 符号付 1/32px):"]
    with open(os.path.join(OUT, "domogram_vector_tbl.bin"), "wb") as f:
        f.write(main_rom[VT:VT + 64])
    for i in range(32):
        vlines.append("  [%2d] dY=%+4d dX=%+4d" % (i, s8(main_rom[VT+i*2]), s8(main_rom[VT+i*2+1])))
    open(os.path.join(OUT, "domogram_vector_tbl.txt"), "w").write("\n".join(vlines) + "\n")

    def fn_of(t):
        return sub[REMAP + (t-1)] if 1 <= t <= 0x80 else -1

    def elen(p):
        fn = fn_of(sub[p+1])
        return fn, (5 + 2*sub[p+4]) if fn == 15 else FNLEN.get(fn)

    ptrs = [sub[0x1000+i*2] | (sub[0x1000+i*2+1] << 8) for i in range(16)]
    col = lambda t: (t + 0xFD) & 0xFF
    row = lambda y: (y >> 3) - 2

    lines = ["Domogram per-area paths (col=(trig+0xFD)&0xFF, row=(spriteY>>3)-2):"]
    for a in range(16):
        start = ptrs[a]; end = ptrs[a+1] if a+1 < 16 else 0x1E52
        p = start; hdr = False
        while p < end - 1:
            fn, L = elen(p)
            if not L or L < 2 or p + L > end:
                break
            if fn == 15:
                if not hdr:
                    lines.append("area%d:" % (a+1)); hdr = True
                trig = sub[p]; sy = sub[p+3]; num = sub[p+4]
                segs = ["dir%d x%df" % (sub[p+5+2*k+1], sub[p+5+2*k]) for k in range(num)]
                lines.append("  start col=%d row=%d (trig0x%02X spriteY0x%02X): %s" %
                             (col(trig), row(sy), trig, sy, " -> ".join(segs)))
            p += L
    txt = "\n".join(lines)
    open(os.path.join(OUT, "area_domogram.txt"), "w").write(txt + "\n")
    print("\n".join(vlines[:17]))
    print("...")
    print(txt[:1200])
    print("\n出力: roms/arcade/xevious-out/enemies/ (domogram_vector_tbl.txt/.bin, area_domogram.txt)")


if __name__ == "__main__":
    main()
