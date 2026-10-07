#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
xevi_aircolor.py - アーケード版ゼビウスの空中物スプライトで使われる色を ROM から
集計し、turboZ テキストパレット(7色+透明, 各 RGB 2bit/ch = 64色中)への割当候補を
2〜3 案出す。比較画像と色数表を roms/arcade/xevious-out/aircolor/ に出力。

色の出所:
  - スプライトのピクセル(pen 0..7)と全パレット(rgb PROM + sprite LUT sp_pen)は
    すべて ROM 由来(xevi_extract.py のデコードを流用)。
  - 各敵の color code は xevi_extract.py:212 の式で決まる値域(0..63)から、資料で
    確定しているアーケードの見た目を再現するコードを選ぶ。RGB 値自体は ROM の
    rgb PROM そのもの。MAME が無く実行時カラー RAM をダンプできないための方針。

サブコマンド:
  sheet   : 全 320 スプライトをタイル番号付きで並べた識別用シートを出力。
  codes   : 指定タイルを 0..63 の全 color code で描いた一覧(色コード特定用)。
  build   : 空中物テーブル(AERIAL)に基づき色集計 + 7色3案 + 比較画像 + 表を出力。
"""
import os, sys, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "roms", "arcade", "xevious-out", "aircolor")
from PIL import Image, ImageDraw


def load_extract():
    p = os.path.join(ROOT, "tools", "xevi_extract.py")
    spec = importlib.util.spec_from_file_location("xevi_extract", p)
    ex = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ex)
    return ex


def sprite_colors(ex, sp_pen, px, cs):
    """pen 16x16 と color code cs -> rgb index の 16x16 (0x80=透明)。"""
    out = [[0x80] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            out[y][x] = sp_pen[cs * 8 + px[y][x]]
    return out


def best_code(ex, sp_pen, px):
    """非透明の異なる rgb index 数が最大になる color code (識別シート用)。"""
    best, bn = 0, -1
    for cs in range(64):
        s = set()
        for row in px:
            for v in row:
                pen = sp_pen[cs * 8 + v]
                if pen != 0x80:
                    s.add(pen)
        if len(s) > bn:
            bn, best = len(s), cs
    return best


def render_tile(rgb, sp_pen, px, cs, scale=3, bg=(30, 30, 36)):
    im = Image.new("RGB", (16 * scale, 16 * scale), bg)
    pix = im.load()
    for y in range(16):
        for x in range(16):
            pen = sp_pen[cs * 8 + px[y][x]]
            if pen == 0x80:
                continue
            c = rgb[pen]
            for dy in range(scale):
                for dx in range(scale):
                    pix[x * scale + dx, y * scale + dy] = c
    return im


def cmd_sheet(ex, rgb, sp_pen, gfx3):
    os.makedirs(OUT_DIR, exist_ok=True)
    N = 320
    cols, scale, pad, lab = 16, 3, 4, 10
    cw = 16 * scale + pad
    ch = 16 * scale + pad + lab
    rows = (N + cols - 1) // cols
    img = Image.new("RGB", (cols * cw, rows * ch), (0, 0, 0))
    dr = ImageDraw.Draw(img)
    for n in range(N):
        px = ex.decode_sprite(gfx3, n)
        cs = best_code(ex, sp_pen, px)
        t = render_tile(rgb, sp_pen, px, cs, scale)
        ox = (n % cols) * cw + pad // 2
        oy = (n // cols) * ch + lab
        img.paste(t, (ox, oy))
        dr.text((ox, oy - lab), "%d" % n, fill=(200, 200, 200))
    p = os.path.join(OUT_DIR, "sprite_sheet_labeled.png")
    img.save(p)
    print("wrote", p, img.size)


def cmd_codes(ex, rgb, sp_pen, gfx3, tiles):
    os.makedirs(OUT_DIR, exist_ok=True)
    scale, pad, lab = 3, 4, 10
    cw = 16 * scale + pad
    ch = 16 * scale + pad + lab
    cols = 16  # color code 0..15,16..31,... 4 rows per tile
    for n in tiles:
        px = ex.decode_sprite(gfx3, n)
        rows = 4
        img = Image.new("RGB", (cols * cw, rows * ch), (0, 0, 0))
        dr = ImageDraw.Draw(img)
        for cs in range(64):
            t = render_tile(rgb, sp_pen, px, cs, scale)
            ox = (cs % cols) * cw + pad // 2
            oy = (cs // cols) * ch + lab
            img.paste(t, (ox, oy))
            dr.text((ox, oy - lab), "%d" % cs, fill=(180, 180, 180))
        p = os.path.join(OUT_DIR, "codes_tile%03d.png" % n)
        img.save(p)
        print("wrote", p)


# ---------------------------------------------------------------------------
# 空中物テーブル (name, tile, color_code)。color_code は xevi_extract.py:212 の
# 値域(0..63)から、アーケードの確定している見た目を再現するコードを選定。
# RGB 値自体は ROM の rgb PROM。※実行時カラー RAM を MAME でダンプできないため
# 象徴的でアーケード色が周知の空中物に限定(ユーザー確認で拡張/訂正可能)。
# ---------------------------------------------------------------------------
# 訂正(2026-10-07, アーケード実機スクショ + 資料): 実機は金属的な銀/灰/白+赤 基調。
#   当初の Toroid=緑(code24) / Solvalou=白青赤(code35) は誤り。
#   code 7 = 白/灰/赤(金属色)を多くの敵が共有。黒球=code44。爆発=code12(炎)。
AERIAL = [
    ("Solvalou(自機)",    162,  7),   # 白/灰/赤 (tile160-165, 中央赤コックピット)
    ("Toroid(トーロイド)",  10,  7),   # 銀灰リング+中心赤 (緑ではない)
    ("Bacura(バキュラ)",   216,  2),   # 銀/クリーム 回転板
    ("AndorGenesis",      200,  7),   # 銀灰ボディ+赤
    ("Zakato/Brag(黒球)",  184, 44),   # 黒球+灰 (ザカート/ブラグザカート)
    ("爆発(explosion)",    104, 12),   # 赤/橙/黄(炎)
]

import numpy as np


def srgb_to_lab(rgb):
    a = np.asarray(rgb, dtype=float) / 255.0
    lin = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
    M = np.array([[0.4124, 0.3576, 0.1805],
                  [0.2126, 0.7152, 0.0722],
                  [0.0193, 0.1192, 0.9505]])
    xyz = lin @ M.T
    white = np.array([0.95047, 1.0, 1.08883])
    xyz = xyz / white
    e, k = 216.0 / 24389.0, 24389.0 / 27.0
    f = np.where(xyz > e, np.cbrt(xyz), (k * xyz + 16.0) / 116.0)
    L = 116.0 * f[..., 1] - 16.0
    A = 500.0 * (f[..., 0] - f[..., 1])
    B = 200.0 * (f[..., 1] - f[..., 2])
    return np.stack([L, A, B], -1)


def de2000(lab1, lab2):
    """CIEDE2000。lab1,lab2 は (...,3)、ブロードキャスト可。"""
    L1, a1, b1 = lab1[..., 0], lab1[..., 1], lab1[..., 2]
    L2, a2, b2 = lab2[..., 0], lab2[..., 1], lab2[..., 2]
    C1 = np.hypot(a1, b1)
    C2 = np.hypot(a2, b2)
    Cbar = (C1 + C2) / 2.0
    G = 0.5 * (1 - np.sqrt(Cbar ** 7 / (Cbar ** 7 + 25.0 ** 7)))
    a1p = (1 + G) * a1
    a2p = (1 + G) * a2
    C1p = np.hypot(a1p, b1)
    C2p = np.hypot(a2p, b2)
    h1p = np.degrees(np.arctan2(b1, a1p)) % 360
    h2p = np.degrees(np.arctan2(b2, a2p)) % 360
    dLp = L2 - L1
    dCp = C2p - C1p
    dhp = h2p - h1p
    dhp = np.where(dhp > 180, dhp - 360, dhp)
    dhp = np.where(dhp < -180, dhp + 360, dhp)
    dHp = 2 * np.sqrt(C1p * C2p) * np.sin(np.radians(dhp) / 2.0)
    Lbarp = (L1 + L2) / 2.0
    Cbarp = (C1p + C2p) / 2.0
    hsum = h1p + h2p
    hdiff = np.abs(h1p - h2p)
    hbarp = np.where(C1p * C2p == 0, hsum,
                     np.where(hdiff <= 180, hsum / 2.0,
                              np.where(hsum < 360, (hsum + 360) / 2.0,
                                       (hsum - 360) / 2.0)))
    T = (1 - 0.17 * np.cos(np.radians(hbarp - 30))
         + 0.24 * np.cos(np.radians(2 * hbarp))
         + 0.32 * np.cos(np.radians(3 * hbarp + 6))
         - 0.20 * np.cos(np.radians(4 * hbarp - 63)))
    dtheta = 30 * np.exp(-(((hbarp - 275) / 25.0) ** 2))
    Rc = 2 * np.sqrt(Cbarp ** 7 / (Cbarp ** 7 + 25.0 ** 7))
    Sl = 1 + (0.015 * (Lbarp - 50) ** 2) / np.sqrt(20 + (Lbarp - 50) ** 2)
    Sc = 1 + 0.045 * Cbarp
    Sh = 1 + 0.015 * Cbarp * T
    Rt = -np.sin(np.radians(2 * dtheta)) * Rc
    return np.sqrt((dLp / Sl) ** 2 + (dCp / Sc) ** 2 + (dHp / Sh) ** 2
                   + Rt * (dCp / Sc) * (dHp / Sh))


def turboz_palette():
    """64色: 各 RGB 2bit/ch。戻り: list[(r,g,b 255scale)], list[portval], list[(lr,lg,lb)]"""
    cols, ports, lv = [], [], []
    for g in range(4):
        for r in range(4):
            for b in range(4):
                cols.append((r * 85, g * 85, b * 85))
                ports.append((g << 4) | (r << 2) | b)
                lv.append((r, g, b))
    return cols, ports, lv


def collect_colors(ex, rgb, sp_pen, gfx3):
    """AERIAL 各敵の (rgb -> pixel数) と全体加重ヒストグラム。"""
    per = []
    total = {}
    for name, tile, cs in AERIAL:
        px = ex.decode_sprite(gfx3, tile)
        hist = {}
        for row in px:
            for v in row:
                pen = sp_pen[cs * 8 + v]
                if pen == 0x80:
                    continue
                c = rgb[pen]
                hist[c] = hist.get(c, 0) + 1
                total[c] = total.get(c, 0) + 1
        per.append((name, tile, cs, hist))
    return per, total


def choose_greedy(tgt_lab, tgt_w, cand_lab, fixed=None, k=7):
    """weighted sum of nearest dE を最小化する k 色を貪欲+局所交換で選ぶ。indices を返す。"""
    n = len(cand_lab)
    # dE matrix targets x candidates
    D = de2000(tgt_lab[:, None, :], cand_lab[None, :, :])  # (T, n)
    chosen = list(fixed) if fixed else []

    def cost(sel):
        sub = D[:, sel].min(axis=1)
        return float((sub * tgt_w).sum())

    while len(chosen) < k:
        best, bc = None, None
        for c in range(n):
            if c in chosen:
                continue
            v = cost(chosen + [c])
            if bc is None or v < bc:
                bc, best = v, c
        chosen.append(best)
    # 局所交換
    improved = True
    while improved:
        improved = False
        cur = cost(chosen)
        for i in range(len(chosen)):
            if fixed and i < len(fixed):
                continue
            for c in range(n):
                if c in chosen:
                    continue
                trial = chosen[:i] + [c] + chosen[i + 1:]
                v = cost(trial)
                if v < cur - 1e-9:
                    chosen, cur, improved = trial, v, True
    return chosen, cost(chosen)


def remap_sprite_img(ex, rgb, sp_pen, px, cs, pal_cols, pal_lab, scale=4,
                     bg=(28, 28, 34)):
    """各非透明ピクセルを pal の最近傍色(ΔE2000)に置換して描画。"""
    im = Image.new("RGB", (16 * scale, 16 * scale), bg)
    pix = im.load()
    for y in range(16):
        for x in range(16):
            pen = sp_pen[cs * 8 + px[y][x]]
            if pen == 0x80:
                continue
            c = rgb[pen]
            lab = srgb_to_lab(np.array(c))
            d = de2000(lab[None, :], pal_lab)
            c2 = pal_cols[int(d.argmin())]
            for dy in range(scale):
                for dx in range(scale):
                    pix[x * scale + dx, y * scale + dy] = c2
    return im


def cmd_build(ex, rgb, sp_pen, gfx3):
    os.makedirs(OUT_DIR, exist_ok=True)
    cols, ports, lv = turboz_palette()
    cand_lab = srgb_to_lab(np.array(cols, dtype=float))
    per, total = collect_colors(ex, rgb, sp_pen, gfx3)

    tgt_cols = list(total.keys())
    tgt_w = np.array([total[c] for c in tgt_cols], dtype=float)
    tgt_lab = srgb_to_lab(np.array(tgt_cols, dtype=float))

    def idx_of(r, g, b):  # level 0..3
        return g * 16 + r * 4 + b

    # 案A: 全体加重 ΔE 最小
    A_idx, A_cost = choose_greedy(tgt_lab, tgt_w, cand_lab, None, 7)
    # 案B: アンカー(白/赤/青/黄)固定 + 残3最適
    #   黄を固定しないと爆発/弾の黄(255,255,0)が緑へ化けるため(ザッパー弾も黄系)
    anchorB = [idx_of(3, 3, 3), idx_of(3, 0, 0), idx_of(0, 0, 3), idx_of(3, 3, 0)]
    B_idx, B_cost = choose_greedy(tgt_lab, tgt_w, cand_lab, anchorB, 7)
    # 案C: 色相保持(手置き7色)
    C_idx = [idx_of(3, 3, 3), idx_of(3, 0, 0), idx_of(0, 3, 0),
             idx_of(0, 0, 3), idx_of(3, 3, 0), idx_of(3, 1, 0), idx_of(2, 2, 2)]

    def metrics(sel):
        D = de2000(tgt_lab[:, None, :], cand_lab[None, sel, :]).min(axis=1)
        wsum = float((D * tgt_w).sum() / tgt_w.sum())
        return wsum, float(D.max())

    proposals = [("A_globalDE", A_idx), ("B_anchored", B_idx),
                 ("C_hue", C_idx)]

    # 検証: 爆発/弾の黄・橙・赤が各案でどの色に写るか(緑化アーティファクト検出)
    for key, sel in [("A", A_idx), ("B", B_idx), ("C", C_idx)]:
        for c in [(255, 255, 0), (255, 143, 0), (255, 0, 0)]:
            lab = srgb_to_lab(np.array(c, dtype=float))
            d = de2000(lab[None, :], cand_lab[sel])
            ni = sel[int(d.argmin())]
            print("  map[%s] %s -> %s (dE=%.1f)" % (key, c, cols[ni], d.min()))

    # パレット見本 + 比較画像
    report = []
    for pname, sel in proposals:
        pal_cols = [cols[i] for i in sel]
        pal_lab = cand_lab[sel]
        wsum, mx = metrics(sel)
        report.append((pname, sel, wsum, mx))
        # swatch
        sw = Image.new("RGB", (7 * 48, 70), (0, 0, 0))
        dr = ImageDraw.Draw(sw)
        for i, si in enumerate(sel):
            dr.rectangle([i * 48, 0, i * 48 + 47, 47], fill=cols[si])
            dr.text((i * 48 + 2, 50), "0x%02X" % ports[si], fill=(220, 220, 220))
        sw.save(os.path.join(OUT_DIR, "palette_%s.png" % pname))
        # 比較: 各敵 original vs remapped
        scale = 4
        cw = 16 * scale + 6
        rows_img = []
        comp = Image.new("RGB", (cw * 2 + 8, cw * len(AERIAL)), (18, 18, 22))
        for r, (name, tile, cs, hist) in enumerate(per):
            px = ex.decode_sprite(gfx3, tile)
            orig = render_tile(rgb, sp_pen, px, cs, scale, bg=(28, 28, 34))
            rem = remap_sprite_img(ex, rgb, sp_pen, px, cs, pal_cols, pal_lab,
                                   scale)
            comp.paste(orig, (0, r * cw))
            comp.paste(rem, (cw + 8, r * cw))
        comp.save(os.path.join(OUT_DIR, "compare_%s.png" % pname))

    # 全敵 x 3案 を1枚に: original | A | B | C
    scale = 4
    cw = 16 * scale + 6
    allimg = Image.new("RGB", (cw * 4 + 12, cw * len(AERIAL) + 20), (18, 18, 22))
    dr = ImageDraw.Draw(allimg)
    for col, lab in enumerate(["original", "A", "B", "C"]):
        dr.text((col * (cw + 4) + 4, 2), lab, fill=(230, 230, 230))
    palmap = {"A": [cols[i] for i in A_idx], "B": [cols[i] for i in B_idx],
              "C": [cols[i] for i in C_idx]}
    lmap = {"A": cand_lab[A_idx], "B": cand_lab[B_idx], "C": cand_lab[C_idx]}
    for r, (name, tile, cs, hist) in enumerate(per):
        px = ex.decode_sprite(gfx3, tile)
        y = 18 + r * cw
        allimg.paste(render_tile(rgb, sp_pen, px, cs, scale, (28, 28, 34)),
                     (4, y))
        for ci, key in enumerate(["A", "B", "C"]):
            rem = remap_sprite_img(ex, rgb, sp_pen, px, cs, palmap[key],
                                   lmap[key], scale)
            allimg.paste(rem, ((ci + 1) * (cw + 4) + 4, y))
    allimg = allimg.resize((allimg.width * 2, allimg.height * 2), Image.NEAREST)
    allimg.save(os.path.join(OUT_DIR, "compare_all.png"))

    # テキスト表
    lines = []
    lines.append("# ゼビウス空中物 色集計 + turboZ テキストパレット7色 候補")
    lines.append("")
    lines.append("## 空中物ごとの使用色 (color code = ROM LUT, RGB = rgb PROM)")
    lines.append("| 敵 | tile | code | 色数 | 色(RGB) |")
    lines.append("|---|---|---|---|---|")
    for name, tile, cs, hist in per:
        items = sorted(hist.items(), key=lambda kv: -kv[1])
        cstr = ", ".join("%d,%d,%d(%d)" % (c[0], c[1], c[2], n)
                         for c, n in items)
        lines.append("| %s | %d | %d | %d | %s |"
                     % (name, tile, cs, len(hist), cstr))
    lines.append("")
    lines.append("## 7色案 (各色 2bit/ch, port値=OUT 0x1FB8|slot)")
    for pname, sel, wsum, mx in report:
        pv = " ".join("0x%02X(%d,%d,%d)" % (ports[i], cols[i][0], cols[i][1],
                                            cols[i][2]) for i in sel)
        lines.append("- **%s**: 加重平均ΔE2000=%.2f 最大ΔE=%.2f" % (pname, wsum, mx))
        lines.append("    色: %s" % pv)
    txt = "\n".join(lines)
    with open(os.path.join(OUT_DIR, "aircolor_report.md"), "w") as f:
        f.write(txt + "\n")
    print(txt)
    print("\nwrote images + aircolor_report.md to", OUT_DIR)


# A案 7色(slot1..7)の port値・表示RGB。build の A_globalDE 結果(確定)。
APLAN = [
    (0x3E, (255, 255, 170)),
    (0x0C, (255, 0, 0)),
    (0x30, (0, 255, 0)),
    (0x2D, (255, 170, 85)),
    (0x15, (85, 85, 85)),
    (0x3F, (255, 255, 255)),
    (0x27, (85, 170, 255)),
]


def cmd_ship(ex, rgb, sp_pen, gfx3, tile=162, code=7):
    """Solvalou を A案スロットに割り当て、png2ship 入力PNG + 表示プレビューを出す。
    向きは地形と同じ rotate(-90)(xevi_extract の map_arcade_rot90 と同じ)。"""
    os.makedirs(OUT_DIR, exist_ok=True)
    out_root = os.path.join(ROOT, "roms", "arcade", "xevious-out")
    slot_lab = srgb_to_lab(np.array([c for _, c in APLAN], dtype=float))

    px = ex.decode_sprite(gfx3, tile)
    # 16x16 の (slot or None)。slot は 1..7(= tc)。
    enc = Image.new("RGBA", (16, 16), (0, 0, 0, 0))     # png2ship 入力(符号化)
    prev = Image.new("RGBA", (16, 16), (0, 0, 0, 0))    # 表示プレビュー
    pe, pp = enc.load(), prev.load()
    usage = {}
    for y in range(16):
        for x in range(16):
            pen = sp_pen[code * 8 + px[y][x]]
            if pen == 0x80:
                continue                                # 透明
            c = rgb[pen]
            lab = srgb_to_lab(np.array(c, dtype=float))
            d = de2000(lab[None, :], slot_lab)
            s = int(d.argmin()) + 1                     # slot 1..7 (= tc)
            usage[s] = usage.get(s, 0) + 1
            # 符号化: plane B=s&1, R=s&2, G=s&4 を各チャンネル 255/0 に
            pe[x, y] = (255 if (s & 2) else 0,           # R chan = R plane
                        255 if (s & 4) else 0,           # G chan = G plane
                        255 if (s & 1) else 0, 255)      # B chan = B plane
            pp[x, y] = (APLAN[s - 1][1][0], APLAN[s - 1][1][1],
                        APLAN[s - 1][1][2], 255)
    # 向きメモ: Xevious はハード ROT90。decode_sprite は raw(=画面と90°ずれ)。
    #   arcade 画面で機首=上 は raw を +90(CCW)。
    #   X1 は前進方向=右(emmscroll64 が右端 col39 を新規列に展開)なので機首=右。
    #   機首=右 は raw を 180 回転(raw は機首=左)。→ X1 データは rotate(180)。
    X1_ROT = 180
    enc_r = enc.rotate(X1_ROT, expand=True)
    enc_p = os.path.join(out_root, "solvalou_ship.png")
    enc_r.save(enc_p)

    # プレビュー: 暗緑背景に合成し、確認用に複数向きを並べる。
    def on_bg(im, scale=10):
        bg = Image.new("RGBA", im.size, (40, 90, 40, 255))
        bg.alpha_composite(im)
        return bg.convert("RGB").resize((im.width * scale, im.height * scale),
                                        Image.NEAREST)
    views = [("raw(機首左)", prev.rotate(0, expand=True)),
             ("arcade上向き(+90)", prev.rotate(90, expand=True)),
             ("X1 機首右(180)", prev.rotate(180, expand=True)),
             ("機首下(-90)", prev.rotate(-90, expand=True))]
    cw = 16 * 10
    cmp = Image.new("RGB", (len(views) * (cw + 6), cw + 16), (0, 0, 0))
    dr = ImageDraw.Draw(cmp)
    for i, (nm, im) in enumerate(views):
        cmp.paste(on_bg(im), (i * (cw + 6), 14))
        dr.text((i * (cw + 6) + 2, 2), nm, fill=(230, 230, 230))
    cmp.save(os.path.join(out_root, "solvalou_ship_preview.png"))
    print("Solvalou tile=%d code=%d (白/灰/赤, 青なし)" % (tile, code))
    print("slot 使用ピクセル数(= tc):")
    for s in sorted(usage):
        print("  slot%d port0x%02X %s x%d" % (s, APLAN[s - 1][0],
                                              APLAN[s - 1][1], usage[s]))
    print("使用スロット:", sorted(usage), " SHIP_PLANES=0x07(全プレーン)")
    print("png2ship 入力:", enc_p)
    print("プレビュー:", os.path.join(out_root, "solvalou_ship_preview.png"))


def main(argv):
    ex = load_extract()
    gfx1, gfx2, gfx3, gfx4, proms = ex.build_regions()
    rgb, bg_pen, sp_pen, fg_pen = ex.build_palette(proms)
    cmd = argv[1] if len(argv) > 1 else "sheet"
    if cmd == "sheet":
        cmd_sheet(ex, rgb, sp_pen, gfx3)
    elif cmd == "codes":
        tiles = [int(a) for a in argv[2:]]
        cmd_codes(ex, rgb, sp_pen, gfx3, tiles)
    elif cmd == "build":
        cmd_build(ex, rgb, sp_pen, gfx3)
    elif cmd == "ship":
        t = int(argv[2]) if len(argv) > 2 else 162
        c = int(argv[3]) if len(argv) > 3 else 7
        cmd_ship(ex, rgb, sp_pen, gfx3, t, c)
    else:
        print("unknown cmd", cmd)


if __name__ == "__main__":
    main(sys.argv)
