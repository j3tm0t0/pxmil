#!/usr/bin/env python3
"""16x16 RGB PNG を X1 自機 PCG のベースデータ (shipdata) に変換する。

出力フォーマット (emmscroll.asm / ship.inc の shipdata と同一):
  16x16 を 8x8 の 2x2 セルに分割。セル順 = TL, TR, BL, BR
    TL=(x0..7,y0..7) TR=(x8..15,y0..7) BL=(x0..7,y8..15) BR=(x8..15,y8..15)
  各セル = 3 プレーン B, R, G の順。各プレーン = 8 ラスタ (y 昇順) の 1 バイト。
  バイトの bit7 = そのラスタの左端ピクセル, bit0 = 右端 (X1 GRAM/PCG と同じ)。
  計 4 セル x 3 プレーン x 8 ラスタ = 96 バイト。

プレーンのビットは各ピクセルのチャンネル値で決まる (B=青, R=赤, G=緑)。
  channel >= --thresh(既定128) で 1。アルファ付き PNG は alpha<128 を透明(全0)。
8 色版 (emmscroll.asm) はこのビットがそのまま色になる。
turboZ 64 色版はテキストパレットで色を付けるので、シルエットを 1 プレーン
(例: 全部 R) に入れておき、色は呼び出し側 (ship.inc の色パラメータ) で選ぶ。

起動時に asm 側 (gen_ship) がこのベースから 0/2/4/6px の 4 シフト版を生成する。
ツールはシフト版を持たない (ベースのみ)。

使い方:
  python3 tools/png2ship.py ship.png            # asm の db ブロックを stdout へ
  python3 tools/png2ship.py ship.png -o out.inc # ファイルへ
  python3 tools/png2ship.py ship.png --bin ship.bin  # 生 96 バイトも出力
"""
import argparse
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("PIL (Pillow) が必要です: pip install Pillow")

PLANES = ("B", "R", "G")          # 出力順
CHANNEL = {"B": 2, "R": 0, "G": 1}  # RGB インデックス
CELLS = (("TL", 0, 0), ("TR", 8, 0), ("BL", 0, 8), ("BR", 8, 8))


def convert(img, thresh):
    """img(16x16 RGBA) -> (bytes96, 行ラベル付きリスト)."""
    px = img.load()
    out = bytearray()
    rows = []   # (label, [8 bytes]) のリスト, db 出力用
    for cname, cx, cy in CELLS:
        for pl in PLANES:
            ch = CHANNEL[pl]
            cellrows = []
            for ry in range(8):
                b = 0
                for rx in range(8):
                    r, g, bl, a = px[cx + rx, cy + ry]
                    on = (a >= 128) and ((r, g, bl)[ch] >= thresh)
                    if on:
                        b |= 0x80 >> rx     # bit7 = 左端
                cellrows.append(b)
            out += bytes(cellrows)
            rows.append(("%s %s" % (cname, pl), cellrows))
    return bytes(out), rows


def emit_db(rows):
    lines = ["shipdata:"]
    for label, cellrows in rows:
        hexb = ",".join("0x%02X" % b for b in cellrows)
        lines.append("\tdb\t%s\t; %s" % (hexb, label))
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description="16x16 PNG -> X1 自機 PCG ベース")
    ap.add_argument("png", help="入力 16x16 PNG")
    ap.add_argument("-o", "--out", help="db ブロック出力先 (既定: stdout)")
    ap.add_argument("--bin", help="生 96 バイトの出力先 (任意)")
    ap.add_argument("--thresh", type=int, default=128, help="チャンネル閾値 (0-255)")
    args = ap.parse_args()

    img = Image.open(args.png).convert("RGBA")
    if img.size != (16, 16):
        sys.exit("入力は 16x16 である必要があります (実際: %dx%d)" % img.size)

    data, rows = convert(img, args.thresh)
    db = emit_db(rows)

    if args.out:
        with open(args.out, "w") as f:
            f.write(db)
        print("wrote %s (%d bytes data)" % (args.out, len(data)), file=sys.stderr)
    else:
        sys.stdout.write(db)

    if args.bin:
        with open(args.bin, "wb") as f:
            f.write(data)
        print("wrote %s (%d bytes)" % (args.bin, len(data)), file=sys.stderr)


if __name__ == "__main__":
    main()
