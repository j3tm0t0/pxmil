#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Xevious の UI(スコア/ハイスコア/残機/ロゴ/アトラクト)洗い出し + 前景フォント抽出。

UI はすべて **前景文字レイヤ(fg)** = gfx1(xvi_12.3b)の 8x8 文字。
  fg_videoram(文字コード, MSB 0xC0..) / fg_colorram(色, MSB 0xB0..)。
  BG マップ・スプライトとは別レイヤの最前面オーバーレイ。
確定(tcdev42/re 参照):
  - 数字フォント = fg char **0x10-0x19**('0'-'9')。display_bcd_value が BCD→0x10+桁。
  - スコア P1 = fg offset 0x1B01 から5桁(+末尾0)、P2 = 0x0801。
  - 残機 = Solvalou アイコン **fg char 0x25** を残機数ぶん、offset 0x1B23、色 0x2A。
  - ハイスコア/"1UP"/"HIGH SCORE" ラベル = display_all_scores / display_player_start_msgs。
  - Xevious ロゴ = fg char 群(0x52-0x64 等, display_xevious_logo_flashing)。
  - 著作権 = display_copyright_msgs。
  - アトラクト構成(attract_mode_jump_tbl@0x0C40): [0]demo gameplay [1]title_screen
    [2]demo gameplay [3]high_score_table を循環。
X1(90°回転)再現に必要なもの(洗い出し):
  1. 8x8 フォント: 数字0x10-0x19, Solvalou icon0x25, 英字(ラベル/ロゴ/著作権用)。
  2. レイアウト再設計(縦画面→X1): スコア/ハイスコア上部, 残機アイコン列, エリア表示。
  3. タイトル: ロゴ(fgタイル組み) + 著作権 + "PUSH START/INSERT COIN"。
  4. アトラクト: タイトル→デモ(ゲーム自動進行)→ハイスコア表 の循環。
出力(非コミット, roms/arcade/xevious-out/ui/):
  fgfont.bin   : fg char 0x00-0xFF の 8x8 1bpp(各8バイト)
  fgfont.inc   : 同 db
  fgfont_preview.png (PIL 有時)
  ui_layout.txt: 上記レイアウト/アトラクトの洗い出し
クレジット: tcdev42/re (tcdev/jotd)。ROM/出力は非コミット。
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xevi_extract as X

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "roms", "arcade", "xevious-out", "ui")


def glyph_1bpp(px):
    """8x8 pen -> 8 バイト(1bpp, bit=0x80>>x, pen!=0 を on)。"""
    out = bytearray()
    for y in range(8):
        b = 0
        for x in range(8):
            if px[y][x]:
                b |= (0x80 >> x)
        out.append(b)
    return bytes(out)


def main():
    g1, g2, g3, g4, pr = X.build_regions()
    os.makedirs(OUT, exist_ok=True)
    nchars = len(g1) // 8  # fg char = 8 bytes (1bpp 8x8)
    data = bytearray()
    inc = []
    for n in range(min(nchars, 256)):
        px = X.decode_fg_char(g1, n)
        gb = glyph_1bpp(px)
        data += gb
        if 0x10 <= n <= 0x19 or n == 0x25 or 0x30 <= n <= 0x4A:
            inc.append("\tdb\t%s\t; char 0x%02X" %
                       (",".join("0x%02X" % b for b in gb), n))
    open(os.path.join(OUT, "fgfont.bin"), "wb").write(data)
    open(os.path.join(OUT, "fgfont.inc"), "w").write(
        "; Xevious 前景フォント 8x8 1bpp(抜粋: 数字0x10-19, icon0x25, 英字0x30-4A).\n"
        + "\n".join(inc) + "\n")

    lay = [
        "Xevious UI 洗い出し(X1 縦画面再現用):",
        "- レイヤ: 前景文字(fg) 8x8, gfx1=xvi_12.3b。最前面オーバーレイ。",
        "- 数字フォント: fg char 0x10-0x19 ('0'-'9')。スコアは BCD→0x10+桁。",
        "- スコア表示: P1 fg offset 0x1B01 から5桁(+末尾0=×10), P2 0x0801。",
        "- 残機表示: Solvalou icon fg char 0x25 を残機数ぶん, offset 0x1B23, 色0x2A。",
        "- ハイスコア/ラベル(1UP/HIGH SCORE): display_all_scores 系。",
        "- タイトルロゴ: fg char 群(display_xevious_logo_flashing, 0x52-0x64 等)。",
        "- 著作権: display_copyright_msgs。",
        "- アトラクト循環: title -> demo(自動プレイ) -> high_score_table。",
        "",
        "X1 で必要: 8x8フォント(fgfont.bin=数字/icon/英字), 縦画面向けレイアウト再設計,",
        "ロゴのタイル組み, PUSH START/INSERT COIN, デモ再生, ハイスコア表。",
    ]
    open(os.path.join(OUT, "ui_layout.txt"), "w").write("\n".join(lay) + "\n")

    # preview (optional)
    try:
        from PIL import Image
        cols = 16; rows = (min(nchars, 256) + 15) // 16
        img = Image.new("RGB", (cols * 9, rows * 9), (0, 0, 0))
        pix = img.load()
        for n in range(min(nchars, 256)):
            px = X.decode_fg_char(g1, n)
            ox, oy = (n % 16) * 9, (n // 16) * 9
            for y in range(8):
                for x in range(8):
                    if px[y][x]:
                        pix[ox + x, oy + y] = (255, 255, 255)
        img.save(os.path.join(OUT, "fgfont_preview.png"))
    except Exception:
        pass

    print("\n".join(lay))
    print("\n出力: roms/arcade/xevious-out/ui/ (fgfont.bin/.inc, ui_layout.txt, fgfont_preview.png)")


if __name__ == "__main__":
    main()
