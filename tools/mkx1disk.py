#!/usr/bin/env python3
"""X1 Hu-BASIC 形式の 2D ディスクイメージ生成ツール.

疑似 IPL (xmil/io/defipl.res) は Hu-BASIC フォーマットの 2D ディスクを要求し、
セクタ0 (ファイルオフセット0) の先頭ディレクトリエントリだけを見て、
指定セクタから size バイトを連続リードして load アドレスへ置き exec へ飛ぶ。

実機 Xevious.2d で確認したエントリ構造 (32 バイト):
  +0x00        属性 (0x01 = Bin 必須)
  +0x01..0x0D  ファイル名 13 バイト (スペース 0x20 埋め)
  +0x0E..0x10  拡張子 "Sys" (0x53 0x79 0x73)
  +0x11        0x20 (Xevious と同じパディング)
  +0x12..0x13  サイズ (LE16)
  +0x14..0x15  ロードアドレス (LE16)
  +0x16..0x17  実行アドレス (LE16)
  +0x1E..0x1F  開始セクタ番号 (LE16) = ファイルオフセット / 256

本ツールはプログラム本体を開始セクタ (既定: セクタ1 = オフセット0x100) から
連続配置し、327680 バイト (2D: 全1280セクタ×256) の .2d を出力する。
"""

import argparse
import struct
import sys

SECTOR_SIZE = 256
DISK_SECTORS = 1280           # 2D: 40 track * 2 side * 16 sector
DISK_SIZE = SECTOR_SIZE * DISK_SECTORS   # 327680


def build_direntry(name, size, load, exec_addr, start_sector):
    e = bytearray(32)
    e[0x00] = 0x01                                   # 属性: Bin
    nm = name.encode("ascii", "replace")[:13]
    e[0x01:0x01 + len(nm)] = nm
    for i in range(0x01 + len(nm), 0x0E):            # 名前残りをスペース埋め
        e[i] = 0x20
    e[0x0E:0x11] = b"Sys"                            # 拡張子
    e[0x11] = 0x20                                   # Xevious と同じパディング
    struct.pack_into("<H", e, 0x12, size)
    struct.pack_into("<H", e, 0x14, load)
    struct.pack_into("<H", e, 0x16, exec_addr)
    struct.pack_into("<H", e, 0x1E, start_sector)
    return bytes(e)


def make_disk(program, name, load, exec_addr, start_sector):
    start_off = start_sector * SECTOR_SIZE
    # IPL は size バイトを連続リードするので、最低でも配置分の領域が必要。
    size = len(program)
    if start_off + size > DISK_SIZE:
        raise ValueError("プログラムがディスク容量を超過しています")

    disk = bytearray(DISK_SIZE)
    disk[0x00:0x20] = build_direntry(name, size, load, exec_addr, start_sector)
    disk[start_off:start_off + size] = program
    return bytes(disk)


def main(argv=None):
    ap = argparse.ArgumentParser(description="X1 Hu-BASIC 2D ディスクイメージ生成")
    ap.add_argument("program", help="配置するプレーンバイナリ (.bin)")
    ap.add_argument("-o", "--output", required=True, help="出力 .2d パス")
    ap.add_argument("-n", "--name", default="TZTEST", help="ファイル名 (最大13文字)")
    ap.add_argument("--load", type=lambda s: int(s, 0), default=0x0100,
                    help="ロードアドレス (既定 0x0100)")
    ap.add_argument("--exec", dest="exec_addr", type=lambda s: int(s, 0),
                    default=None, help="実行アドレス (既定: load と同じ)")
    ap.add_argument("--start-sector", type=lambda s: int(s, 0), default=1,
                    help="開始セクタ番号 (既定 1 = オフセット0x100)")
    args = ap.parse_args(argv)

    with open(args.program, "rb") as f:
        program = f.read()

    exec_addr = args.exec_addr if args.exec_addr is not None else args.load
    disk = make_disk(program, args.name, args.load, exec_addr, args.start_sector)

    with open(args.output, "wb") as f:
        f.write(disk)

    print("wrote %s: %d bytes (prog %d bytes @sector%d off 0x%X, load 0x%04X exec 0x%04X)"
          % (args.output, len(disk), len(program), args.start_sector,
             args.start_sector * SECTOR_SIZE, args.load, exec_addr))
    return 0


if __name__ == "__main__":
    sys.exit(main())
