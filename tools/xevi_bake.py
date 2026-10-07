#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Xevious エリア地上物の「タイル焼き込み」ツール。

xevi2x1_64.py が作る 64色タイルマップ(xtilemap64/xtiles64)に、動かない地上物
(Barra/Zolbak/Logram)の絵を地形タイルへ合成し、ユニークタイルを追加して
差し替え版を出力する。位置と種別は xevi_objtbl.py(tcdev42/re の注釈付き
逆アセンブル参照)から取得。

座標(確定・検証済み):
  tilemap_col = (trigger + 0xF2) & 0xFF   (get_map_row の bs0; xtilemap64 は col=bs0)
  row         = spriteY >> 3
地上物スプライト(size=0 ⇒ tile=code, DIRECT; Toroid code8=tile8 で確認):
  Barra=tile 0x17, Zolbak=tile 0x1F, Logram=tile 0x2C
クレーター: アーケードに専用スプライトは無い(破壊→爆発→消滅)。ポート用に
  暗い窪みを地形へ合成したタイルを生成する。

ROM/出力は非コミット(roms/ は gitignore)。
クレジット: tcdev42/re (Mark McDougall/tcdev, Jean-Francois Fabre/jotd)。
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xevi2x1_64 as T       # cmap/tile_pattern_64/定数を再利用
import xevi_objtbl as O
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "roms", "arcade", "xevious-out")

GROUND_SPRITE = {0x1E: 0x17, 0x1F: 0x1F, 0x26: 0x2C}   # type -> sprite tile(code)
GROUND_COLOR  = {0x1E: 7, 0x1F: 7, 0x26: 7}            # 各地上物の sprite colour code

def build_cmap(ex, g2, g4, rgb, bg_pen, bs1_list):
    seen = {}
    order = []
    for bs0 in range(T.BS0_LEN):
        for bs1 in bs1_list:
            code, color, fx, fy = ex.bg_cell(g4, bs0, bs1)
            pal = [rgb[bg_pen[color*4+v]] for v in range(4)]
            for v in range(4):
                r, g, b = pal[v]
                key = (T.q4(b), T.q4(r), T.q4(g))
                if key not in seen:
                    seen[key] = len(order); order.append(key)
    cmap = dict(seen)
    return cmap, order

def nearest_idx(order, b4, r4, g4):
    return min(range(len(order)), key=lambda j: (order[j][0]-b4)**2 +
               (order[j][1]-r4)**2 + (order[j][2]-g4)**2)

def rgb_to_pattern(cell_rgb, cmap, order):
    """8x8 RGB -> 48B 6面パターン。"""
    planes = [bytearray(8) for _ in range(6)]
    for y in range(8):
        for x in range(8):
            r, g, b = cell_rgb[y][x]
            key = (T.q4(b), T.q4(r), T.q4(g))
            idx = cmap.get(key)
            if idx is None:
                idx = nearest_idx(order, *key)
            for p in range(6):
                if (idx >> p) & 1:
                    planes[p][y] |= (0x80 >> x)
    return bytes(b"".join(planes))

def terrain_cell_rgb(ex, g2, rgb, bg_pen, code, color, fx, fy):
    raw = ex.decode_bg_tile(g2, code)
    pal = [rgb[bg_pen[color*4+v]] for v in range(4)]
    out = [[None]*8 for _ in range(8)]
    for y in range(8):
        sy = 7-y if fy else y
        for x in range(8):
            sx = 7-x if fx else x
            out[y][x] = pal[raw[sy][sx]]
    return out

def sprite_rgb16(ex, rgb, sp_pen, tile, color):
    """16x16 の (rgb or None[透明])。"""
    px = ex.decode_sprite(gfx3_cache, tile)
    out = [[None]*16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            pen = sp_pen[color*8 + px[y][x]]
            out[y][x] = None if pen == 0x80 else rgb[pen]
    return out

def crater_rgb16():
    """16x16 のクレーター上書き(暗い窪み)。中央の暗円のみ非透明。"""
    out = [[None]*16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            dx, dy = x-7.5, y-7.5
            d = (dx*dx+dy*dy) ** 0.5
            if d < 6.5:
                shade = 30 if d < 4 else 70
                out[y][x] = (shade, shade, shade)
    return out

gfx3_cache = None

def main():
    ex = T.load_extract()
    global gfx3_cache
    g1,g2,g3,g4,pr = ex.build_regions(); gfx3_cache = g3
    rgb,bg_pen,sp_pen,fg_pen = ex.build_palette(pr)
    area = 1
    area_off = T.load_area_table()[area-1]
    bs1_list = [(area_off - 1 + T.ACROSS_SKIP + k) & 0x7f for k in range(T.ACROSS_USE)]

    cmap, order = build_cmap(ex, g2, g4, rgb, bg_pen, bs1_list)

    # ベース: tiles(48B) + tilemap(col x row = tid)
    pat_to_id = {}; tiles = []; tilemap = []
    for col in range(T.BS0_LEN):
        colids = []
        for row in range(T.ACROSS_USE):
            code,color,fx,fy = ex.bg_cell(g4, col, bs1_list[row])
            cell = terrain_cell_rgb(ex, g2, rgb, bg_pen, code, color, fx, fy)
            pat = rgb_to_pattern(cell, cmap, order)
            tid = pat_to_id.get(pat)
            if tid is None:
                tid = len(tiles); pat_to_id[pat] = tid; tiles.append(pat)
            colids.append(tid)
        tilemap.append(colids)
    base_n = len(tiles)

    # 地上物抽出 + 焼き込み
    rom = O.load_subrom(); ptrs = O.area_ptrs(rom)
    objs = O.extract_ground(rom, ptrs[0], ptrs[1])
    placements = []   # (col,row,type,crater_tid_2x2)
    # 補正: get_map_row の bs0 = (trigger+0xF2) に +11 列、across は spriteY>>3 -2 行。
    #   クリアリング中心合わせ(全10物の2x2=40/40 が tan タイルに乗る)で実測。
    def col_of(t): return (t + 0xFD) & 0xFF      # 0xF2 + 11
    def row_of(y): return (y >> 3) - 2

    def bake(col0, row0, over16):
        """16x16 over を (col0,row0) の 2x2 セルへ焼き込み、tid 4つを返す。"""
        ids = []
        for cy in range(2):
            for cx in range(2):
                col = (col0 + cx) & 0xFF; row = row0 + cy
                if not (0 <= row < T.ACROSS_USE):
                    ids.append(None); continue
                code,color,fx,fy = ex.bg_cell(g4, col, bs1_list[row])
                cell = terrain_cell_rgb(ex, g2, rgb, bg_pen, code, color, fx, fy)
                for y in range(8):
                    for x in range(8):
                        ov = over16[cy*8+y][cx*8+x]
                        if ov is not None:
                            cell[y][x] = ov
                pat = rgb_to_pattern(cell, cmap, order)
                tid = pat_to_id.get(pat)
                if tid is None:
                    tid = len(tiles); pat_to_id[pat] = tid; tiles.append(pat)
                tilemap[col][row] = tid
                ids.append(tid)
        return ids

    obj_tiles_added_start = len(tiles)
    for trig,typ,off,y in objs:
        if typ not in GROUND_SPRITE:   # Grobda(動)/Sol(隠)は焼込まず
            placements.append((col_of(trig), row_of(y), typ, None)); continue
        col0, row0 = col_of(trig), row_of(y)
        over = sprite_rgb16(ex, rgb, sp_pen, GROUND_SPRITE[typ], GROUND_COLOR[typ])
        oids = bake(col0, row0, over)
        placements.append((col0, row0, typ, ("baked", oids)))
    obj_n = len(tiles)

    # クレーター(焼込対象と同位置)
    crater = crater_rgb16()
    crater_ids = {}
    for col0,row0,typ,info in placements:
        if info is None or info[0] != "baked":
            continue
        cids = bake(col0, row0, crater)   # 注意: tilemap は上書きされるので別途保持
        # tilemap を地上物版へ戻す(クレーターは破壊時に使う ID 列として保持のみ)
        for k,(cx,cy) in enumerate([(0,0),(1,0),(0,1),(1,1)]):
            col=(col0+cx)&0xFF; row=row0+cy
            if 0<=row<T.ACROSS_USE:
                tilemap[col][row] = info[1][k]
        crater_ids[(col0,row0)] = cids
    crater_n = len(tiles)

    # レポート
    print("=== エリア1 地上物 焼き込み ===")
    print("ベース地形ユニークタイル: %d" % base_n)
    print("+ 地上物焼込で追加: %d (計 %d)" % (obj_n-base_n, obj_n))
    print("+ クレーターで追加: %d (計 %d)" % (crater_n-obj_n, crater_n))
    TB = 0x0103
    print("タイル表末尾 addr: 0x%04X (tilebase 0x%04X + %d*48)" % (TB+(crater_n-1)*48, TB, crater_n-1))
    print()
    print("位置リスト (tilemap_col, row, type_id, crater_tile_ids[TL,TR,BL,BR]):")
    TID={0x1E:1,0x1F:2,0x26:3,0x1D:4,0x2C:5}
    for col0,row0,typ,info in placements:
        cids = crater_ids.get((col0,row0))
        craddr = [hex(TB+c*48) for c in cids] if cids else "-(焼込対象外)"
        print("  col=%3d row=%2d type=%d %s crater=%s" %
              (col0,row0,TID[typ],O.GROUND_TYPES[typ], craddr))

    if "--emit" in sys.argv:
        with open(os.path.join(OUT,"xtilemap64_obj.bin"),"wb") as f:
            for col in tilemap:
                for tid in col:
                    addr = TB + tid*48
                    f.write(bytes([addr&0xff,(addr>>8)&0xff]))
        with open(os.path.join(OUT,"xtiles64_obj.bin"),"wb") as f:
            for t in tiles: f.write(t)
        print("出力: roms/arcade/xevious-out/xtilemap64_obj.bin, xtiles64_obj.bin")

    # プレビュー(地上物焼込後の area をアーケード類似色で)
    if "--preview" in sys.argv:
        pal_rgb = []
        for key in order:
            b4,r4,g4v = key
            pal_rgb.append((r4*17, g4v*17, b4*17))
        img = Image.new("RGB",(T.BS0_LEN*4, T.ACROSS_USE*8))
        # tiles -> 復元描画は省略、tilemap の tid の代表色で簡易表示
        for col in range(T.BS0_LEN):
            for row in range(T.ACROSS_USE):
                tid = tilemap[col][row]
                pat = tiles[tid]
                for y in range(8):
                    for x in range(8):
                        idx=0
                        for p in range(6):
                            if pat[p*8+y] & (0x80>>x): idx |= (1<<p)
                        c = pal_rgb[idx] if idx < len(pal_rgb) else (0,0,0)
                        img.putpixel((col*4 + x//2, row*8+y), c)
        img.save(os.path.join(OUT,"bake_preview.png"))
        print("プレビュー: roms/arcade/xevious-out/bake_preview.png")

if __name__ == "__main__":
    main()
