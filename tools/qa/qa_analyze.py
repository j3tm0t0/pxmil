#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""QA 解析ヘルパ(tools/qa/qa_check.sh から呼ばれる)。

PPM フレーム(640x400, P6, RGB565 を 8bit に展開済)を読み、
自動チェック(a)〜(e)の判定とコンタクトシート生成を行う。

重要(環境): このスクリプトは tools/qa/ に置く。python3 で絶対パス起動すると
sys.path[0]=tools/qa になり、scratchpad に植わった dis.py 等を import しない。
numpy は user-site にあるため -I は使わない(-I だと numpy が見えなくなる)。

自機 rect(640x400 座標):
  native 320x200 の col18/row12 が自機 3x3 セル(24x24px)の左上。
  640x400 は native の 2 倍 → x[288,336) y[192,240) の 48x48。
  (ship.inc: SHIP_HX0=18*4, SHIP_VY0=12*4, SHIP_VSTRIDE=3x3)
"""
import sys, os, argparse, glob
import numpy as np
from PIL import Image

# --- 自機 rect(640x400)。実測で Solvalou が収まる範囲(x288-352, y184-232)。
#     実際の自機ピクセルはマスクで抽出するので少し広めでよい。
#     (native col18/row12 由来だが描画原点にオフセットがあり実測でここ)---
SHIP_X0, SHIP_X1 = 288, 352
SHIP_Y0, SHIP_Y1 = 184, 232
MASK_FREQ = 0.85   # play フレーム中この割合以上で同一値 → 自機「安定本体」ピクセル
                   #   (低いと端/地形混入ピクセルが入り、どのフレームとも完全一致しなくなる)
MASK_MIN_PX = 40   # マスク画素がこれ未満なら makeref 失敗
PPM_SIZE = 15 + 640 * 400 * 3   # 完全な 640x400 P6 のバイト数(切り詰め検出用)

# --- 判定しきい値 ---
BLACK_SUM   = 24      # r+g+b <= これ → 黒(RGB565 の最小非ゼロ段差を吸収)
GREY_MAXMIN = 8       # max-min <= これ → 無彩色(グレー)。RGB565 で R!=G!=B になる灰色を許容
DEATH_NONBLACK_MAX = 1300   # 非黒ピクセルがこれ以下(=全画素の0.5%)→「ほぼ全黒(death/READY)」
                            #   クリーンな READY は grey のみで ~400-650px。地形復帰の遷移は除外。
DEATH_CLEAN_FRAC = 0.95     # 全黒フレームのうちクリーン(grey のみ)がこの割合以上で PASS
                            #   (末尾1枚の地形復帰遷移は許容。バグ=全黒中ずっと残る異物は FAIL)
TERRAIN_HTRANS_MAX = 0.42   # 水平隣接ピクセル変化率がこれを超える地形フレーム → ノイズ/崩壊疑い
CHANGE_EPS = 0.5            # 連続フレームの平均絶対差がこれ未満 → 「変化なし」


def load_ppm(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int16)


def frames_in(d):
    """完全な PPM のみ返す(kill 時に書きかけの最終フレームは切り詰められるので除外)。"""
    out = []
    for p in sorted(glob.glob(os.path.join(d, "*.ppm"))):
        try:
            if os.path.getsize(p) == PPM_SIZE:
                out.append(p)
        except OSError:
            pass
    return out


def ship_crop(img):
    return img[SHIP_Y0:SHIP_Y1, SHIP_X0:SHIP_X1, :]


def is_black(img):
    return img.sum(-1) <= BLACK_SUM


def is_grey(img):
    mx = img.max(-1); mn = img.min(-1)
    return (mx - mn) <= GREY_MAXMIN


def nonblack_count(img):
    return int((~is_black(img)).sum())


def nonblack_nongrey_count(img):
    nb = ~is_black(img)
    ng = ~is_grey(img)
    return int((nb & ng).sum())


def htrans_density(img):
    """水平方向の隣接ピクセル「変化」率。地形の平坦さの逆指標。
    画面全体の緑/森でも二重化(2x2)で低く出る。崩壊/チェッカは高い。"""
    g = img.sum(-1)  # HxW
    diff = np.abs(g[:, 1:] - g[:, :-1]) > 24
    return float(diff.mean())


def mean_absdiff(a, b):
    return float(np.abs(a.astype(np.int32) - b.astype(np.int32)).mean())


# ----------------------------------------------------------------------
def _green(img):
    r, g, b = img[..., 0], img[..., 1], img[..., 2]
    return (g > r + 10) & (g > b + 10)


def cmd_makeref(args):
    """play フレーム群から自機本体を抽出し、参照(mode 画像 + マスク)を保存。

    自機 rect(64x64)の各ピクセルについて play 全フレームの最頻 RGB を取る。
    自機本体のピクセルは多数フレームで同一値(freq>=MASK_FREQ)になる。透明部分は
    スクロール地形が透けて値がばらつく → freq 低。さらに緑(地形色)の画素を除いて
    白/赤/灰の distinctive な自機画素だけをマスクにする(地形との誤一致を防ぐ)。"""
    fs = frames_in(args.play_dir)
    if not fs:
        print("makeref: no frames in", args.play_dir); return 2
    crops = np.stack([ship_crop(load_ppm(p)) for p in fs]).astype(np.int64)   # N,H,W,3
    N, H, Wd, _ = crops.shape
    # 各ピクセルの最頻色とその頻度(色をパックして一致数を数える)。
    # int64 必須: int16 だと <<16 がオーバーフローして mode が壊れる。
    packed = (crops[..., 0] << 16) | (crops[..., 1] << 8) | crops[..., 2]  # N,H,W
    mode = np.zeros((H, Wd, 3), np.uint8)
    freq = np.zeros((H, Wd))
    for y in range(H):
        for x in range(Wd):
            vals, cnts = np.unique(packed[:, y, x], return_counts=True)
            k = cnts.argmax()
            v = int(vals[k]); freq[y, x] = cnts[k] / N
            mode[y, x] = [(v >> 16) & 255, (v >> 8) & 255, v & 255]
    mask = (freq >= MASK_FREQ) & (~_green(mode.astype(int)))
    npx = int(mask.sum())
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    Image.fromarray(mode, "RGB").save(args.out)
    mpath = os.path.splitext(args.out)[0] + "_mask.png"
    Image.fromarray((mask * 255).astype(np.uint8), "L").save(mpath)
    with open(args.out + ".txt", "w") as f:
        f.write("rect=%d,%d,%d,%d\n" % (SHIP_X0, SHIP_Y0, SHIP_X1, SHIP_Y1))
        f.write("mask_px=%d\n" % npx)
        f.write("src_commit=%s\n" % (args.commit or "?"))
    print("makeref: %d frames, ship-mask %d px -> %s (+_mask.png)" % (N, npx, args.out))
    return 0 if npx >= MASK_MIN_PX else 3


def _ref_arr(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int16)


def _ref_ok(ref_path):
    mpath = os.path.splitext(ref_path)[0] + "_mask.png"
    return os.path.exists(ref_path) and os.path.exists(mpath)


def _load_ref_mask(ref_path):
    ref = _ref_arr(ref_path)
    mpath = os.path.splitext(ref_path)[0] + "_mask.png"
    mask = np.asarray(Image.open(mpath).convert("L")) > 127
    return ref, mask


def _ship_matches(img, ref, mask):
    """ship rect の mask 画素が ref(mode)と完全一致するか。"""
    crop = ship_crop(img)
    return bool((crop[mask] == ref[mask]).all())


def cmd_checks(args):
    fails = []
    def line(name, ok, detail, skip=False):
        tag = "SKIP" if skip else ("PASS" if ok else "FAIL")
        print("  [%s] %s: %s" % (tag, name, detail))
        if not ok and not skip:
            fails.append(name)

    # ---- (a) scroll-only 中に自機を描かない ----
    if not args.has_scroll_only:
        line("(a) no-ship-in-scroll-only", True,
             "scroll_only 機能がこのコミットに無い(機能未導入)", skip=True)
    elif not _ref_ok(args.ref):
        line("(a) no-ship-in-scroll-only", False, "ref(+mask)が無い: " + args.ref)
    else:
        ref, mask = _load_ref_mask(args.ref)
        fs = frames_in(args.boot_dir)
        checked = matches = 0
        for p in fs:
            img = load_ppm(p)
            if is_black(img).mean() > 0.9:   # boot 中の黒/decode フレームは無視
                continue
            checked += 1
            if _ship_matches(img, ref, mask):
                matches += 1
        if checked == 0:
            line("(a) no-ship-in-scroll-only", False, "scroll-only フレームが取れていない")
        else:
            line("(a) no-ship-in-scroll-only", matches == 0,
                 "scroll-only %d フレーム中, 自機マスク一致は %d(0 が正常)" % (checked, matches))

    # ---- (d) 自機スプライトが参照と pixel 一致 ----
    if not _ref_ok(args.ref):
        line("(d) ship-pixel-match", False, "ref(+mask)が無い: " + args.ref)
    else:
        ref, mask = _load_ref_mask(args.ref)
        fs = frames_in(args.play_dir)
        matches = sum(1 for p in fs if _ship_matches(load_ppm(p), ref, mask))
        ok = matches >= args.ship_k
        line("(d) ship-pixel-match", ok,
             "play %d フレーム中 %d が ref(mask %d px)と完全一致(>=%d で PASS)"
             % (len(fs), matches, int(mask.sum()), args.ship_k))

    # ---- (b) 全黒(死亡)フレームは灰色 READY 以外の非黒ピクセルが 0 ----
    if not args.has_fastdeath:
        line("(b) death-black+ready-only", True,
             "FASTDEATH 機能がこのコミットに無い", skip=True)
    else:
        fs = frames_in(args.death_dir)
        deaths = clean = 0
        worst = 0
        for p in fs:
            img = load_ppm(p)
            if nonblack_count(img) <= DEATH_NONBLACK_MAX:
                deaths += 1
                nn = nonblack_nongrey_count(img)
                worst = max(worst, nn)
                if nn == 0:
                    clean += 1
        frac = clean / deaths if deaths else 0.0
        if deaths == 0:
            line("(b) death-black+ready-only", False, "死亡(ほぼ全黒)フレームが観測されず")
        else:
            line("(b) death-black+ready-only", frac >= DEATH_CLEAN_FRAC,
                 "全黒フレーム %d 個, クリーン(grey のみ)%d 個 = %.0f%%(>=%.0f%% で PASS, worst 異物=%d px)"
                 % (deaths, clean, frac * 100, DEATH_CLEAN_FRAC * 100, worst))

    # ---- (c) 地形サニティ(崩壊/チェッカ/ノイズでない) ----
    cdirs = [("boot", args.boot_dir), ("play", args.play_dir)]
    if args.area2_dir:
        cdirs.append(("area2", args.area2_dir))
    worst_dens = 0.0; worst_tag = ""
    nseen = 0
    for tag, d in cdirs:
        for p in frames_in(d):
            img = load_ppm(p)
            if is_black(img).mean() > 0.5:   # 黒/READY フレームは地形評価対象外
                continue
            dens = htrans_density(img)
            nseen += 1
            if dens > worst_dens:
                worst_dens = dens; worst_tag = tag
    if nseen == 0:
        line("(c) terrain-sanity", False, "地形フレームが取れていない")
    else:
        line("(c) terrain-sanity", worst_dens <= TERRAIN_HTRANS_MAX,
             "最悪 h-trans 密度 %.3f @%s(閾値 %.2f 以下で PASS, 低いほど平坦)"
             % (worst_dens, worst_tag, TERRAIN_HTRANS_MAX))

    # ---- (e) ハングなし / フレームが進む・絵が変わる ----
    e_ok = True; e_detail = []
    for tag, d, want in [("boot", args.boot_dir, args.n_boot),
                         ("play", args.play_dir, args.n_play),
                         ("death", args.death_dir, args.n_death)]:
        if want <= 0:
            continue
        fs = frames_in(d)
        got = len(fs)
        # フレーム番号(stderr PPM ログ)
        fr = parse_frames_log(os.path.join(d, "stderr.log"))
        incr = all(fr[i] < fr[i+1] for i in range(len(fr)-1)) if len(fr) >= 2 else (got >= 1)
        # 連続フレームの変化(黒/READY ペアは除外)
        changing = True
        prev = None
        for p in fs:
            img = load_ppm(p)
            if prev is not None:
                if nonblack_count(img) > DEATH_NONBLACK_MAX and nonblack_count(prev) > DEATH_NONBLACK_MAX:
                    if mean_absdiff(img, prev) < CHANGE_EPS:
                        changing = False
            prev = img
        ok = (got >= max(1, int(want * 0.5))) and incr and changing
        e_ok = e_ok and ok
        e_detail.append("%s:%d/%d frm,incr=%s,chg=%s" % (tag, got, want, incr, changing))
    line("(e) no-hang/advancing", e_ok, "; ".join(e_detail))

    print("  ---- %d FAIL ----" % len(fails))
    return 1 if fails else 0


def parse_frames_log(path):
    out = []
    if not os.path.exists(path):
        return out
    for ln in open(path, errors="replace"):
        if ln.startswith("PPM ") and "frame=" in ln:
            try:
                out.append(int(ln.strip().split("frame=")[1]))
            except ValueError:
                pass
    return out


def cmd_orient(args):
    """向きチェック(area1 boot): 水色(水)が player-view 左半分に偏るか。
    player view = rotate(90,CCW)。元画像の上端(y 小)が左になる。
    ROT180 なら area1 冒頭の海岸が player 左。結果は honest に出力するだけ。"""
    fs = frames_in(args.boot_dir)
    best = None
    for p in fs:
        img = load_ppm(p)
        if is_black(img).mean() > 0.5:
            continue
        r, g, b = img[..., 0], img[..., 1], img[..., 2]
        water = (b > 120) & (b > r + 30) & (b > g + 20)
        if water.sum() < 2000:
            continue
        pv = np.asarray(Image.fromarray(img.astype(np.uint8)).rotate(90, expand=True)).astype(np.int16)
        pr, pg, pb = pv[..., 0], pv[..., 1], pv[..., 2]
        wv = (pb > 120) & (pb > pr + 30) & (pb > pg + 20)
        W = pv.shape[1]
        left = wv[:, :W//2].sum(); right = wv[:, W//2:].sum()
        tot = left + right
        if tot and (best is None or tot > best[0]):
            best = (tot, left, right, p)
    if not best:
        print("  [INFO] orient: 水ピクセルを十分持つ area1 フレーム無し(判定不能)")
        return 0
    tot, left, right, p = best
    frac = left / tot
    print("  [INFO] orient: water left/right = %d/%d (left frac %.2f) @%s"
          % (left, right, frac, os.path.basename(p)))
    print("         参考: ROT180 の area1 は海岸が player 左寄り(frac>0.5)を期待")
    return 0


def cmd_contact(args):
    """各シーン代表フレームを player view で並べたコンタクトシート。"""
    import math
    tiles = []
    for tag, d in args.pairs:
        fs = frames_in(d)
        pick = None
        want_black = "death" in tag.lower()
        for p in fs:
            img = load_ppm(p)
            blk = nonblack_count(img) <= DEATH_NONBLACK_MAX
            if want_black and blk:            # death タイルは全黒+READY を優先
                pick = p; break
            if not want_black and is_black(img).mean() <= 0.5:   # 中身のあるフレーム優先
                pick = p; break
        if pick is None and fs:
            pick = fs[len(fs)//2]
        if pick is None:
            continue
        img = Image.open(pick).convert("RGB").rotate(90, expand=True)
        img.thumbnail((300, 480))
        tiles.append((tag, img))
    if not tiles:
        print("contact: no tiles"); return 2
    cols = len(tiles)
    tw = max(t.width for _, t in tiles)
    th = max(t.height for _, t in tiles)
    pad, lab = 8, 20
    W = cols * (tw + pad) + pad
    H = th + lab + 2 * pad
    sheet = Image.new("RGB", (W, H), (20, 20, 20))
    from PIL import ImageDraw
    dr = ImageDraw.Draw(sheet)
    for i, (tag, t) in enumerate(tiles):
        x = pad + i * (tw + pad)
        sheet.paste(t, (x, lab + pad))
        dr.text((x + 2, 4), tag, fill=(230, 230, 230))
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    sheet.save(args.out)
    print("contact sheet:", args.out)
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    m = sub.add_parser("makeref")
    m.add_argument("--play-dir", required=True)
    m.add_argument("--out", required=True)
    m.add_argument("--commit", default="")
    m.set_defaults(fn=cmd_makeref)

    c = sub.add_parser("checks")
    c.add_argument("--boot-dir", required=True)
    c.add_argument("--play-dir", required=True)
    c.add_argument("--death-dir", required=True)
    c.add_argument("--area2-dir", default="")
    c.add_argument("--ref", required=True)
    c.add_argument("--ship-k", type=int, default=3)
    c.add_argument("--has-scroll-only", type=int, default=1)
    c.add_argument("--has-fastdeath", type=int, default=1)
    c.add_argument("--n-boot", type=int, default=0)
    c.add_argument("--n-play", type=int, default=0)
    c.add_argument("--n-death", type=int, default=0)
    c.set_defaults(fn=cmd_checks)

    o = sub.add_parser("orient")
    o.add_argument("--boot-dir", required=True)
    o.set_defaults(fn=cmd_orient)

    k = sub.add_parser("contact")
    k.add_argument("--pair", action="append", default=[], help="tag=dir")
    k.add_argument("--out", required=True)
    k.set_defaults(fn=lambda a: cmd_contact(_mkpairs(a)))

    args = ap.parse_args()
    # bool 正規化
    if getattr(args, "has_scroll_only", None) is not None:
        args.has_scroll_only = bool(args.has_scroll_only)
    if getattr(args, "has_fastdeath", None) is not None:
        args.has_fastdeath = bool(args.has_fastdeath)
    sys.exit(args.fn(args))


def _mkpairs(a):
    a.pairs = []
    for s in a.pair:
        tag, d = s.split("=", 1)
        a.pairs.append((tag, d))
    return a


if __name__ == "__main__":
    main()
