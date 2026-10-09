#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""area_domogram.txt を per-area バイナリ(areaNN_domogram.bin)に bake する。

Domogram は移動する地上砲台。各エリアで start(col,row)から経路(dir,dur)列に
沿って domogram_vector_tbl の (dY,dX)(1/32px)で移動。asm 側はこの bin を per-area
ロードし、スクロールが col に到達したら spawn する。

出力フォーマット(areaNN_domogram.bin):
  u8  ndomo
  ×ndomo:
    u8 col          ; world 列 (0-255; coarse 系)
    u8 row          ; world 行 (0-26)
    u8 nseg         ; 経路セグメント数
    ×nseg: u8 dir, u8 dur   ; dir=domogram_vector_tbl index(0-31), dur=フレーム数(1-255)

area_domogram.txt に出てこないエリア(未出現)は ndomo=0 の bin を出す(全16エリア揃える)。
使い方: python3 -I tools/xevi_domogram_bake.py
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "roms", "arcade", "xevious-out", "enemies", "area_domogram.txt")
OUT = os.path.join(ROOT, "roms", "arcade", "xevious-out", "enemies")


def parse():
    areas = {}  # area -> list of (col,row,[(dir,dur),...])
    cur = None
    for line in open(SRC):
        m = re.match(r'area(\d+):', line)
        if m:
            cur = int(m.group(1)); areas[cur] = []; continue
        m = re.search(r'start col=(\d+) row=(\d+)', line)
        if not (m and cur is not None):
            continue
        col = int(m.group(1)); row = int(m.group(2))
        segs = []
        for s in re.findall(r'dir(\d+) x(\d+)f', line):
            d = int(s[0]); dur = int(s[1])
            if d > 31:
                sys.exit("dir out of range: %d" % d)
            # dur は 1-255 に収める(255 は "長い"=そのまま)
            segs.append((d, min(255, max(1, dur))))
        areas[cur].append((col, row, segs))
    return areas


# [鏡像修正] XEVI_ROT180=1 で spawn col/row を 180°反転([[xevi-scroll-mirror]])。
#   Domogram は 2x2(DOMO_BASE)。TL' = (W1-2-col, H1-2-row)。経路 dir 列は不変
#   (スクロール軸 wcx+=16-2dX は自己整合で不変、横断軸 wcy の符号反転はエンジン側
#   =emm-scroll の担当)。flip 後は col 昇順で再ソート(domo_spawn の前方走査用)。
ROT180 = bool(int(os.environ.get("XEVI_ROT180", "0")))
W1, H1 = 256, 25


def main():
    areas = parse()
    total = 0
    for a in range(1, 17):
        doms = areas.get(a, [])
        if ROT180:
            # 2x2 TL の 180°。row は画面外(25-26→-1,-2)もあり得るので u8 マスク
            #   (エンジンは row>=24 を非表示扱い、wcy 反転で対称に画面内へ進入)。
            doms = [((W1 - 2 - col) & 0xFF, (H1 - 2 - row) & 0xFF, segs) for (col, row, segs) in doms]
        # col 昇順(= 出現順)。asm の domo_spawn は先頭ポインタだけを見て進める。
        doms = sorted(doms, key=lambda d: d[0])
        buf = bytearray([len(doms)])
        for col, row, segs in doms:
            if col > 255 or row > 255 or len(segs) > 255:
                sys.exit("field overflow area%d" % a)
            buf += bytes([col, row, len(segs)])
            for d, dur in segs:
                buf += bytes([d, dur])
        path = os.path.join(OUT, "area%02d_domogram.bin" % a)
        open(path, "wb").write(buf)
        total += len(doms)
        print("  area%02d: %2d domogram, %3d bytes" % (a, len(doms), len(buf)))
    print("total %d domogram across 16 areas" % total)


if __name__ == "__main__":
    main()
