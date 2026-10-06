# TODO

## 機能 (ユーザー要望、後回しで OK)
- [ ] 連射機能 (○/× の autofire、レート切替)
- [ ] ボタン左右入れ替えモード (○×⇔×○ スワップ)

## 性能 (進行中)
- [ ] 計測基盤: exec/present の per-frame µs + 毎秒ログ → 終了時 pxmil.log
- [ ] サウンド生成コスト (soft-double): samplingrate 22050 化 / NOSOUND 比較
- [ ] MEMOPTIMIZE 2 → 0 の比較 (低メモリモードが遅いパスを選んでいる可能性)
- [ ] -G8 (px68k で -14%/frame。GPREL16 オーバーフローが出たら extern 変数要調査)
- [ ] present が重ければ GU 縮小転送 (px68k psp/gecomp.c の 512 分割を移植)
- [ ] PGO (px68k tools/pgo-train.sh。デバッグポート未移植なので __gcov_dump() at exit + GCOV_PREFIX 方式を検討)

## 機能 (その他)
- [ ] メニュー UI (embed/menubase 連携、ディスク入替)
- [ ] ソフトウェアキーボード
- [ ] ステートセーブ
- [ ] ini 設定の保存/読込
