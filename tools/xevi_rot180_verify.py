#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""地形鏡像/逆スクロール修正(案B=bake 180°回転)の整合自動チェック。

背景([[xevi-scroll-mirror]]): 現 X1 はスクロール軸を arcade と逆走査し、プレイヤー
視点が arcade 表示の 180°回転になっている(RT3 実機確定)。修正は bake を 180°回転
(col 反転 + row 反転 + 各タイル/スプライト 180°flip)する。

180°変換の規約(実装の唯一の真実):
  地形マップ:    Gnew[col'][row'] = G[W1-1-col'][H1-1-row'] かつ各タイルは fx,fy 両toggle。
  物体 TL:       幅 w 高 h の物体が TL=(c0,r0) にあるとき、TL' = (W1-w-c0, H1-h-r0)。
                 (= 元の各セルが 180°先へ写り、footprint の左右上下が入替わる)

このツールは「地形マップの反転」と「物体 TL の式」が**同一の 180°変換**になっているかを、
明示的に反転グリッドを作って footprint 照合で検証する(off-by-one や片側だけ反転した
ミスを検出 = team-lead 依頼「反転後 map 上で同じ地形セルを指すこと」)。

対象物体: gobj(地上物), Domogram spawn, fly spawn, Sol。crater は gobj に追従。
出力: 検証結果を stdout。ROM/画像は非コミット。クレジット: tcdev42/re。
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xevi_extract as X
import xevi2x1_64 as T
import xevi_bake as XB
import xevi_objtbl as O

W1, H1 = 256, 25


def classify(r, g, b):
    if b > 90 and b > r:   return 'W'
    if r > 110 and g > 80 and b < 95:  return 'R'
    if g > r and g > b and g < 110:    return 'F'
    return 'O'


def build_class_grid(ex, g2, g4, rgb, bg_pen, bs1):
    """地形クラスグリッド G[col][row](256x25)。"""
    G = [['O'] * H1 for _ in range(W1)]
    for col in range(W1):
        for row in range(H1):
            code, color, fx, fy = ex.bg_cell(g4, col, bs1[row])
            cell = XB.terrain_cell_rgb(ex, g2, rgb, bg_pen, code, color, fx, fy)
            rs = gs = bs = n = 0
            for yy in range(8):
                for xx in range(8):
                    c = cell[yy][xx]
                    if c is not None:
                        rs += c[0]; gs += c[1]; bs += c[2]; n += 1
            G[col][row] = classify(rs // n, gs // n, bs // n) if n else 'O'
    return G


def tl_rot180(c0, r0, w, h):
    return (W1 - w - c0, H1 - h - r0)


def footprint(G, c0, r0, w, h):
    """(c0,r0) から w x h の地形クラス列(row-major)。範囲外は 'x'。"""
    out = []
    for dr in range(h):
        for dc in range(w):
            col, row = c0 + dc, r0 + dr
            out.append(G[col & 0xFF][row] if 0 <= row < H1 else 'x')
    return out


def main():
    ex = X
    g1, g2, g3, g4, pr = X.build_regions(); XB.gfx3_cache = g3
    rgb, bg_pen, sp_pen, fg_pen = X.build_palette(pr)
    areatbl = T.load_area_table(); rom = O.load_subrom(); ptrs = O.area_ptrs(rom)
    col_of = lambda t: (t + 0xFD) & 0xFF
    row_of = lambda y: (y >> 3) - 2
    REMAP = 0x06AA
    FNLEN = {0:3,1:4,2:3,3:2,4:2,5:2,6:3,7:2,8:3,9:3,10:3,11:3,12:3,13:3,
             14:5,15:None,16:3,17:3,18:2,19:2,20:2,21:2,22:3,23:2}
    fn_of = lambda t: rom[REMAP + (t - 1)] if 1 <= t <= 0x80 else -1
    W2 = {0x1E: 2, 0x1F: 2, 0x26: 2, 0x20: 2, 0x2D: 2, 0x1B: 2, 0x21: 4}  # footprint 幅=高

    print("=== 180°回転 bake 整合チェック(地形反転 vs 物体TL式の一致)===")
    tot_ok = tot = 0
    for A in range(1, 17):
        off = areatbl[A - 1]
        bs1 = [(off - 1 + T.ACROSS_SKIP + k) & 0x7f for k in range(H1)]
        G = build_class_grid(ex, g2, g4, rgb, bg_pen, bs1)
        # 180°反転グリッド(タイル種別は flip 不変なので順序反転のみ)
        Gnew = [[G[W1 - 1 - c][H1 - 1 - r] for r in range(H1)] for c in range(W1)]
        start = ptrs[A - 1]; end = ptrs[A] if A < 16 else 0x1E52
        items = []
        for trig, typ, o, y in O.extract_ground_full(rom, start, end):
            w = W2.get(typ, 2)
            items.append((O.GROUND_TYPES.get(typ, '?'), col_of(trig), row_of(y), w, w))
        p = start
        while p < end - 1:
            fn = fn_of(rom[p + 1]); L = (5 + 2 * rom[p + 4]) if fn == 15 else FNLEN.get(fn)
            if not L or L < 2 or p + L > end:
                break
            if fn == 15:
                items.append(('Domo', col_of(rom[p]), row_of(rom[p + 3]), 1, 1))
            p += L
        ok = bad = 0; firstbad = []
        for (nm, c0, r0, w, h) in items:
            fp_old = footprint(G, c0, r0, w, h)             # 変換前 footprint
            c1, r1 = tl_rot180(c0, r0, w, h)
            fp_new = footprint(Gnew, c1, r1, w, h)          # 変換後 map 上の同物体 footprint
            # 期待: fp_new は fp_old を 180°(row/col 両反転=列を完全反転)したもの
            if fp_new == fp_old[::-1]:
                ok += 1
            else:
                bad += 1
                if len(firstbad) < 3:
                    firstbad.append("%s(%d,%d):%s/%s" % (nm, c0, r0, ''.join(fp_old), ''.join(fp_new)))
        tot_ok += ok; tot += ok + bad
        note = ("  NG: " + "; ".join(firstbad)) if bad else ""
        print("  area%2d: 物体 %3d  整合 %3d/%3d%s" % (A, len(items), ok, ok + bad, note))
    print("--- 整合合計: %d/%d (%.1f%%) ---" % (tot_ok, tot, 100.0 * tot_ok / max(1, tot)))
    print("100% = 地形マップ反転と物体TL式が同一 180°変換 = 反転後も物体は同じ地形上。")
    print("※向き(player view==arcade)は RT3 相互相関で確定済(orient/rt3_vs_arcade.png)。")


if __name__ == "__main__":
    main()
