#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
xevi_extract.py - アーケード版ゼビウス (Namco, MAME "xevious" セット) の ROM から
グラフィック (前景文字 / 背景タイル / スプライト) と背景マップを抽出し、
正しいパレットを適用して PNG 出力するツール。

仕様はすべて MAME の src/mame/namco/xevious.cpp に準拠 (推測なし):
  - gfx_layout / GFXDECODE (bgcharlayout, spritelayout_xevious, gfx_8x8x1)
  - xevious_palette()          カラー PROM + ルックアップ PROM の解釈
  - get_bg_tile_info()         背景タイルの code/color/flip 計算
  - xevious_bb_r()             背景マップ生成 (rom 2A/2B/2C)
  - init_xevious()             スプライト plane2 の nibble アンパック

MAME の gfx デコードは MSB-first (readbit = src[n/8] & (0x80 >> (n%8)))。
ROM データは一切コミットしない。出力は roms/arcade/xevious-out/ 。

使い方:
    python3 -P tools/xevi_extract.py            # 全出力
    python3 -P tools/xevi_extract.py --stats    # 数値集計のみ
"""
import os
import sys
import argparse

try:
    from PIL import Image
except ImportError:
    sys.exit("PIL (Pillow) が必要です: pip install Pillow")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ROM_DIR = os.path.join(ROOT, "roms", "arcade", "xevious")
OUT_DIR = os.path.join(ROOT, "roms", "arcade", "xevious-out")


def load(name):
    with open(os.path.join(ROM_DIR, name), "rb") as f:
        return bytearray(f.read())


# ---------------------------------------------------------------------------
# ROM リージョン構築 (MAME ROM_START(xevious) の通り)
# ---------------------------------------------------------------------------
def build_regions():
    # gfx1: 前景文字 (0x1000)
    gfx1 = load("xvi_12.3b")

    # gfx2: 背景タイル B0/B1 (0x2000)
    gfx2 = load("xvi_13.3c") + load("xvi_14.3d")

    # gfx3: スプライト (0xa000)
    gfx3 = bytearray(0xa000)
    gfx3[0x0000:0x2000] = load("xvi_15.4m")   # set#1 plane0/1
    gfx3[0x2000:0x4000] = load("xvi_17.4p")   # set#2 plane0/1
    gfx3[0x4000:0x5000] = load("xvi_16.4n")   # set#3 plane0/1
    gfx3[0x5000:0x7000] = load("xvi_18.4r")   # set#1 plane2 / set#2 plane2
    # 0x9000-0x9fff は ROM_FILL 0x00 (set#3 plane2 = 無し) -> 既に 0
    # init_xevious(): rom[0x5000+i+0x2000] = rom[0x5000+i] >> 4  (i=0..0x1fff)
    base = 0x5000
    for i in range(0x2000):
        gfx3[base + i + 0x2000] = gfx3[base + i] >> 4

    # gfx4: 背景マップ生成 ROM 2A/2B/2C (0x4000)
    gfx4 = bytearray(0x4000)
    gfx4[0x0000:0x1000] = load("xvi_9.2a")    # rom2a
    gfx4[0x1000:0x3000] = load("xvi_10.2b")   # rom2b
    gfx4[0x3000:0x4000] = load("xvi_11.2c")   # rom2c

    # proms: RGB(各256) + bg lut(各512) + sprite lut(各512)
    proms = (load("xvi-8.6a") + load("xvi-9.6d") + load("xvi-10.6e")
             + load("xvi-7.4h") + load("xvi-6.4f")
             + load("xvi-4.3l") + load("xvi-5.3m"))
    return gfx1, gfx2, gfx3, gfx4, proms


# ---------------------------------------------------------------------------
# パレット (xevious_palette)
# ---------------------------------------------------------------------------
def build_palette(proms):
    """戻り値:
        rgb[0..0x80]     : 間接カラー (0x80 = 透明マーカー, 黒)
        bg_pen[0..511]   : 背景 lut -> rgb index
        sp_pen[0..511]   : スプライト lut -> rgb index (0x80=透明)
        fg_pen[0..127]   : 前景 lut -> rgb index (0x80=透明)
    """
    W = (0x0e, 0x1f, 0x43, 0x8f)  # 2.2k,1k,470,220 ohm の重み (bit0..3)

    def comp(byte):
        return (W[0] * (byte & 1) + W[1] * ((byte >> 1) & 1)
                + W[2] * ((byte >> 2) & 1) + W[3] * ((byte >> 3) & 1))

    rgb = [(0, 0, 0)] * 0x81
    red, grn, blu = proms[0:256], proms[256:512], proms[512:768]
    for i in range(128):
        rgb[i] = (comp(red[i]), comp(grn[i]), comp(blu[i]))
    rgb[0x80] = (0, 0, 0)  # スプライト透明マーカー

    p = 0x300  # RGB(128使用) + 未使用128 + 256*2 = 0x300 (bg lut 先頭 = xvi-7.4h)
    bg_low = proms[p:p + 512]
    bg_high = proms[p + 512:p + 1024]
    bg_pen = [(bg_low[i] & 0x0f) | ((bg_high[i] & 0x0f) << 4) for i in range(512)]

    p += 1024  # sprite lut 先頭 = xvi-4.3l
    sp_low = proms[p:p + 512]
    sp_high = proms[p + 512:p + 1024]
    sp_pen = []
    for i in range(512):
        c = (sp_low[i] & 0x0f) | ((sp_high[i] & 0x0f) << 4)
        sp_pen.append((c & 0x7f) if (c & 0x80) else 0x80)

    fg_pen = [((i >> 1) if (i & 1) else 0x80) for i in range(128)]
    return rgb, bg_pen, sp_pen, fg_pen


# ---------------------------------------------------------------------------
# gfx デコード (MSB-first)
# ---------------------------------------------------------------------------
def decode_fg_char(gfx1, n):
    """前景文字 8x8 1bpp -> pixel値(0/1) の 8x8 リスト"""
    px = [[0] * 8 for _ in range(8)]
    b = n * 8
    for y in range(8):
        byte = gfx1[b + y]
        for x in range(8):
            px[y][x] = (byte >> (7 - x)) & 1
    return px


def decode_bg_tile(gfx2, n):
    """背景タイル 8x8 2bpp -> pixel値(0..3). plane0(高位)=B0, plane1(低位)=B1"""
    half = 0x1000
    px = [[0] * 8 for _ in range(8)]
    b = n * 8  # charincrement 64bit = 8byte
    for y in range(8):
        b0 = gfx2[b + y]          # plane0 (MSB, pixel bit1)
        b1 = gfx2[half + b + y]   # plane1 (pixel bit0)
        for x in range(8):
            s = 7 - x
            px[y][x] = (((b0 >> s) & 1) << 1) | ((b1 >> s) & 1)
    return px


# スプライト xoffset: STEP4(0,1),STEP4(8*8,1),STEP4(16*8,1),STEP4(24*8,1)
_SP_XOFF = [g * 64 + i for g in range(4) for i in range(4)]
# スプライト yoffset: STEP8(0,8), STEP8(32*8,8)
_SP_YOFF = [y * 8 for y in range(8)] + [256 + y * 8 for y in range(8)]
_SP_FRAC = 0x5000 * 8  # RGN_FRAC(1,2) in bits
# planeoffset = { RGN_FRAC(1,2)+4, 0, 4 } : [bit2(MSB), bit1, bit0]
_SP_PLANE = (_SP_FRAC + 4, 0, 4)


def decode_sprite(gfx3, n):
    """スプライト 16x16 3bpp -> pixel値(0..7) の 16x16 リスト"""
    px = [[0] * 16 for _ in range(16)]
    base = n * 64 * 8  # charincrement = 64*8 bit
    for y in range(16):
        yo = _SP_YOFF[y]
        for x in range(16):
            o = base + yo + _SP_XOFF[x]
            val = 0
            for pi, po in enumerate(_SP_PLANE):  # pi=0->bit2,1->bit1,2->bit0
                bit = o + po
                b = (gfx3[bit >> 3] >> (7 - (bit & 7))) & 1
                val |= b << (2 - pi)
            px[y][x] = val
    return px


# ---------------------------------------------------------------------------
# 背景マップ生成 (xevious_bb_r)
# ---------------------------------------------------------------------------
def bb_read(gfx4, bs0, bs1, odd):
    rom2a = gfx4            # +0
    rom2b_ofs = 0x1000
    rom2c_ofs = 0x3000
    adr_2b = ((bs1 & 0x7e) << 6) | ((bs0 & 0xfe) >> 1)
    if adr_2b & 1:
        dat1 = ((rom2a[adr_2b >> 1] & 0xf0) << 4) | gfx4[rom2b_ofs + adr_2b]
    else:
        dat1 = ((rom2a[adr_2b >> 1] & 0x0f) << 8) | gfx4[rom2b_ofs + adr_2b]
    adr_2c = ((dat1 & 0x1ff) << 2) | ((bs1 & 1) << 1) | (bs0 & 1)
    if dat1 & 0x400:
        adr_2c ^= 1
    if dat1 & 0x200:
        adr_2c ^= 2
    if odd:  # BB1 (-> videoram / tile code)
        return gfx4[rom2c_ofs + (adr_2c | 0x800)]
    # BB0 (-> colorram / attribute, flip を XOR 済み)
    dat2 = gfx4[rom2c_ofs + adr_2c]
    # swap bit6 & 7
    dat2 = (dat2 & 0x3f) | ((dat2 & 0x40) << 1 & 0x80) | ((dat2 & 0x80) >> 1 & 0x40)
    if dat1 & 0x400:
        dat2 ^= 0x40
    if dat1 & 0x200:
        dat2 ^= 0x80
    return dat2


def bg_cell(gfx4, bs0, bs1):
    """(code, color, flipx, flipy) を返す。get_bg_tile_info と同一。

    bb_r が返す 2 値の行き先はプログラム (xvi_5.3f @0x303-0x316) で確定:
        BB0 (offset even) -> colorram 0xb8xx = attribute
        BB1 (offset odd)  -> videoram 0xc8xx = tile name (code 下位8bit)
    (BB0 に flip が XOR 済みで、get_bg_tile_info が colorram bit6/7 を
     flip として読むため、この対応でのみ整合する。)
    """
    name = bb_read(gfx4, bs0, bs1, odd=True)    # BB1 -> code
    attr = bb_read(gfx4, bs0, bs1, odd=False)   # BB0 -> attribute
    code = name + ((attr & 0x01) << 8)
    color = ((attr & 0x3c) >> 2) | ((code & 0x80) >> 3) | ((attr & 0x03) << 5)
    flipx = bool(attr & 0x40)
    flipy = bool(attr & 0x80)
    return code, color, flipx, flipy


# ---------------------------------------------------------------------------
# 画像ユーティリティ
# ---------------------------------------------------------------------------
def gray_ramp(maxval):
    return [(v * 255 // maxval,) * 3 for v in range(maxval + 1)]


def put_tile(img, ox, oy, px, palette, w, h, flipx=False, flipy=False,
             transparent=None):
    pix = img.load()
    for y in range(h):
        sy = (h - 1 - y) if flipy else y
        for x in range(w):
            sx = (w - 1 - x) if flipx else x
            v = px[sy][sx]
            if transparent is not None and v == transparent:
                continue
            pix[ox + x, oy + y] = palette[v]


# ---------------------------------------------------------------------------
# 出力 1: gfx シート
# ---------------------------------------------------------------------------
def out_fg_chars(gfx1):
    n = len(gfx1) // 8  # 512
    cols, cell = 32, 9
    rows = (n + cols - 1) // cols
    img = Image.new("RGB", (cols * cell, rows * cell), (40, 40, 48))
    pal = [(0, 0, 0), (255, 255, 255)]
    for i in range(n):
        px = decode_fg_char(gfx1, i)
        put_tile(img, (i % cols) * cell, (i // cols) * cell, px, pal, 8, 8)
    img = img.resize((img.width * 3, img.height * 3), Image.NEAREST)
    img.save(os.path.join(OUT_DIR, "fg_chars.png"))
    return n


def out_bg_tiles(gfx2, rgb, bg_pen, tile_colorset):
    n = len(gfx2) // 16  # 512 (2bpp: 16byte/tile)
    cols, cell = 32, 9
    rows = (n + cols - 1) // cols
    ramp = gray_ramp(3)
    gray = Image.new("RGB", (cols * cell, rows * cell), (40, 40, 48))
    color = Image.new("RGB", (cols * cell, rows * cell), (40, 40, 48))
    for i in range(n):
        px = decode_bg_tile(gfx2, i)
        ox, oy = (i % cols) * cell, (i // cols) * cell
        put_tile(gray, ox, oy, px, ramp, 8, 8)
        cs = tile_colorset.get(i, 0)
        pal = [rgb[bg_pen[cs * 4 + v]] for v in range(4)]
        put_tile(color, ox, oy, px, pal, 8, 8)
    for im, nm in ((gray, "bg_tiles_gray.png"), (color, "bg_tiles_color.png")):
        im = im.resize((im.width * 3, im.height * 3), Image.NEAREST)
        im.save(os.path.join(OUT_DIR, nm))
    return n


def out_sprites(gfx3, rgb, sp_pen):
    n = 320  # RGN_FRAC(1,2)=0x5000, 64byte/sprite
    cols, cell = 16, 17
    rows = (n + cols - 1) // cols
    ramp = gray_ramp(7)
    gray = Image.new("RGB", (cols * cell, rows * cell), (40, 40, 48))
    color = Image.new("RGBA", (cols * cell, rows * cell), (0, 0, 0, 0))
    for i in range(n):
        px = decode_sprite(gfx3, i)
        ox, oy = (i % cols) * cell, (i // cols) * cell
        put_tile(gray, ox, oy, px, ramp, 16, 16)
        # 代表色: 非透明ピクセルの異なる色数が最大となる color set を選ぶ
        best_cs, best_score = 0, -1
        for cs in range(64):
            seen = set()
            for row in px:
                for v in row:
                    pen = sp_pen[cs * 8 + v]
                    if pen != 0x80:
                        seen.add(pen)
            if len(seen) > best_score:
                best_score, best_cs = len(seen), cs
        pal = []
        for v in range(8):
            pen = sp_pen[best_cs * 8 + v]
            pal.append((0, 0, 0, 0) if pen == 0x80 else (*rgb[pen], 255))
        pix = color.load()
        for y in range(16):
            for x in range(16):
                c = pal[px[y][x]]
                if c[3]:
                    pix[ox + x, oy + y] = c
    g = gray.resize((gray.width * 3, gray.height * 3), Image.NEAREST)
    g.save(os.path.join(OUT_DIR, "sprites_gray.png"))
    c = color.resize((color.width * 3, color.height * 3), Image.NEAREST)
    c.save(os.path.join(OUT_DIR, "sprites_color.png"))
    return n


def out_palette(rgb):
    cell = 16
    img = Image.new("RGB", (16 * cell, 8 * cell + cell), (30, 30, 30))
    pix = img.load()
    for i in range(128):
        ox, oy = (i % 16) * cell, (i // 16) * cell
        for y in range(cell):
            for x in range(cell):
                pix[ox + x, oy + y] = rgb[i]
    img.save(os.path.join(OUT_DIR, "palette.png"))


# ---------------------------------------------------------------------------
# 出力 2: 背景マップ全体 (2048 x 1024, native)。bs0=水平(進行軸), bs1=横断
# ---------------------------------------------------------------------------
MAP_W = 256   # bs0 0..255  -> 8x8 セル 256 列
MAP_H = 128   # bs1 0..127  -> 8x8 セル 128 行


def render_map(gfx2, gfx4, rgb, bg_pen):
    """native 向き (bs0=X 水平) の背景マップ画像と、タイル使用統計を返す。"""
    img = Image.new("RGB", (MAP_W * 8, MAP_H * 8), (0, 0, 0))
    tile_cache = {}
    used_codes = set()
    colorset_count = {}      # code -> {colorset: n}
    for bs1 in range(MAP_H):
        for bs0 in range(MAP_W):
            code, color, fx, fy = bg_cell(gfx4, bs0, bs1)
            used_codes.add(code)
            d = colorset_count.setdefault(code, {})
            d[color] = d.get(color, 0) + 1
            key = (code, color, fx, fy)
            tpx = tile_cache.get(key)
            if tpx is None:
                raw = decode_bg_tile(gfx2, code)
                pal = [rgb[bg_pen[color * 4 + v]] for v in range(4)]
                t = Image.new("RGB", (8, 8))
                tp = t.load()
                for y in range(8):
                    sy = 7 - y if fy else y
                    for x in range(8):
                        sx = 7 - x if fx else x
                        tp[x, y] = pal[raw[sy][sx]]
                tpx = t
                tile_cache[key] = t
            img.paste(tpx, (bs0 * 8, bs1 * 8))
    # 最頻 colorset (タイルシート用)
    tile_colorset = {c: max(d, key=d.get) for c, d in colorset_count.items()}
    return img, used_codes, tile_colorset


# ---------------------------------------------------------------------------
# 出力 3: X1 8色 減色 (デジタル RGB 各1bit)
# ---------------------------------------------------------------------------
def _nearest8(r, g, b):
    return (255 if r >= 128 else 0, 255 if g >= 128 else 0, 255 if b >= 128 else 0)


def reduce_x1_nearest(src):
    out = Image.new("RGB", src.size)
    sp, op = src.load(), out.load()
    for y in range(src.height):
        for x in range(src.width):
            op[x, y] = _nearest8(*sp[x, y][:3])
    return out


def out_map_grid(rot_img):
    """回転後マップに 16タイル(128px)ごとのナビ用グリッドを重ねる。
    (エリア境界ではない。あくまで座標読み取り用。)"""
    img = rot_img.convert("RGB").copy()
    pix = img.load()
    w, h = img.size
    step = 128  # 16 tiles
    for x in range(0, w, step):
        for y in range(h):
            pix[min(x, w - 1), y] = (80, 80, 255)
    for y in range(0, h, step):
        for x in range(w):
            pix[x, min(y, h - 1)] = (80, 80, 255)
    img.save(os.path.join(OUT_DIR, "map_arcade_grid.png"))


def out_width_compare(rot_img):
    """幅合わせ比較: アーケード可視幅 224(28タイル) を X1 縦画面幅 200 へ。
    回転後マップから 224px 幅のサンプル縦ストリップを取り、
    [原寸224 / 8色224 / 中央切出し200・8色 / 縮小200・8色] を並べる。"""
    H = 640
    x0 = 400
    strip = rot_img.convert("RGB").crop((x0, 0, x0 + 224, H))
    a = strip
    b = reduce_x1_nearest(strip)
    crop200 = strip.crop((12, 0, 212, H))      # 中央 200px 切出し
    c = reduce_x1_nearest(crop200)
    scaled = strip.resize((200, H), Image.BILINEAR)  # 224->200 縮小
    d = reduce_x1_nearest(scaled)
    gap = 12
    widths = [224, 224, 200, 200]
    imgs = [a, b, c, d]
    labels = ["orig 224", "8col 224", "crop 200", "scale 200"]
    total = sum(widths) + gap * (len(imgs) + 1)
    out = Image.new("RGB", (total, H + 20), (30, 30, 30))
    from PIL import ImageDraw
    dr = ImageDraw.Draw(out)
    x = gap
    for im, lb, wd in zip(imgs, labels, widths):
        out.paste(im, (x, 16))
        dr.text((x, 2), lb, fill=(255, 255, 255))
        x += wd + gap
    out.save(os.path.join(OUT_DIR, "x1_width_compare.png"))


def reduce_x1_dither(src):
    # Floyd-Steinberg を各チャンネル 1bit へ
    w, h = src.size
    sp = src.load()
    buf = [[list(sp[x, y][:3]) for x in range(w)] for y in range(h)]
    out = Image.new("RGB", src.size)
    op = out.load()
    for y in range(h):
        for x in range(w):
            old = buf[y][x]
            new = [255 if old[c] >= 128 else 0 for c in range(3)]
            op[x, y] = tuple(new)
            err = [old[c] - new[c] for c in range(3)]
            for dx, dy, f in ((1, 0, 7), (-1, 1, 3), (0, 1, 5), (1, 1, 1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    for c in range(3):
                        buf[ny][nx][c] += err[c] * f / 16
    return out


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stats", action="store_true", help="数値集計のみ")
    args = ap.parse_args()

    os.makedirs(OUT_DIR, exist_ok=True)
    gfx1, gfx2, gfx3, gfx4, proms = build_regions()
    rgb, bg_pen, sp_pen, fg_pen = build_palette(proms)

    # bg_pen の健全性チェック (間接カラーは 0..0x80 のはず)
    bad = [p for p in bg_pen if p > 0x80]
    if bad:
        print("WARN: bg_pen に 0x80 超の値: %d 件 (max=0x%x)" % (len(bad), max(bad)))

    # マップ (タイル使用統計も取得)
    map_img, used_codes, tile_colorset = render_map(gfx2, gfx4, rgb, bg_pen)

    if not args.stats:
        nfg = out_fg_chars(gfx1)
        nbg = out_bg_tiles(gfx2, rgb, bg_pen, tile_colorset)
        nsp = out_sprites(gfx3, rgb, sp_pen)
        out_palette(rgb)

        # native マップ
        map_img.save(os.path.join(OUT_DIR, "map_native.png"))
        # アーケード向き: ROT90 (時計回り)。bs0(水平) -> 画面縦。
        rot = map_img.rotate(-90, expand=True)
        rot.save(os.path.join(OUT_DIR, "map_arcade_rot90.png"))
        out_map_grid(rot)
        out_width_compare(rot)

        # X1 減色 (native マップを対象)
        reduce_x1_nearest(map_img).save(
            os.path.join(OUT_DIR, "map_x1_nearest.png"))
        reduce_x1_dither(map_img).save(
            os.path.join(OUT_DIR, "map_x1_dither.png"))
        print("PNG 出力完了: fg=%d bg=%d sprites=%d" % (nfg, nbg, nsp))

    # ---- 数値集計 ----
    # 背景で実際に使われる色 (bg_pen 経由の間接カラー) を集計
    used_colorsets = {}
    bg_colors_used = set()
    for bs1 in range(MAP_H):
        for bs0 in range(MAP_W):
            code, color, _, _ = bg_cell(gfx4, bs0, bs1)
            used_colorsets[color] = used_colorsets.get(color, 0) + 1
            for v in range(4):
                bg_colors_used.add(rgb[bg_pen[color * 4 + v]])
    map_dots_long = MAP_W * 8  # bs0 軸 (進行方向) のドット数
    map_dots_across = MAP_H * 8
    # EMM 4ページずらし概算: マップを X1 の 8色/ドット (3bpp) で保持と仮定。
    # X1 GRAM は 1ドット=3bit (3プレーン)。横断幅は可視 200 ドットに合わせる想定。
    # 2ドット単位スクロールの 4ページ = 同一データを 0/2/4/6 ドットずらして 4 版持つ。
    # 90度回転後、進行軸(bs0=2048dot)が X1 ネイティブの水平=GRAM バイト方向。
    # GRAM: 1ライン = 進行軸 2048dot/8 = 256byte、横断 200ライン、3プレーン。
    across = 200
    bytes_1page = (map_dots_long // 8) * across * 3  # 256byte * 200line * 3plane
    emm_4page = bytes_1page * 4

    print("---- 数値集計 ----")
    print("背景タイル種類数 (使用 code): %d / 全 %d"
          % (len(used_codes), len(gfx2) // 16))
    print("背景 color code 使用数: %d / 128 (6bit color + code bit8)"
          % len(used_colorsets))
    print("背景マップで実際に出る色数 (RGB ユニーク): %d" % len(bg_colors_used))
    print("マップ寸法 (native): %d x %d dot (進行軸 %d x 横断 %d)"
          % (map_dots_long, map_dots_across, map_dots_long, map_dots_across))
    print("  = %d x %d タイル(8x8)" % (MAP_W, MAP_H))
    print("可視横断幅 224dot=28タイル, X1縦画面幅想定 %d dot" % across)
    print("EMM 概算: 1ページ %d byte (進行軸%d/8 byte * 横断%d line * 3plane), "
          "4ページ %d byte (%.1f KB)"
          % (bytes_1page, map_dots_long, across, emm_4page, emm_4page / 1024))


if __name__ == "__main__":
    main()
