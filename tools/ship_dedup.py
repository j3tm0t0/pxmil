#!/usr/bin/env python3
"""自機 PCG セル(16版×9=144)の重複除去表 ship_codetab を生成する。

入力: SHIP_CELL_DUMP ビルドの PROBE ログ。各セルは
      marker(0xC000|元code) + 24バイト(B/R/G プレーン×8ラスタ) で出力される。
      元code = SHIP_BASE(0x10) + 版*9 + off、idx = 元code-0x10。

出力: ship.inc に貼る ship_codetab の db 行(16版×9)。

重複除去方針:
  - バイト完全一致セルは必ず同一コードを共有(ロスレス)。
  - 使用可能コードのプールは自機専用に空いている範囲のみ:
        0x10..0x66 (87) と 0x92..0x9F (14) = 計 101。
    (0x67..0x91 は Bacura 常駐、0xA0.. は弾/敵なので使わない)
  - exact-dedup のユニーク数がプールを超える場合のみ、超過分だけ
    「バイト差が最小のパターン対」を貪欲にマージ(近似)。マージした
    対とその距離を表示するので、近似が視覚的に許容範囲か判断できる。
  - 空白(全0)パターンは必ず単一コードにまとめ、非空白と絶対に混ぜない。

使い方:
  python3 -I tools/ship_dedup.py <scd.log>  > /tmp/codetab.txt
"""
import sys

POOL = list(range(0x10, 0x67)) + list(range(0x92, 0xA0))   # 87 + 14 = 101
BLANK = tuple([0] * 24)


def parse_cells(path):
    vals = []
    for ln in open(path):
        if ln.startswith("PROBE "):
            try:
                vals.append(int(ln.split()[1]))
            except ValueError:
                pass
    cells = {}
    i = 0
    while i < len(vals) and len(cells) < 144:
        v = vals[i]
        if 0xC000 <= v <= 0xC0FF:
            idx = (v & 0xFF) - 0x10
            body = tuple(x & 0xFF for x in vals[i + 1:i + 25])
            if 0 <= idx < 144 and len(body) == 24:
                cells.setdefault(idx, body)
            i += 25
        else:
            i += 1
    if len(cells) != 144:
        sys.exit(f"ERROR: 144 セル採取できず ({len(cells)})。ログのフレーム数不足")
    return [cells[idx] for idx in range(144)]


def byte_dist(a, b):
    """2 パターンのバイト差(異なるバイト数)。"""
    return sum(1 for x, y in zip(a, b) if x != y)


def main():
    cells = parse_cells(sys.argv[1])

    # exact-dedup: 出現順(idx順)でユニークパターン列を作る
    uniq = []            # 代表パターン(出現順)
    pat_index = {}       # pattern -> uniq 内の添字
    for b in cells:
        if b not in pat_index:
            pat_index[b] = len(uniq)
            uniq.append(b)
    n = len(uniq)
    print(f"# exact-dedup ユニーク数: {n}  プール: {len(POOL)} (0x10-0x66, 0x92-0x9F)")

    # パターン -> 最終的に割り当てる「代表パターン」。初期は自分自身。
    rep = {b: b for b in uniq}

    if n > len(POOL):
        need = n - len(POOL)
        print(f"# プール超過 {need} 件。最近傍マージを実施(空白は保護):")
        nonblank = [b for b in uniq if b != BLANK]
        # 貪欲: 最小距離の対を need 回マージ
        merged = 0
        active = list(nonblank)
        while merged < need:
            best = None
            for i in range(len(active)):
                for j in range(i + 1, len(active)):
                    d = byte_dist(active[i], active[j])
                    if best is None or d < best[0]:
                        best = (d, i, j)
            d, i, j = best
            # active[j] を active[i] に吸収
            victim = active[j]
            rep[victim] = active[i]
            # victim を指していたものも付け替え
            for k in list(rep):
                if rep[k] == victim:
                    rep[k] = active[i]
            print(f"#   merge dist={d}: パターン吸収 (非空白同士)")
            active.pop(j)
            merged += 1

    # 代表パターンごとにコードを割当(出現順)。空白は最初に出た所で確定。
    repset_order = []
    seen = set()
    for b in cells:
        r = rep[b]
        if r not in seen:
            seen.add(r)
            repset_order.append(r)
    if len(repset_order) > len(POOL):
        sys.exit(f"ERROR: マージ後も {len(repset_order)} > プール {len(POOL)}")
    code_of_rep = {r: POOL[k] for k, r in enumerate(repset_order)}

    codes = [code_of_rep[rep[cells[idx]]] for idx in range(144)]

    # 検証: 同一コードのセルは全てバイト一致(= rep が同じ)であること
    from collections import defaultdict
    g = defaultdict(list)
    for idx in range(144):
        g[codes[idx]].append(idx)
    conflicts = 0
    for c, idxs in g.items():
        reps = {rep[cells[i]] for i in idxs}
        if len(reps) > 1:
            conflicts += 1
    blank_code = code_of_rep.get(rep[BLANK]) if BLANK in rep else None
    print(f"# 使用コード数: {len(code_of_rep)}  衝突(異rep同code): {conflicts}  空白コード: "
          + (f"0x{blank_code:02X}" if blank_code is not None else "なし"))
    # 空白コードに非空白セルが混ざっていないか
    if blank_code is not None:
        bad = [i for i in g[blank_code] if cells[i] != BLANK]
        print(f"# 空白コードに混入した非空白セル: {len(bad)} (0 であること)")

    print()
    print("ship_codetab:")
    for v in range(16):
        row = codes[v * 9:v * 9 + 9]
        print("\tdb\t" + ",".join(f"0x{c:02X}" for c in row) + f"\t; 版{v}")


if __name__ == "__main__":
    main()
