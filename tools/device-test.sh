#!/bin/sh
# 実機 (192.168.1.102) で全自動 perf テストを 1 回まわす:
#   EBOOT.PBP と autotest (秒数) を転送 -> 実行 -> 自動終了 ->
#   pspbrew.dev 復帰 -> pxmil.log を results/ に回収。
#
#   tools/device-test.sh [秒数=45] [ラベル=run]
#
# 実機が別アプリ実行中 (FTP 不達) のときは最大 RETRY 回リトライする。
# PSP_HOST で対象を変えられる (例: PSP_HOST=192.168.1.112 で PSP Go)。
set -e
cd "$(dirname "$0")/.."
SEC=${1:-45}
LABEL=${2:-run}
RETRY=${RETRY:-20}
PSPPY=$(cd ../pspbrew/tools && pwd)/psp.py
STAMP=$(date +%m%d-%H%M%S)
OUT="results/$STAMP-$LABEL.csv"
mkdir -p results
TMP=$(mktemp)
echo "$SEC" > "$TMP"
trap 'rm -f "$TMP"' EXIT

i=0
while :; do
	i=$((i + 1))
	if /usr/bin/python3 "$PSPPY" run \
		--put EBOOT.PBP:/PSP/GAME/PXMIL/EBOOT.PBP \
		--put "$TMP":/PSP/GAME/PXMIL/autotest \
		--exec /PSP/GAME/PXMIL/EBOOT.PBP \
		--get /PSP/GAME/PXMIL/pxmil.log:"$OUT" \
		--timeout $((SEC + 120)) 2>&1 | tee /dev/stderr | grep -q "$OUT"; then
		echo "device-test: $OUT"
		exit 0
	fi
	[ "$i" -lt "$RETRY" ] || { echo "device-test: giving up after $RETRY tries"; exit 1; }
	sleep 15
done
