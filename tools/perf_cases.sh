#!/bin/zsh
# =============================================================================
# perf_cases.sh - XEVALL(ALLAREAS)の正規ケース perf 計測(チーム共通の唯一の手順)
# =============================================================================
# これ以外の方法で測った perf 値は比較に使わないこと(手順差で 9.43% と 14.08% の
# ような食い違いが出るため)。perf 回復の前後比較は必ず本スクリプトの数値で出す。
#
# 使い方:   tools/perf_cases.sh [workdir]
#   workdir 省略時は mktemp の一時ディレクトリを使う(roms/ は書き換えない)。
#
# 計測仕様(固定):
#   - ビルド: ALLAREAS + 全 EXTDATA + SOUND + PERF_NODEATH(自機を死なせず全エリア周回)。
#   - ディスク: common_pal/common_tiles + area01..16(used/map.lz/gobj) + domogram_all.bin(最後)。
#   - 実行 env: SDL_VIDEODRIVER=dummy  XMIL_ROM_TYPE=3(RT3)  XMIL_CYCMUL=256(=4MHz)  XMIL_PROBE=1
#   - 3 ケース: idle(入力なし) / tap(連打 XMIL_AUTOFIRE=FF) / tap+blaster(連打+ブラスター XMIL_AUTOFIRE=BF)
#   - run 長: NFRAMES=56000(16 エリア 1 周 ≒ 55.6k フレーム。boot 後の全エリアを含む)。
#   - metric(dropped = iocore が毎フレーム出す累積ドロップ数, PROBE 1 値/フレーム):
#       tail  = (dropped[-1] - dropped[6000]) / (nframes - 6000) * 100   … 6000 フレーム以降の平均ドロップ率 %
#       worst = max_i (dropped[i+1000] - dropped[i]) / 1000 * 100         … 最も重い 1000 フレーム窓のドロップ率 %
#   前提: roms/arcade/xevious-out/allareas/ と enemies/ のデータ生成済(build-recipes 参照)。
# =============================================================================
# 注: set -e は使わない(run_case の wait ループで benign な非ゼロ終了が中断を招くため)。
#     ビルド失敗だけ明示的に検出して exit する。
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"

W="${1:-$(mktemp -d)}"; mkdir -p "$W"
AA=roms/arcade/xevious-out/allareas
EN=roms/arcade/xevious-out/enemies
AREAS=(01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16)
NFRAMES=56000
TAIL_START=6000
WIN=1000

echo "[perf_cases] workdir=$W  NFRAMES=$NFRAMES"

# --- Domogram データ(bake+combine, 速い。roms/ の enemies/ に出力)---
python3 -I tools/xevi_domogram_bake.py    >/dev/null
python3 -I tools/xevi_domogram_combine.py >/dev/null

# --- map を LZSS 圧縮(workdir へ)---
for n in $AREAS; do
  python3 -I tools/lzmap.py compress "$AA/area${n}_map.bin" "$W/area${n}_map.lz" >/dev/null
done

# --- ビルド(ALLAREAS + PERF_NODEATH)---
DEF=(--define SHIP --define GOBJ_TILEMAP --define ALLAREAS --define SOUND
     --define SHIP_EXTDATA --define RETICLE_EXTDATA --define BLASTER_EXTDATA
     --define ENEMY_EXTDATA --define JARA_EXTDATA --define TORKAN_EXTDATA
     --define GROBDA_EXTDATA --define PERF_NODEATH)
R=$(sjasmplus --raw="$W/perf.bin" "${DEF[@]}" tools/emmscroll64.asm 2>&1 | tail -1)
echo "[perf_cases] build: $R"
case "$R" in *"Errors: 0"*) ;; *) echo "[perf_cases] BUILD FAILED"; exit 1;; esac

# --- ディスク(domogram_all.bin は必ず最後)---
DATA=("$AA/common_pal.bin" "$AA/common_tiles.bin")
for n in $AREAS; do DATA+=("$AA/area${n}_used.bin" "$W/area${n}_map.lz" "$AA/area${n}_gobj.bin"); done
DATA+=("$EN/domogram_all.bin")
python3 tools/mkx1disk.py "$W/perf.bin" -o "$W/perf.2d" -n XEVALL --load 0x0100 --data "${DATA[@]}" >/dev/null

# --- 1 ケース実行(headless, 指定 AUTOFIRE)---
run_case() {
  local tag="$1"
  local af="$2"
  local L="$W/perf_${tag}.log"
  rm -f "$L"
  local -a env_af
  env_af=()
  if [ -n "$af" ]; then env_af=(XMIL_AUTOFIRE="$af"); fi
  env SDL_VIDEODRIVER=dummy XMIL_ROM_TYPE=3 XMIL_CYCMUL=256 XMIL_PROBE=1 "${env_af[@]}" \
      ./xmilsdl2 "$W/perf.2d" > "$L" 2>&1 &
  local pid=$!
  local i n
  for i in $(seq 1 120); do
    n=$(grep -c '^PROBE ' "$L" 2>/dev/null; true); n=${n:-0}
    [ "$n" -ge "$NFRAMES" ] && break
    kill -0 $pid 2>/dev/null || break
    sleep 2
  done
  sleep 1; kill -9 $pid 2>/dev/null || true
  echo "[perf_cases]   ${tag}: $(grep -c '^PROBE ' "$L" 2>/dev/null; true) frames"
}

echo "[perf_cases] running idle / tap(FF) / tap+blaster(BF) ..."
run_case idle ""
run_case tap  FF
run_case both BF

# --- metric 集計 + 表 ---
python3 -I - "$W" "$TAIL_START" "$WIN" <<'PY'
import sys
W, tail_start, win = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
def metrics(path):
    v=[int(l.split()[1]) for l in open(path) if l.startswith('PROBE ')]
    n=len(v)
    if n < tail_start+win: return (n, None, None)
    tail = (v[-1]-v[tail_start])/(n-tail_start)*100
    worst = max(v[i+win]-v[i] for i in range(0, n-win, 50))/win*100
    return (n, tail, worst)
print()
print(f"{'case':18}{'frames':>9}{'tail[%s:]%%'%tail_start:>12}{'worst-%d%%'%win:>12}")
print('-'*51)
for tag,label in [('idle','idle'),('tap','tap(連打)'),('both','tap+blaster')]:
    n,t,w = metrics(f"{W}/perf_{tag}.log")
    if t is None: print(f"{label:18}{n:>9}{'(短すぎ)':>12}")
    else:         print(f"{label:18}{n:>9}{t:>11.2f} {w:>11.1f}")
print()
PY
echo "[perf_cases] done. logs: $W/perf_{idle,tap,both}.log"
