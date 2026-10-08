#!/usr/bin/env python3
"""areaNN_map.bin を 256B 窓 LZSS で圧縮する(ディスク節約)。

allarea_load(init)が Z80 デコーダで EMM へ展開する。後方参照窓=256B なので
Z80 側は 256B リングだけで済み、出力は EMM へ逐次書き(自動+1)、参照はリングを読む。

フォーマット(ストリーム):
  繰り返し: flag(1B, bit0..bit7) + 8 アイテム分のデータ。
    flag bit=0 → リテラル1B(そのまま出力)。
    flag bit=1 → マッチ2B: off(1B)=距離-1(1..256 を 0..255), len(1B)=長さ-MINMATCH(0..)。
                 デコード: 距離 d=off+1、長さ L=len+MINMATCH。output[-d..] から L バイトコピー。
  出力長は固定(12800B)なので、デコーダは 12800B 出力したら終了。終端マーカー不要。

使い方:
  python3 -I tools/lzmap.py compress  <in.bin> <out.lz>
  python3 -I tools/lzmap.py roundtrip <in.bin>          # 圧縮→展開→一致検証
"""
import sys

WINDOW = 256
MINMATCH = 3
MAXMATCH = MINMATCH + 255   # len byte 0..255


def compress(data):
    out = bytearray()
    n = len(data)
    i = 0
    pending = bytearray()
    flag = 0
    fcnt = 0

    def flush():
        nonlocal flag, fcnt, pending
        out.append(flag)
        out.extend(pending)
        flag = 0
        fcnt = 0
        pending = bytearray()

    while i < n:
        best_len = 0
        best_off = 0
        lo = max(0, i - WINDOW)
        # greedy longest match in window
        for s in range(i - 1, lo - 1, -1):
            l = 0
            maxl = min(MAXMATCH, n - i)
            while l < maxl and data[s + l] == data[i + l]:
                l += 1
            if l > best_len:
                best_len = l
                best_off = i - s
                if best_len == MAXMATCH:
                    break
        if best_len >= MINMATCH:
            flag |= (1 << fcnt)
            pending.append(best_off - 1)          # 距離-1 (0..255)
            pending.append(best_len - MINMATCH)   # 長さ-MINMATCH
            i += best_len
        else:
            pending.append(data[i])
            i += 1
        fcnt += 1
        if fcnt == 8:
            flush()
    if fcnt:
        flush()
    return bytes(out)


def decompress(comp, outlen):
    out = bytearray()
    p = 0
    clen = len(comp)
    while len(out) < outlen:
        flag = comp[p]; p += 1
        for b in range(8):
            if len(out) >= outlen:
                break
            if flag & (1 << b):
                off = comp[p]; length = comp[p + 1]; p += 2
                d = off + 1
                L = length + MINMATCH
                start = len(out) - d
                for k in range(L):
                    out.append(out[start + k])
            else:
                out.append(comp[p]); p += 1
    return bytes(out)


def main():
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    cmd = sys.argv[1]
    data = open(sys.argv[2], "rb").read()
    if cmd == "compress":
        c = compress(data)
        open(sys.argv[3], "wb").write(c)
        print(f"{sys.argv[2]}: {len(data)} -> {len(c)} ({100*len(c)//len(data)}%)")
    elif cmd == "roundtrip":
        c = compress(data)
        d = decompress(c, len(data))
        ok = (d == data)
        print(f"{sys.argv[2]}: {len(data)} -> {len(c)} ({100*len(c)//len(data)}%) roundtrip={'OK' if ok else 'FAIL'}")
        if not ok:
            sys.exit(2)
    else:
        print("unknown cmd", cmd); sys.exit(1)


if __name__ == "__main__":
    main()
