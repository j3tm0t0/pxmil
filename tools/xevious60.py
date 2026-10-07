#!/usr/bin/env python3
"""X1 版ゼビウスの 60fps 化パッチ。

Xevious.2d (320KB 2D フラットイメージ) の速度調整ルーチンを VBLANK
エッジ待ちに置き換えた Xevious60.2d を生成する。

背景:
  ゲーム本編のメインループ (RAM 0x017e-0x01b2) はフリーランで、唯一の
  速度調整は 0x01a0 から呼ばれる RAM 0x03c9 のルーチン (敵スロットの
  空き数 C を数えて 26*C サイクルのカウントダウンで待つ負荷平準化遅延。
  実測 4MHz ゲーム中 約 75 周/秒)。CPU クロックを上げると遅延も比例して
  縮むため、ゲーム速度も音楽テンポも全部速くなる。

パッチ:
  0x03c9 の遅延ルーチン本体 (ディスク上 0x4ac9) を 14 バイトの
  「VBLANK 立ち上がりエッジ待ち」で上書きする。8255 PPI ポート B
  (I/O 0x1a01) の bit7 は表示期間=1 / ブランキング=0。表示期間になる
  まで待ち、次にブランキングになるまで待つことで、呼び出しごとに
  ちょうど 1 回フレーム境界へ同期する (ゲーム自身がラスタ処理で使って
  いる 0x2cce のイディオムと同一)。

        03c9: 01 01 1a    LD   BC,0x1a01
        03cc: ed 78       IN   A,(C)
        03ce: f2 cc 03    JP   P,0x03cc   ; bit7=1 (表示期間) まで待つ
        03d1: ed 78       IN   A,(C)
        03d3: fa d1 03    JP   M,0x03d1   ; bit7=0 (ブランキング) まで待つ
        03d6: c9          RET

  元ルーチンは A/BC/DE/HL を全部壊すので、A/BC/F だけ使うスタブに
  退避は不要。旧本体の残り (0x03d7-0x043b) は到達不能のデッドコードに
  なる (ゲーム中の進入経路が 0x01a0 の CALL だけであることは PC トレース
  で実測確認済み)。

効果:
  メインループが 60Hz (フレームレート) に固定される。CPU クロックを
  6/8MHz に上げても速度・音楽テンポは変わらず、処理落ちだけが減る。

使い方:
  python3 tools/xevious60.py [入力 .2d] [出力 .2d]
  (省略時: roms/Xevious.2d -> roms/Xevious60.2d)
"""

import sys
import os

IMAGE_SIZE = 327680				# 2D: 40 トラック x 両面 x 16 セクタ x 256B

PATCH_OFF = 0x4AC9				# ディスク上オフセット (RAM 0x03c9 に相当)

# パッチ位置の前後 48 バイト (0x4ab9-0x4ae8)。イメージの同定と、
# パッチ位置がずれていないことの検証に使う。
CONTEXT_OFF = 0x4AB9
CONTEXT = bytes.fromhex(
	"2100f801ff031e0073230b78b120f9c9"	# 03b9: テーブルクリア (LD HL,0xf800 ...)
	"21b0f911100006080e007efe0020"		# 03c9: 遅延ルーチン先頭 (パッチ対象)
	"010c1910f72160f879c606477efe0020"	# 03d7: 遅延ルーチン続き
	"340c")

PATCH = bytes.fromhex("01011aed78f2cc03ed78fad103c9")

assert len(PATCH) == 14
assert CONTEXT[PATCH_OFF - CONTEXT_OFF:PATCH_OFF - CONTEXT_OFF + len(PATCH)] \
		== bytes.fromhex("21b0f911100006080e007efe0020")


def main():
	src = sys.argv[1] if len(sys.argv) > 1 else "roms/Xevious.2d"
	dst = sys.argv[2] if len(sys.argv) > 2 else "roms/Xevious60.2d"

	with open(src, "rb") as f:
		img = bytearray(f.read())

	if len(img) != IMAGE_SIZE:
		sys.exit(f"{src}: サイズが {len(img)} (期待 {IMAGE_SIZE})。"
				 " 2D フラットイメージではない")

	if img[CONTEXT_OFF:CONTEXT_OFF + len(CONTEXT)] != CONTEXT:
		if img.find(PATCH) != -1:
			sys.exit(f"{src}: パッチ適用済みに見える (出力をそのまま使える)")
		sys.exit(f"{src}: 0x{CONTEXT_OFF:04x} の内容が想定と一致しない。"
				 " 別バージョンのイメージ?")

	if img.find(CONTEXT, CONTEXT_OFF + 1) != -1:
		sys.exit(f"{src}: 検証パターンが複数箇所にある。オフセット特定不能")

	img[PATCH_OFF:PATCH_OFF + len(PATCH)] = PATCH

	with open(dst, "wb") as f:
		f.write(img)
	print(f"{dst}: 0x{PATCH_OFF:04x} に {len(PATCH)} バイトのパッチを適用した")


if __name__ == "__main__":
	main()
