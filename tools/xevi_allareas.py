#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Xevious 全16エリアのタイルデータを「共通表 + エリア別ローカルID」方式で一括生成。

方式(team-lead 決定, B案ベース):
  - EMM 常駐: 共通64色パレット(全エリア union, 実測29色)、共通タイル表(全エリアの
    地形+地上物焼込+crater+Sol を dedup)、16エリア分のタイルマップ・地上物リスト。
  - エリア開始時: そのエリアの used タイルを共通表から RAM のタイル表領域へ DMA。
    タイルマップは「ローカル ID = RAM アドレス(TB + local_idx*48)」で持つ。
出力(非コミット, roms/arcade/xevious-out/allareas/):
  common_tiles.bin  : 共通タイル表(48B/タイル)
  common_pal.bin    : 共通パレット(xpal64 形式, 64エントリ×5B=320B)
  areaNN_used.bin    : 先頭2B=タイル数 n、続いて local_idx 順の「共通表 index」
                       一覧(2B×n)。engine は common[used[i]] を RAM[TB+i*48] へ集約コピー。
  areaNN_map.bin     : ローカルIDタイルマップ(256列×25行×2B = RAM アドレス TB+local_idx*48)
  areaNN_gobj.bin    : 地上物リスト。gobj_count(1B)+ 1件[col:2B,row:1B,type(TID):1B,
                       size:1B, fire_mask_id:1B, crater_addr[(2*size)^2]:各2B]。
                       size=1 → 2x2(16x16), size=2 → 4x4(32x32)。
                       fire_mask_id: 0=非射撃 / 0x08=derota(Derota/GaruDerota) /
                       0x09=logram(Logram) / 0x10=boza(BozaLogram)。TID=3 の
                       Logram/BozaLogram 曖昧をこの mask で区別。射撃=自機狙い弾
                       type6 1.0px/f, ((rnd&ffreq[mask])+1)*8f, scol が前方帯の間のみ。
                       続いて sol_count(1B)+
                       1件[col:2B,row:1B, frame[4]の各[TL,TR,BL,BR]addr:2B×16]。
座標: col=(trigger+0xFD)&0xFF, row=(spriteY>>3)-2 (エリア1で検証, 全エリア共通式)。
地上物焼込: Barra(0x1E)/Zolbak(0x1F)/Logram(0x26)/GaruBarra(0x20=Barra同形)/
  BozaLogram(0x2D=Logram同形)=16x16(検証済)。
  Grobda(動)=PCGスプライト別, Sol(0x1D)=命中時上書き。
  ※GaruDerota(0x21)=32x32 は暫定(PROVISIONAL)。handler が多部品(2x2本体+
    中央砲塔 code0x27 の1x1 next-object)で、単純 2x2(tile0x24-0x27)合成は
    非コヒーレント(0x27は砲塔で BR 隅ではなく中央に乗るべき)。位置も terrain
    クリアリング非依存(spriteX/Yのみ)で pad 照合不可。アート・位置とも要実機照合。
    4x4 焼込インフラ(size=2, 16クレーターaddr)は本物。実データは暫定タイルで出力。
クレジット: tcdev42/re (tcdev/jotd)。ROM/出力は非コミット。
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xevi_extract as X
import xevi2x1_64 as T
import xevi_bake as XB
import xevi_objtbl as O

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "roms", "arcade", "xevious-out", "allareas")
TB = 0x0103                               # ローカル RAM タイルベース
GS = {0x1E: 0x17, 0x1F: 0x1F, 0x26: 0x2C, 0x20: 0x17, 0x2D: 0x2C}   # 焼込 16x16 -> tile
GS32 = {0x21: 0x24}                                    # 焼込 32x32(2x2 sprite base tile)
TID = {0x1E: 1, 0x1F: 2, 0x26: 3, 0x20: 1, 0x2D: 3, 0x21: 6}
GROB = {0x2C, 0x38, 0x3A}   # 動く地上物 Grobda(stationary/stops/darts)。焼込まず位置のみ出力
# 射撃砲台の ffreq mask_id(= SUB fn index, ffreq レコードと同じ番号)。非射撃=0。
#   Derota(0x1B)/GaruDerota(0x21)=derota 0x08, Logram(0x26)=logram 0x09, BozaLogram(0x2D)=boza 0x10。
#   gobj レコードに 1 バイト追加(TID=3 の Logram/Boza 曖昧を解消し turret 発火を正確化)。
FIRE_MASK = {0x1B: 0x08, 0x21: 0x08, 0x26: 0x09, 0x2D: 0x10}
SOL_FRAMES = (168, 169, 170, 171)

def sprite_rgb32(ex, g3, sp, rgb, base, cs):
    """32x32 の (rgb or None)。2x2 sprite(base=TL, +1=TR, +2=BL, +3=BR)。"""
    out = [[None]*32 for _ in range(32)]
    for t, ox, oy in ((base, 0, 0), (base+1, 16, 0), (base+2, 0, 16), (base+3, 16, 16)):
        px = ex.decode_sprite(g3, t)
        for y in range(16):
            for x in range(16):
                pen = sp[cs*8 + px[y][x]]
                if pen != 0x80:
                    out[oy+y][ox+x] = rgb[pen]
    return out

def crater_rgb(n):
    """n x n のクレーター(暗い窪み)。"""
    out = [[None]*n for _ in range(n)]
    c = (n-1)/2.0
    for y in range(n):
        for x in range(n):
            d = ((x-c)**2 + (y-c)**2) ** 0.5
            if d < n*0.42:
                out[y][x] = (30, 30, 30) if d < n*0.25 else (70, 70, 70)
    return out

def addq(colset, rgb, pen_rgb):
    r, g, b = pen_rgb
    colset.add((T.q4(b), T.q4(r), T.q4(g)))

def main():
    ex = X
    g1, g2, g3, g4, pr = X.build_regions(); XB.gfx3_cache = g3
    rgb, bg_pen, sp_pen, fg_pen = X.build_palette(pr)
    areatbl = T.load_area_table(); rom = O.load_subrom(); ptrs = O.area_ptrs(rom)

    # --- union palette(全エリア地形 + 地上物/Sol + crater) ---
    uc = set()
    for a in range(16):
        off = areatbl[a]; bs1 = [(off-1+T.ACROSS_SKIP+k) & 0x7f for k in range(25)]
        for bs0 in range(256):
            for b1 in bs1:
                code, color, fx, fy = X.bg_cell(g4, bs0, b1)
                for v in range(4):
                    addq(uc, rgb, rgb[bg_pen[color*4+v]])
    gs32_tiles = set()
    for base in GS32.values():
        gs32_tiles |= {base, base+1, base+2, base+3}
    for gt in set(GS.values()) | set(SOL_FRAMES) | gs32_tiles:
        px = X.decode_sprite(g3, gt)
        for y in range(16):
            for x in range(16):
                pen = sp_pen[7*8 + px[y][x]]
                if pen != 0x80:
                    addq(uc, rgb, rgb[pen])
    for cr in (XB.crater_rgb16(), crater_rgb(32)):
        for row in cr:
            for c in row:
                if c is not None:
                    addq(uc, rgb, c)
    uorder = list(uc); ucmap = {k: i for i, k in enumerate(uorder)}

    # --- 共通タイル表 ---
    common = []; pat2ci = {}
    def common_idx(pat):
        ci = pat2ci.get(pat)
        if ci is None:
            ci = len(common); pat2ci[pat] = ci; common.append(pat)
        return ci

    crater = XB.crater_rgb16()
    os.makedirs(OUT, exist_ok=True)
    summary = []

    for a in range(1, 17):
        off = areatbl[a-1]; bs1 = [(off-1+T.ACROSS_SKIP+k) & 0x7f for k in range(25)]
        local = []; ci2local = {}
        def local_idx(pat):
            ci = common_idx(pat)
            li = ci2local.get(ci)
            if li is None:
                li = len(local); ci2local[ci] = li; local.append(ci)
            return li
        lmap = [[0]*25 for _ in range(256)]
        for col in range(256):
            for row in range(25):
                code, color, fx, fy = X.bg_cell(g4, col, bs1[row])
                lmap[col][row] = local_idx(XB.rgb_to_pattern(
                    XB.terrain_cell_rgb(ex, g2, rgb, bg_pen, code, color, fx, fy), ucmap, uorder))

        objs = O.extract_ground(rom, ptrs[a-1], ptrs[a] if a < 16 else 0x2000)
        col_of = lambda t: (t + 0xFD) & 0xFF
        row_of = lambda y: (y >> 3) - 2

        def bake_cells(c0, r0, over, setmap, ncell=2):
            ids = []
            for cy in range(ncell):
                for cx in range(ncell):
                    col = (c0+cx) & 0xFF; row = r0+cy
                    if not (0 <= row < 25):
                        ids.append(None); continue
                    code, color, fx, fy = X.bg_cell(g4, col, bs1[row])
                    cell = XB.terrain_cell_rgb(ex, g2, rgb, bg_pen, code, color, fx, fy)
                    for yy in range(8):
                        for xx in range(8):
                            ov = over[cy*8+yy][cx*8+xx]
                            if ov is not None:
                                cell[yy][xx] = ov
                    li = local_idx(XB.rgb_to_pattern(cell, ucmap, uorder))
                    if setmap:
                        lmap[col][row] = li
                    ids.append(li)
            return ids

        crater32 = crater_rgb(32)
        gobj = []; sol = []; grobda = []
        for trig, typ, o, y in objs:
            c0, r0 = col_of(trig), row_of(y)
            if typ in GROB:
                grobda.append((c0, r0))
                continue
            if typ in GS:
                bake_cells(c0, r0, XB.sprite_rgb16(ex, rgb, sp_pen, GS[typ], 7), True)
                cids = bake_cells(c0, r0, crater, False)
                gobj.append((c0, r0, TID[typ], 1, FIRE_MASK.get(typ, 0), cids))
            elif typ in GS32:
                over32 = sprite_rgb32(ex, g3, sp_pen, rgb, GS32[typ], 7)
                bake_cells(c0, r0, over32, True, ncell=4)
                cids = bake_cells(c0, r0, crater32, False, ncell=4)
                gobj.append((c0, r0, TID[typ], 2, FIRE_MASK.get(typ, 0), cids))
            elif typ == 0x1D:
                frames = [bake_cells(c0, r0, XB.sprite_rgb16(ex, rgb, sp_pen, fr, 7), False)
                          for fr in SOL_FRAMES]
                sol.append((c0, r0, frames))

        def addr_of(li):
            return (TB + li*48) if li is not None else 0xFFFF
        with open(os.path.join(OUT, "area%02d_map.bin" % a), "wb") as f:
            for col in range(256):
                for row in range(25):
                    f.write(struct.pack("<H", TB + lmap[col][row]*48))
        with open(os.path.join(OUT, "area%02d_used.bin" % a), "wb") as f:
            f.write(struct.pack("<H", len(local)))   # 先頭2B: タイル数(エリア切替DMAのループ回数)
            for ci in local:
                f.write(struct.pack("<H", ci))
        with open(os.path.join(OUT, "area%02d_gobj.bin" % a), "wb") as f:
            f.write(bytes([len(gobj)]))
            for col, row, t, sz, fmask, cids in gobj:
                # col:2B, row:1B, TID:1B, size:1B, fire_mask_id:1B(0=非射撃/0x08/0x09/0x10), crater addrs
                f.write(struct.pack("<H", col) + bytes([row & 0xff, t, sz, fmask]))
                for li in cids:
                    f.write(struct.pack("<H", addr_of(li)))
            f.write(bytes([len(sol)]))
            for col, row, frames in sol:
                f.write(struct.pack("<H", col) + bytes([row & 0xff]))
                for fr in frames:
                    for li in fr:
                        f.write(struct.pack("<H", addr_of(li)))
            # Grobda(動く地上物): sol の後ろに追記。grob_count(1B) + 1件[col:2B LE, row:1B]
            f.write(bytes([len(grobda)]))
            for col, row in grobda:
                f.write(struct.pack("<H", col) + bytes([row & 0xff]))
        summary.append((a, off, len(local), len(local)*48, len(gobj), len(sol), len(grobda)))

    with open(os.path.join(OUT, "common_tiles.bin"), "wb") as f:
        for p in common:
            f.write(p)
    with open(os.path.join(OUT, "common_pal.bin"), "wb") as f:
        for i in range(64):
            b4, r4, g4v = uorder[i] if i < len(uorder) else (0, 0, 0)
            f.write(struct.pack("<H", T.BANKTBL0[i]) + bytes((b4, r4, g4v)))

    print("=== 全16エリア 一括生成 (B案: 共通表 + ローカルID) ===")
    print("共通パレット: %d 色 / 64" % len(uorder))
    print("共通タイル表: %d タイル = %d B (%.1f KB)" %
          (len(common), len(common)*48, len(common)*48/1024))
    print("area off  used(RAM展開)  gobj sol grob  map(EMM)")
    for a, off, n, sz, ng, ns, ngr in summary:
        print("  %2d 0x%02X  %3d (%5dB)  %2d  %d  %2d   12800B" % (a, off, n, sz, ng, ns, ngr))
    tot_map = 16*12800
    print("タイルマップ合計(16エリア): %d B (%.0f KB)" % (tot_map, tot_map/1024))
    print("出力: roms/arcade/xevious-out/allareas/")

if __name__ == "__main__":
    main()
