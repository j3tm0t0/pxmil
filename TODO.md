# TODO

## 機能 (ユーザー要望)
- [x] 連射機能 (L トリガーでトグル、○ 約 16 連/秒)
- [x] ボタン左右入れ替えモード (L+R 同時でトグル)
- [x] 設定の ini 保存 (連射・入替・アスペクト)
- [ ] 連射レート切替

## 性能
実機ゲーム本編で 60fps 達成 (exec 10.4ms + present 6.3ms ≈ 16.3ms)。
- [x] 計測基盤 (psp/perf.c、autotest、tools/device-test.sh で無人計測)
- [x] GU 縮小転送 + 32px スライス (present 25ms → 6.3ms)
- [x] -G8 (効果なしだが維持) / MEMOPTIMIZE 0 vs 2 (差なし、2 に戻した)
- [ ] PGO: CPU 予算 98% で余裕がないので、音切れ等が出たら着手
  (デバッグポート未移植なので __gcov_dump() at exit + GCOV_PREFIX 方式)
- [ ] サウンド生成は per-sample 整数演算と確認済み (soft-double は init のみ)

## 機能 (その他)
- [x] メニュー UI (SELECT 開閉、ディスク入替・設定・リセット・終了)
- [x] ソフトウェアキーボード (△ 開閉、○ 押下、ロック式モディファイア)
- [ ] ステートセーブ
- [ ] ini 設定の保存/読込
