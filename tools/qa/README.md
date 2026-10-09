# tools/qa — 縦画面ゼビウス 自動 QA / 回帰ハーネス

X1turboZ(64色/RT3)版「縦持ちゼビウス」デモの自動回帰チェック。
あるコミットについて production ディスクをクリーンな worktree でビルドし、
headless エミュレータで複数シナリオを走らせ、画面を自動判定する。

## 使い方

```
tools/qa/qa_check.sh [commit]      # 既定 HEAD
```

環境変数:
- `QA_OUT`      出力先(既定 `scratchpad/qa`)。シナリオ PPM・コンタクトシートが出る。
- `QA_SCRATCH`  worktree 等の作業ルート(既定 `scratchpad`)。
- `QA_MAKEREF=1` 自機参照 crop を今回のビルドから作り直す。
- `QA_CYCMUL`   既定 256(=4MHz 相当, headless 高速)。

終了コード: 自動チェックに 1 つでも FAIL があれば非 0。

実行時間は 1 コミットあたり ~15〜20 秒(ビルド 4 本 + エミュ 4 シナリオ)。

## 何をするか

1. **ビルド + md5**: `git worktree add --detach <commit>` のクリーン作業コピーで
   production ディスク(ROT180 既定)をビルド。`roms/` は gitignored なので
   worktree に symlink。ディスクと**全データ入力(共通pal/tiles・area01..16 の
   used/map(.bin と圧縮後 .lz)/gobj・domogram_all)の md5** を出力する
   (ビルド時データ差し替え/レースの検出はこの md5 比較が担う)。
2. **シナリオ(headless, `XMIL_ROM_TYPE=3`, `SDL_VIDEODRIVER=dummy`)**:
   - `boot`  : prod, 入力なし → scroll-only(自機非表示)。
   - `play`  : **PERF_NODEATH** ビルド + `XMIL_AUTOFIRE=FF` → 自機が死なず常時実体化。
   - `area2` : **START_AREA=2** ビルド, 入力なし → エリア2地形。
   - `death` : **FASTDEATH** ビルド + `FF` → 128f毎に被弾。全黒+grey READY フェーズ
     (死亡巻戻し/game_over いずれかの経路)を複数回捕捉する。
   - ※`AUTOFIRE` は **FF**(ザッパー tap)。`BF` はブラスター保持で scroll_only の
     開始エッジ(両トリガ非押下→押下)が成立せず自機/死亡が出ない。
3. **自動チェック(PASS/FAIL)**:
   - **(a) no-ship-in-scroll-only**: scroll-only フレームに自機が描かれない。
   - **(b) death-black+ready-only**: 全黒フレーム(death/READY フェーズ)は grey の
     READY! 以外の非黒ピクセルが無い(全黒フレームの ≥95% が grey のみでクリーン。
     末尾の地形復帰 1 枚程度は許容)。
   - **(c) terrain-sanity**: 水平隣接ピクセル変化率(h-trans)が閾値以下。
     チェッカ/画素ノイズで崩れると ~1.0 に跳ね上がり FAIL。
   - **(d) ship-pixel-match**: 自機スプライトが参照 crop と pixel 完全一致(マスク適用)。
   - **(e) no-hang/advancing**: 期待フレーム数到達・ゲームフレーム番号が単調増加・
     連続フレームが変化(全黒/READY ペアは除外)。
   - 参考: **向きチェック**(area1 の水が player 視点で左寄りか)を INFO 出力。
4. **コンタクトシート**: 各シナリオ代表フレームを player view(90°CCW 回転)で並べた
   PNG を `QA_OUT` に出力。

## 自機参照(ref)について

- `roms/qa_ref/ship_ref.png` + `ship_ref_mask.png`(どちらも `roms/` 配下=gitignored)。
- mask は play フレーム群で**安定(freq≥0.85)かつ非緑**の自機本体ピクセル。透明部分に
  透ける地形を除外する。(d) はこの mask 画素が ref と完全一致するフレーム数で判定。
- **回帰検出には ref を信頼コミットで固定すること**。ref が無ければ今回のビルドから
  自動生成する(その場合 NOTE を出す)。自機スプライトを意図的に変えたら
  `QA_MAKEREF=1` で更新する。

## 既知の限界

- **(c)** は画素レベルの崩壊(checkerboard/ノイズ, h-trans≈1.0)を検出するが、
  「正常タイルの並び替え/タイル index 破損」(内部はコヒーレント, h-trans≈0.16)は
  検出できない。地形マップとの相関照合は未実装。
- **向き(ROT180 vs 非回転)** は確実に判定できない。pre-ROT180 でも地形自体は
  コヒーレントに描けるため (c) は通る。orient は scroll-only 水フレームが
  無いコミットでは判定不能。
- **(d)** は自機の描画(ビットマップ/位置)が変わると FAIL する。地形回転だけの
  変更では自機ビットマップは不変のため PASS する(= 地形向き回帰は (d) では捕れない)。
- 旧コミットで test-define(scroll_only/FASTDEATH/START_AREA/PERF_NODEATH)が無い
  場合、該当チェック/シナリオは自動的に SKIP になる。
- 自機 rect(640x400 で x288-352, y184-232)は実測ハードコード。描画位置が
  大きく変わると mask が取れず要調整。
- 出力は `QA_OUT/<hash>/` に分離するので複数コミットの比較は潰し合わない。
- 正常終了時は worktree を自動削除するが、**SIGKILL されると trap が走らない**。
  残った場合は `git worktree prune`(と `$QA_SCRATCH/qa_wt_*` の手動削除)。

## 依存

- `sjasmplus`(`$HOME/.local/bin`), `python3`+numpy+PIL(非対話 zsh で mise 未activeでも
  numpy を持つ python を自動検出), リポジトリ root の `./xmilsdl2`。
