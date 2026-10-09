#!/bin/zsh
# =============================================================================
# qa_check.sh - 縦画面ゼビウス(X1turboZ, 64色/RT3)の自動 QA / 回帰ハーネス
# =============================================================================
# あるコミット(既定 HEAD)について:
#   1) クリーンな git worktree で production ディスクをビルドし、ディスクと
#      全データ入力の md5 を出力する(ビルド時データレース混入の検出用)。
#   2) headless エミュレータで複数シナリオを実行(boot/scroll-only, AUTOFIRE プレイ,
#      START_AREA=2, 死亡シーケンス)。
#   3) 自動チェック(a)〜(e)を PASS/FAIL 判定。
#   4) player view のコンタクトシート PNG を出力。
#
# 使い方:  tools/qa/qa_check.sh [commit]   (既定 HEAD)
#   環境変数:
#     QA_OUT        出力先(既定 = scratchpad/qa)
#     QA_SCRATCH    worktree 等の作業ルート(既定 = scratchpad)
#     QA_MAKEREF=1  自機参照 crop を今回ビルドから作り直す
#     QA_CYCMUL     既定 256 (=4MHz 相当, headless 高速)
#
# 注: roms/ は gitignored(ROM 抽出データ)。worktree に symlink して使い、
#     ディスクや ref は roms/ 配下(= 非コミット)に置く。本スクリプト自身と
#     tools/qa/ のヘルパだけがコミット対象。
# =============================================================================
set -u
SELF="${0:A}"
QADIR="${SELF:h}"              # tools/qa
MAIN="${QADIR:h:h}"           # repo root
export PATH="$HOME/.local/bin:$PATH"

COMMIT="${1:-HEAD}"
SCRATCH_DEFAULT="/private/tmp/claude-501/-Users-moto-Dropbox-src-github-com-j3tm0t0-pxmil/25870b4b-da1a-4c35-b0a3-704d290ea02e/scratchpad"
QA_SCRATCH="${QA_SCRATCH:-$SCRATCH_DEFAULT}"
QA_OUT="${QA_OUT:-$QA_SCRATCH/qa}"
CYCMUL="${QA_CYCMUL:-256}"
QA_MAKEREF="${QA_MAKEREF:-0}"
# numpy/PIL を持つ python3 を明示検出(非対話 zsh では mise 未activeで
# 既定 python3 が numpy 無しになることがある)。
PYBIN="$(command -v python3 2>/dev/null)"
if ! "$PYBIN" -c 'import numpy, PIL' >/dev/null 2>&1; then
  for cand in \
      "$HOME/.local/share/mise/installs/python/3.14/bin/python3" \
      python3.14 python3.13 python3.12 python3; do
    c="$(command -v "$cand" 2>/dev/null)"
    if [ -n "$c" ] && "$c" -c 'import numpy, PIL' >/dev/null 2>&1; then PYBIN="$c"; break; fi
  done
fi
"$PYBIN" -c 'import numpy, PIL' >/dev/null 2>&1 || { echo "[qa] FATAL: numpy/PIL を持つ python3 が見つからない"; exit 1; }
PY=("$PYBIN" "$QADIR/qa_analyze.py")
EMU="$MAIN/xmilsdl2"
REFDIR="$MAIN/roms/qa_ref"
REF="$REFDIR/ship_ref.png"

mkdir -p "$QA_OUT" "$REFDIR"

HASH=$(git -C "$MAIN" rev-parse --short "$COMMIT") || { echo "bad commit: $COMMIT"; exit 1; }
OUTH="$QA_OUT/$HASH"; mkdir -p "$OUTH"    # コミット毎に分離(前コミットのフレームを潰さない)
WT="$QA_SCRATCH/qa_wt_$$"
echo "================================================================"
echo "[qa] commit=$COMMIT ($HASH)  worktree=$WT"
echo "[qa] out=$QA_OUT"
echo "================================================================"

cleanup() { git -C "$MAIN" worktree remove --force "$WT" >/dev/null 2>&1; }
trap cleanup EXIT INT TERM

git -C "$MAIN" worktree add --detach "$WT" "$HASH" >/dev/null 2>&1 || { echo "worktree add failed"; exit 1; }
ln -sfn "$MAIN/roms" "$WT/roms"      # incbin(sound/*.bin, *_rot180.inc) + データ参照

AA="$MAIN/roms/arcade/xevious-out/allareas_rot180"
EN="$MAIN/roms/arcade/xevious-out/enemies_rot180"
AREAS=(01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16)
W="$WT/_qa"; mkdir -p "$W"

# ---- 機能(define)検出: 古いコミットでは no-op のものを SKIP 扱いにする ----
HAS_SCROLL_ONLY=$(grep -c 'scroll_only' "$WT/tools/sprite.inc" 2>/dev/null || echo 0)
HAS_FASTDEATH=$(grep -c 'FASTDEATH' "$WT/tools/emmscroll64.asm" 2>/dev/null || echo 0)
HAS_STARTAREA=$(grep -c 'START_AREA' "$WT/tools/emmscroll64.asm" 2>/dev/null || echo 0)
[ "$HAS_SCROLL_ONLY" -gt 0 ] && SO=1 || SO=0
[ "$HAS_FASTDEATH"  -gt 0 ] && FD=1 || FD=0
[ "$HAS_STARTAREA"  -gt 0 ] && SA=1 || SA=0
echo "[qa] features: scroll_only=$SO fastdeath=$FD start_area=$SA"

# ---- map を LZSS 圧縮(workdir)----
echo "[qa] compressing 16 maps ..."
for n in $AREAS; do
  python3 -I "$WT/tools/lzmap.py" compress "$AA/area${n}_map.bin" "$W/area${n}_map.lz" >/dev/null
done

# ---- (1) md5: 全データ入力 + 生成 .lz ----
echo "---- md5(data inputs) --------------------------------------------"
md5sum "$AA/common_pal.bin" "$AA/common_tiles.bin" 2>/dev/null || md5 "$AA/common_pal.bin" "$AA/common_tiles.bin"
for n in $AREAS; do
  md5sum "$AA/area${n}_used.bin" "$AA/area${n}_map.bin" "$AA/area${n}_gobj.bin" "$W/area${n}_map.lz" 2>/dev/null \
    || md5 "$AA/area${n}_used.bin" "$AA/area${n}_map.bin" "$AA/area${n}_gobj.bin" "$W/area${n}_map.lz"
done
md5sum "$EN/domogram_all.bin" 2>/dev/null || md5 "$EN/domogram_all.bin"
echo "------------------------------------------------------------------"

# ---- 共通 defines(production, ROT180 既定)----
DEF=(--define SHIP --define GOBJ_TILEMAP --define ALLAREAS --define SOUND
     --define SHIP_EXTDATA --define RETICLE_EXTDATA --define BLASTER_EXTDATA
     --define ENEMY_EXTDATA --define JARA_EXTDATA --define TORKAN_EXTDATA
     --define GROBDA_EXTDATA)

DATA=("$AA/common_pal.bin" "$AA/common_tiles.bin")
for n in $AREAS; do DATA+=("$AA/area${n}_used.bin" "$W/area${n}_map.lz" "$AA/area${n}_gobj.bin"); done
DATA+=("$EN/domogram_all.bin")

build_disk() {   # tag  extra-defines...
  local tag="$1"; shift
  local R
  ( cd "$WT" && R=$(sjasmplus --raw="$W/${tag}.bin" "${DEF[@]}" "$@" tools/emmscroll64.asm 2>&1 | tail -1); echo "$R" )
}

echo "[qa] building prod ..."
BR=$(build_disk prod)
echo "[qa] build prod: $BR"
case "$BR" in *"Errors: 0"*) ;; *) echo "[qa] PROD BUILD FAILED"; exit 1;; esac
python3 "$WT/tools/mkx1disk.py" "$W/prod.bin" -o "$W/prod.2d" -n XEVALL --load 0x0100 --data "${DATA[@]}" >/dev/null
echo "---- md5(output) -------------------------------------------------"
md5sum "$W/prod.bin" "$W/prod.2d" 2>/dev/null || md5 "$W/prod.bin" "$W/prod.2d"
echo "------------------------------------------------------------------"

# play 用: PERF_NODEATH(自機が死なず常時実体化)→ 参照 crop と (d) が安定。
HAS_NODEATH=$(grep -c 'PERF_NODEATH' "$WT/tools/sprite.inc" 2>/dev/null || echo 0)
[ "$HAS_NODEATH" -gt 0 ] && ND=1 || ND=0
PLAY_DISK="$W/prod.2d"
if [ "$ND" = 1 ]; then
  echo "[qa] building nodeath (PERF_NODEATH, play用) ..."
  BRN=$(build_disk nodeath --define PERF_NODEATH)
  case "$BRN" in *"Errors: 0"*) python3 "$WT/tools/mkx1disk.py" "$W/nodeath.bin" -o "$W/nodeath.2d" -n XEVALN --load 0x0100 --data "${DATA[@]}" >/dev/null; PLAY_DISK="$W/nodeath.2d" ;;
    *) echo "[qa] nodeath build FAILED: $BRN"; ND=0;; esac
fi

if [ "$SA" = 1 ]; then
  echo "[qa] building area2 (START_AREA=2) ..."
  BR2=$(build_disk area2 --define START_AREA=2)
  case "$BR2" in *"Errors: 0"*) python3 "$WT/tools/mkx1disk.py" "$W/area2.bin" -o "$W/area2.2d" -n XEVAL2 --load 0x0100 --data "${DATA[@]}" >/dev/null ;;
    *) echo "[qa] area2 build FAILED: $BR2"; SA=0;; esac
fi
if [ "$FD" = 1 ]; then
  echo "[qa] building death (FASTDEATH) ..."
  BRD=$(build_disk death --define FASTDEATH)
  case "$BRD" in *"Errors: 0"*) python3 "$WT/tools/mkx1disk.py" "$W/death.bin" -o "$W/death.2d" -n XEVALD --load 0x0100 --data "${DATA[@]}" >/dev/null ;;
    *) echo "[qa] death build FAILED: $BRD"; FD=0;; esac
fi

# ---- シナリオ実行(headless)----
run_scn() {   # tag disk autofire skip every n
  local tag="$1" disk="$2" af="$3" skip="$4" every="$5" n="$6"
  local d="$OUTH/$tag"; rm -rf "$d"; mkdir -p "$d"
  local -a env_af; env_af=()
  [ -n "$af" ] && env_af=(XMIL_AUTOFIRE="$af")
  env SDL_VIDEODRIVER=dummy XMIL_ROM_TYPE=3 XMIL_CYCMUL="$CYCMUL" \
      XMIL_DUMP="$d/f" XMIL_DUMP_N="$n" XMIL_DUMP_EVERY="$every" XMIL_DUMP_SKIP="$skip" "${env_af[@]}" \
      "$EMU" "$disk" >"$d/stderr.log" 2>&1 &
  local pid=$!
  local i cur
  for i in $(seq 1 60); do
    cur=$(find "$d" -maxdepth 1 -name 'f_*.ppm' 2>/dev/null | wc -l | tr -d ' ')
    [ "$cur" -ge "$n" ] && break
    kill -0 $pid 2>/dev/null || break
    sleep 1
  done
  sleep 1; kill -9 $pid 2>/dev/null
  echo "[qa]   $tag: $(find "$d" -maxdepth 1 -name 'f_*.ppm' 2>/dev/null | wc -l | tr -d ' ')/$n frames"
}

N_BOOT=10; N_PLAY=20; N_DEATH=120; N_AREA2=8
echo "[qa] running scenarios ..."
# AUTOFIRE=FF(ザッパー tap のみ)。BF はブラスター(bit6)保持で scroll_only_tick の
# 「両トリガ非押下→押下」エッジが成立せず scroll_only が解除されない(=自機/死亡が出ない)。
run_scn boot  "$W/prod.2d" ""   1500 150 $N_BOOT
run_scn play  "$PLAY_DISK" FF   2600  25 $N_PLAY
# death: FASTDEATH が 128f 毎に ship_hit。全黒+READY フェーズ(死亡巻戻し/game_over
#   いずれかの経路)を確実に跨ぐよう、長め・密めに採る(観測: 全黒+grey READY を複数回捕捉)。
[ "$FD" = 1 ] && run_scn death "$W/death.2d" FF 2000 12 $N_DEATH || { mkdir -p "$OUTH/death"; N_DEATH=0; }
[ "$SA" = 1 ] && run_scn area2 "$W/area2.2d" ""  1500 150 $N_AREA2 || mkdir -p "$OUTH/area2"

# ---- 自機参照 crop ----
if [ "$QA_MAKEREF" = "1" ] || [ ! -f "$REF" ]; then
  echo "[qa] (re)making ship ref from this build's play scenario"
  "${PY[@]}" makeref --play-dir "$OUTH/play" --out "$REF" --commit "$HASH"
  [ -f "$REF" ] && echo "[qa] NOTE: ref は今回($HASH)のビルドから生成。回帰比較では信頼コミットの ref を固定すること。"
fi

# ---- 自動チェック ----
echo "================================================================"
echo "[qa] automatic checks (commit $HASH)"
echo "================================================================"
AREA2_ARG=(); [ "$SA" = 1 ] && AREA2_ARG=(--area2-dir "$OUTH/area2")
"${PY[@]}" checks \
  --boot-dir "$OUTH/boot" --play-dir "$OUTH/play" --death-dir "$OUTH/death" "${AREA2_ARG[@]}" \
  --ref "$REF" --ship-k 3 \
  --has-scroll-only $SO --has-fastdeath $FD \
  --n-boot $N_BOOT --n-play $N_PLAY --n-death $N_DEATH
CHECKS_RC=$?

echo "---- 向きチェック(参考) -----------------------------------------"
"${PY[@]}" orient --boot-dir "$OUTH/boot"

# ---- コンタクトシート(player view)----
PAIRS=(--pair "boot/scroll-only=$OUTH/boot" --pair "play(AUTOFIRE)=$OUTH/play")
[ "$SA" = 1 ] && PAIRS+=(--pair "area2=$OUTH/area2")
[ "$FD" = 1 ] && PAIRS+=(--pair "death=$OUTH/death")
"${PY[@]}" contact "${PAIRS[@]}" --out "$QA_OUT/contact_$HASH.png"

echo "================================================================"
echo "[qa] done. commit=$HASH  checks_rc=$CHECKS_RC"
echo "[qa] contact sheet: $QA_OUT/contact_$HASH.png"
echo "================================================================"
exit $CHECKS_RC
