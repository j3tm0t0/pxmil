#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Xevious アーケード ROM の「エリアオブジェクト表」読み出しツール。

表構造・エントリ形式は tcdev42/re の注釈付き逆アセンブルを参照して特定した
(クレジット: Mark McDougall(tcdev) / Jean-Francois Fabre(jotd),
 https://github.com/tcdev42/re )。逆アセンブルのソースは本リポジトリに取り込まず、
文書化された「形式」のみを用いて我々の ROM からデータを読む。

ROM(非コミット, roms/arcade/xevious/):
  SUB CPU ROM = xvi_5.3f(0x0000-0x0FFF) + xvi_6.3j(0x1000-0x1FFF)
  area_object_tbl_tbl @ 0x1000 : 16 エリア分のポインタ(2byte LE)
  各エリアのリスト: 可変長エントリ列。
    entry[0] = scroll トリガ(along-track 位置。エリア先頭 0xFF 付近 -> 末尾へ減少)
    entry[1] = type(loc_6AA[type] で handler 分岐)
    地上物(handler fn_1)= 4 byte: [trig, type, offset(RAM slot, 偶数 0x04..0x1F), spriteY(across)]
  地上物 type:
    0x1D Sol Citadel/Tower, 0x1E Barra(pyramid), 0x1F Zolbak(dome),
    0x20 (variant), 0x21 Garu Derota, 0x25 (variant), 0x26 Logram(sphere),
    0x2C Grobda(stationary), 0x2D Boza Logram, 0x38 Grobda(stops), 0x3A Grobda(darts)
"""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROMDIR = os.path.join(ROOT, "roms", "arcade", "xevious")

GROUND_TYPES = {
    0x1B: "Derota",
    0x1D: "Sol",       0x1E: "Barra",      0x1F: "Zolbak",
    0x20: "GndObj20",  0x21: "GaruDerota", 0x25: "GndObj25",
    0x26: "Logram",    0x2C: "Grobda(stat)", 0x2D: "BozaLogram",
    0x38: "Grobda(stop)", 0x3A: "Grobda(dart)",
}

# SUB area stream のコマンド→fn remap(loc_6AA, type-1 で index)と各 fn の entry 長。
# fn_15 domogram のみ可変(5+2*num)。extract_ground の 1 グループ制約を解消する正規版。
_REMAP = 0x06AA
_FNLEN = {0:3,1:4,2:3,3:2,4:2,5:2,6:3,7:2,8:3,9:3,10:3,11:3,12:3,13:3,
          14:5,15:None,16:3,17:3,18:2,19:2,20:2,21:2,22:3,23:2}


def extract_ground_full(rom, start, end):
    """SUB area command stream を正規に walk し、**全ての** fn_1 地上物エントリ
    (trig, type, off, spriteY)を返す。extract_ground(ヒューリスティック, 先頭1グループ
    ≤14件のみ)の上位互換=既存位置を保持しつつ見逃し分を補完する superset。"""
    def fn_of(t):
        return rom[_REMAP + (t - 1)] if 1 <= t <= 0x80 else -1
    objs = []
    p = start
    while p < end - 1:
        fn = fn_of(rom[p + 1])
        L = (5 + 2 * rom[p + 4]) if fn == 15 else _FNLEN.get(fn)
        if not L or L < 2 or p + L > end:
            break
        if fn == 1 and rom[p + 1] in GROUND_TYPES:
            objs.append((rom[p], rom[p + 1], rom[p + 2], rom[p + 3]))
        p += L
    return objs

def load_subrom():
    lo = open(os.path.join(ROMDIR, "xvi_5.3f"), "rb").read()
    hi = open(os.path.join(ROMDIR, "xvi_6.3j"), "rb").read()
    return lo + hi   # 0x0000-0x1FFF

def area_ptrs(rom):
    base = 0x1000
    return [rom[base + i*2] | (rom[base + i*2 + 1] << 8) for i in range(16)]

def extract_ground(rom, start, end):
    """[start,end) を走査し、offset が 0x04,0x06,...,0x1E と厳密増加する
    4byte 地上物エントリを順に拾う(堅牢抽出)。"""
    objs = []
    expected = 0x04
    i = start
    while i + 3 < end and expected <= 0x1E:
        trig, typ, off, y = rom[i], rom[i+1], rom[i+2], rom[i+3]
        if typ in GROUND_TYPES and off == expected:
            objs.append((trig, typ, off, y))
            expected += 2
            i += 4
        else:
            i += 1
    return objs

def main():
    rom = load_subrom()
    ptrs = area_ptrs(rom)
    area = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    idx = area - 1
    start = ptrs[idx]
    end = ptrs[idx+1] if idx+1 < 16 else 0x2000
    print("area %d: list @ 0x%04X .. 0x%04X (%d bytes)" % (area, start, end, end-start))
    print("area pointers:", [hex(p) for p in ptrs])
    objs = extract_ground(rom, start, end)
    print("地上物 %d 個:" % len(objs))
    print(" idx  trig  type            offset spriteY")
    for n,(trig,typ,off,y) in enumerate(objs):
        print("  %2d  0x%02X  0x%02X %-13s 0x%02X   0x%02X" %
              (n, trig, typ, GROUND_TYPES[typ], off, y))

if __name__ == "__main__":
    main()
