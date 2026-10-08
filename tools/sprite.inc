; =====================================================================
; sprite.inc — M7 弾/敵/爆発のスプライト共通モジュール (emmscroll64 用)
;   自機(ship.inc)と同じ PCG/テキスト方式。各スプライトは 2 窓
;   (cur_pos と cur_pos^OFF1024) に描く。色はテキストパレット(D案)のスロット。
;
; --- PCG コード予算 (計 188 / 256) ---
;   自機      0x10..0x9F  144 (16版 x 3x3, ship.inc)
;   弾        0xA0..0xAF   16 (16版 x 1セル, 小ドット=白 slot1)
;   敵        0xB0..0xBF   16 (4フレーム x 2x2, 8pxグリッド)       [Stage2]
;   爆発      0xC0..0xCB   12 (3フレーム x 2x2)                    [Stage3]
;   空き      0xCC..0xFF   52
;
; 呼び出し側(emmscroll64)が用意するもの: ship.inc と同じ EQU 群(COLS/ROWS/
;   OFF1024/PORT_SCRN/SCRN_PCG/TVRAM/TATTR/PCG_B/R/G/PCG_DEFCELL/PORT_PSG*),
;   cur_pos(dw), SPRGEN(生成バッファ 24B), および pcg_select/def_plane(ship.inc)。
; 公開: sprite_init(起動時), sprite_update(毎フレーム, ship_update の後)。
; =====================================================================

BULLET_BASE	EQU	0xA0		; 弾 PCG 先頭 (0xA0..0xAF)
MAX_BULLETS	EQU	4		; 同時存在数 (ザッパー2発 x 3組)。PCG は共有16版で不変
TRIG1		EQU	0x20		; PSG port A bit5 (トリガ1, 負論理)
ENEMY_BASE	EQU	0xB0		; [M8] Toroid PCG (0xB0..0xBF, 4フレーム x 2x2)
JARA_BASE	EQU	0xE0		; [M8④] Jara PCG (0xE0..0xEF, 4フレーム x 2x2)
TORKAN_BASE	EQU	0xF0		; [M8④] Torkan PCG (0xF0..0xF7, 2フレーム x 2x2)
ZOSHI_BASE	EQU	0x04		; [敵] Zoshi PCG (0x04..0x0B, 2フレーム x 2x2)
BACURA_RES_BASE	EQU	0x67		; [敵] Bacura 回転8コマ常駐先頭(自機dedupで空いた0x67..0x9F)
BACURA_RES_N	EQU	43		; 8コマ重複除去後のユニークセル数(0x67..0x91)
MAX_ENEMIES	EQU	5		; [M8④] 最大同時数(num_flying が 3→4→5 と上昇)
DIFF_INTERVAL	EQU	1800		; [M8④] 難易度1段の経過フレーム(≒30s@60Hz)
ENEMY_SPEED	EQU	1		; 横移動(セル/移動tick) ※敵は8pxグリッド
EN_NTYPES	EQU	4		; [敵] 敵タイプ数 (Toroid/Jara/Torkan/Zoshi)
TOROID_STOP	EQU	16		; [M8④] Toroid が停止する画面セル列(寄って停止)
TOROID_PAUSE	EQU	24		; 停止フレーム数(移動tick単位 ×ENEMY_MOVE)
EXPL_BASE	EQU	0xC0		; 爆発 PCG 先頭 (0xC0..0xCB, 3フレーム x 2x2)
MAX_EXPL	EQU	1		; 同時爆発数 (ピーク負荷削減)
RETICLE_BASE	EQU	0xCC		; [M8] 照準 PCG (0xCC..0xCF, 2x2 の角ブラケット)
RETICLE_DIST	EQU	10 * 4		; 自機前方の表示距離 (hx 2px単位, 10セル先)
BLASTER_BASE	EQU	0xD0		; [M8] ブラスター弾 PCG (0xD0..0xDB, 3サイズ x 2x2)
BLASTER_SIZES	EQU	3		; 照準へ近づくほど小さく(大→中→小)
RETICLE_RED_BASE EQU	0xDC		; [M8-3] 照準ロック時の赤版 PCG (0xDC..0xDF, 2x2)
RETICLE_BLINK	EQU	0x08		; 点滅周期(framecnt のこのビットで赤/シアン切替)
MAX_BLASTER	EQU	1		; 同時存在数 (ピーク負荷削減)
BLASTER_SPEED	EQU	4		; 前進/呼出 (2px単位)
TRIG2		EQU	0x40		; PSG port A bit6 (トリガ2, 負論理)
BLASTER_DELAY	EQU	12		; 連射間隔

; 速度/間隔定数は sprite_update の呼ばれる頻度で意味が変わる。
;   既定: sprite_update は 8フレームごと(ステップフレームのみ)。
;   -DSPR_SMOOTH: 毎フレーム / -DSPR_FRAMESEP: 約22Hz(3/8フレーム)。
;   どちらも 1呼出=ほぼ毎フレーム相当なので frame 単位の定数を使う。
	IFDEF	SPR_SMOOTH
FRAMEUNIT	EQU	1
	ENDIF
	IFDEF	SPR_FRAMESEP
FRAMEUNIT	EQU	1
	ENDIF
	IFDEF	FRAMEUNIT
BULLET_SPEED	EQU	3		; 前進/呼出 (2px単位; 3=6px/frame, 速め)
FIRE_DELAY	EQU	8		; 連射間隔(フレーム)
ENEMY_ANIM	EQU	12		; アニメ更新間隔(フレーム)
ENEMY_SPAWN	EQU	96		; 出現間隔(フレーム)
ENEMY_MOVE	EQU	8		; 移動間隔(フレーム) 8=1セル/8frame≒1px/frame平均
EXPL_ANIM	EQU	8		; 1コマの表示フレーム数
	ELSE
BULLET_SPEED	EQU	4		; 前進/呼出 (2px単位; 4=8px)
FIRE_DELAY	EQU	6		; 連射間隔(呼出)
ENEMY_ANIM	EQU	6		; アニメ更新間隔(呼出)
ENEMY_SPAWN	EQU	48		; 出現間隔(呼出)
ENEMY_MOVE	EQU	4		; 移動間隔(呼出)
EXPL_ANIM	EQU	4		; 1コマの表示呼出数
	ENDIF

;=====================================================================
; 起動時: 弾 PCG(16版)生成 + テーブル初期化。PCGモード有効時に呼ぶ。
sprite_init:
	call	gen_bullet
	call	gen_enemy
	call	gen_expl
	call	gen_ebullet		; [敵弾] スパリオ PCG
	call	ebul_init
	call	bacura_init		; [敵] Bacura
	ld	a, 3			; [敵弾] 残機初期化
	ld	(lives), a
	xor	a
	ld	(ship_inv), a
	ld	(game_over), a
	call	gen_reticle
	call	gen_reticle_red		; [M8-3] ロック点滅用の赤版照準 PCG
	call	gen_blaster
	IFDEF	GROBDA_EXTDATA
	call	gen_grobda		; [M8⑥] Grobda PCG
	call	grobda_init		; Grobda 初期配置
	ENDIF
	ld	hl, 0xFFFF		; 照準 prev 無効化
	ld	(ret_prev), hl
	ld	(ret_prev2), hl
	ld	a, RETICLE_BASE		; 初期はシアン
	ld	(ret_code), a
	ld	(ret_lcode), a
	ld	hl, bla_active		; ブラスター全非アクティブ
	ld	b, MAX_BLASTER
.cb:	ld	(hl), 0
	inc	hl
	djnz	.cb
	ld	hl, gobj_dead		; [M8-3] 地上物破壊フラグをクリア
	ld	b, MAX_GOBJ
.cg:	ld	(hl), 0
	inc	hl
	djnz	.cg
	xor	a
	ld	(bfire_tmr), a
	; 弾テーブルを全て非アクティブに
	ld	hl, bul_active
	ld	b, MAX_BULLETS
.ci:	ld	(hl), 0
	inc	hl
	djnz	.ci
	; 敵テーブルを全て非アクティブに
	ld	hl, ene_active
	ld	b, MAX_ENEMIES
.ce:	ld	(hl), 0
	inc	hl
	djnz	.ce
	; 爆発テーブルを全て非アクティブに
	ld	hl, exp_active
	ld	b, MAX_EXPL
.cx:	ld	(hl), 0
	inc	hl
	djnz	.cx
	xor	a
	ld	(fire_tmr), a
	ld	(spawn_tmr), a
	ret

; 弾 PCG 生成: 小ドット(左上 2x2, B プレーン=白 slot1)の 16 シフト版。
;   版 = vv*4 + vh。shift 右 2*vh / 下 2*vv。R,G=0。
gen_bullet:
	xor	a
	ld	(sp_vv), a
.vvl:
	xor	a
	ld	(sp_vh), a
.vhl:
	; --- 8ラスタの B パターンを SPRGEN[0..7] に作る ---
	;   ドット: raster r0,r1 = 0xC0。下へ 2*vv、右へ 2*vh シフト。
	push	hl
	ld	hl, SPRGEN
	ld	b, 8
	xor	a
.zf:	ld	(hl), a			; 0 クリア 8バイト
	inc	hl
	djnz	.zf
	; dot row = 2*vv, 2*vv+1 に 0xC0>>(2*vh) を置く
	ld	a, (sp_vh)
	add	a, a			; 2*vh
	ld	b, a			; シフト量
	ld	a, 0xC0
	inc	b
.shr:	dec	b
	jr	z, .shd
	srl	a
	jr	.shr
.shd:	; a = 0xC0 >> 2vh (ドットの1ラスタ)
	ld	c, a			; パターン
	ld	a, (sp_vv)
	add	a, a			; row = 2*vv
	ld	e, a
	ld	d, 0
	ld	hl, SPRGEN
	add	hl, de
	ld	(hl), c			; raster 2vv
	inc	hl
	ld	(hl), c			; raster 2vv+1
	pop	hl
	; --- PCG セル定義: code = BULLET_BASE + vv*4 + vh ---
	ld	a, (sp_vv)
	add	a, a
	add	a, a			; vv*4
	ld	b, a
	ld	a, (sp_vh)
	add	a, b			; 版
	add	a, BULLET_BASE
	call	pcg_select
	; B プレーン = SPRGEN(8), R/G = 0
	ld	hl, SPRGEN
	ld	d, PCG_B >> 8
	call	def_plane		; B (hl += 8)
	ld	hl, SPR_ZERO
	ld	d, PCG_R >> 8
	call	def_plane
	ld	hl, SPR_ZERO
	ld	d, PCG_G >> 8
	call	def_plane
	ld	a, (sp_vh)
	inc	a
	ld	(sp_vh), a
	cp	4
	jp	nz, .vhl
	ld	a, (sp_vv)
	inc	a
	ld	(sp_vv), a
	cp	4
	jp	nz, .vvl
	ret

;=====================================================================
; [M8] 照準(リティクル): 自機前方 RETICLE_DIST に常時表示する 2x2 のオリジナル
;   角ブラケット(orange tc7=B+R+G)。常時 active なので ship 同様に両窓 erase+draw。
; 照準 PCG 生成。実測: PCG プレーンは RGB チャンネルに直接対応(R→赤,G→緑,B→青)。
;   形状(shape)は reticle_data の G プレーン(セル内 offset+16)から取得(単色前提)。
;   通常=シアン(G+B), ロック=赤(R のみ)。gen_reticle_cells で共通化。
;   a=先頭PCGコード, c=プレーン選択(0=シアン:B+G, 1=赤:R)
gen_reticle:
	ld	a, RETICLE_BASE
	ld	c, 0			; シアン
	jr	gen_reticle_cells
gen_reticle_red:
	ld	a, RETICLE_RED_BASE
	ld	c, 1			; 赤
gen_reticle_cells:
	ld	(ene_code), a
	ld	a, c
	ld	(rr_mode), a
	ld	hl, reticle_data
	ld	(rr_src), hl
	ld	b, 4
.l:	push	bc
	ld	a, (ene_code)
	call	pcg_select
	ld	a, (rr_mode)
	or	a
	jr	nz, .red
	; 通常=tc6=slot6 シアン(0x27): R+G プレーン(B=0)。RT3 テキストパレットD 経由。
	ld	hl, rr_zero
	ld	d, PCG_B >> 8
	call	def_plane
	call	.shape
	ld	d, PCG_R >> 8
	call	def_plane
	call	.shape
	ld	d, PCG_G >> 8
	call	def_plane
	jr	.done
.red:	; ロック=tc5=slot5 赤(0x0C): B+G プレーン(R=0)。
	call	.shape
	ld	d, PCG_B >> 8
	call	def_plane
	ld	hl, rr_zero
	ld	d, PCG_R >> 8
	call	def_plane
	call	.shape
	ld	d, PCG_G >> 8
	call	def_plane
.done:
	ld	hl, (rr_src)		; 次セル (src+24)
	ld	de, 24
	add	hl, de
	ld	(rr_src), hl
	ld	a, (ene_code)
	inc	a
	ld	(ene_code), a
	pop	bc
	djnz	.l
	ret
; .shape: hl <- 現セルの shape プレーン(src+16 = G プレーン)
.shape:
	ld	hl, (rr_src)
	ld	de, 16
	add	hl, de
	ret

; [M8-3] 照準ロック判定: 照準の tilemap 位置に未破壊地上物があれば ret_code=赤
;   (framecnt のビットで点滅)、無ければシアン。実ヒットと同じ (col_ex,col_ey) 基準。
reticle_lock_update:
	ld	a, (ship_hx)
	add	a, RETICLE_DIST
	srl	a
	srl	a			; 照準セル列
	cp	COLS - 2
	jr	c, .cok
	ld	a, COLS - 2
.cok:	ld	b, a			; 画面セル列
	ld	a, (coarse)
	add	a, b
	add	a, GOBJ_OFFSET
	ld	c, a			; tilemap 列
	ld	a, (ship_vy)
	add	a, 2			; 照準の縦中心(reticle_soff と揃える)
	srl	a
	srl	a			; セル行 = tilemap 行(非スクロール)
	IFDEF	FORCE_RED
	ld	a, RETICLE_RED_BASE	; テスト: 常に赤
	ld	(ret_code), a
	ret
	ENDIF
	call	gobj_at			; carry = 未破壊地上物あり
	ld	a, RETICLE_BASE
	jr	nc, .set		; ロックなし → シアン
	ld	a, (framecnt)
	and	RETICLE_BLINK
	ld	a, RETICLE_BASE
	jr	z, .set			; 点滅オフ相 → シアン
	ld	a, RETICLE_RED_BASE	; 点滅オン相 → 赤
.set:	ld	(ret_code), a
	ret

; 照準の表示セル位置(soff)を計算して spr_soff に格納。
reticle_soff:
	ld	a, (ship_hx)
	add	a, RETICLE_DIST
	srl	a
	srl	a			; cell col = (hx+dist)>>2
	cp	COLS - 2
	jr	c, .cok
	ld	a, COLS - 2		; 画面内にクランプ
.cok:	ld	c, a			; cell col
	ld	a, (ship_vy)
	add	a, 2			; 着弾の縦中心=照準=自機中心(reticle と揃える)
	srl	a
	srl	a			; cell row
	ld	h, 0
	ld	l, a
	add	hl, hl
	add	hl, hl
	add	hl, hl			; *8
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, hl			; *32
	add	hl, de			; *40
	ld	a, c
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	(spr_soff), hl
	ret

reticle_erase:
	; [M8] idle-skip: scroll 不変かつ自機不動なら照準を描き直さない(ピーク負荷削減)
	ld	a, (scroll_moved)
	or	a
	jr	nz, .redraw
	ld	a, (ship_hx)
	ld	hl, ret_lhx
	cp	(hl)
	jr	nz, .redraw
	ld	a, (ship_vy)
	ld	hl, ret_lvy
	cp	(hl)
	jr	nz, .redraw
	ld	a, (ret_code)		; ロック点滅で色(PCGコード)が変われば描き直す
	ld	hl, ret_lcode
	cp	(hl)
	jr	nz, .redraw
	ld	a, 1			; 変化なし → スキップ
	ld	(ret_skip), a
	ret
.redraw:
	xor	a
	ld	(ret_skip), a
	ld	hl, (ret_prev)
	ld	a, h
	cp	0xFF
	ret	z
	call	ene_clr2x2
	ld	hl, (ret_prev2)
	call	ene_clr2x2
	ret

reticle_draw:
	ld	a, (ret_skip)
	or	a
	ret	nz			; erase 側でスキップ判定済
	ld	a, (ship_hx)		; 最終描画時の自機位置を記録
	ld	(ret_lhx), a
	ld	a, (ship_vy)
	ld	(ret_lvy), a
	ld	a, (ret_code)		; 今回描画する PCG コード(シアン/赤)を記録
	ld	(ret_lcode), a
	call	reticle_soff
	; 窓1
	ld	hl, (spr_soff)
	ld	de, (cur_pos)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(ret_prev), hl
	ld	a, (ret_code)
	call	ene_put2x2
	; 窓2
	ld	hl, (cur_pos)
	ld	a, h
	xor	0x04
	ld	h, a
	ld	de, (spr_soff)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(ret_prev2), hl
	ld	a, (ret_code)
	call	ene_put2x2
	ret

;=====================================================================
; [M8] ブラスター: トリガ2(bit6)で地上攻撃。自機前方へ飛び、照準位置(RETICLE_DIST
;   先)で着弾=爆発(地上物ヒットは M8-3 で接続)。2x2 のオリジナル小弾(青 tc6)。
gen_blaster:
	ld	hl, blaster_data
	ld	a, BLASTER_BASE
	ld	(ene_code), a
	ld	b, BLASTER_SIZES * 4
.l:	push	bc
	ld	a, (ene_code)
	call	pcg_select
	ld	d, PCG_B >> 8
	call	def_plane
	ld	d, PCG_R >> 8
	call	def_plane
	ld	d, PCG_G >> 8
	call	def_plane
	ld	a, (ene_code)
	inc	a
	ld	(ene_code), a
	pop	bc
	djnz	.l
	ret

fire_blaster:
	ld	a, (bfire_tmr)
	or	a
	jr	z, .rdy
	dec	a
	ld	(bfire_tmr), a
	ret
.rdy:
	call	read_joy
	bit	6, a			; トリガ2
	ret	nz
	; 空きスロット
	ld	hl, bla_active
	ld	b, MAX_BLASTER
	ld	c, 0
.fs:	ld	a, (hl)
	or	a
	jr	z, .sp
	inc	hl
	inc	c
	djnz	.fs
	ret
.sp:	ld	(hl), 1
	ld	a, c
	ld	e, a
	ld	d, 0
	ld	a, (ship_hx)
	add	a, 2 * 4		; 機首から
	ld	hl, bla_hx
	add	hl, de
	ld	(hl), a
	ld	a, (ship_hx)		; 着弾 x = ship_hx + RETICLE_DIST (照準 reticle_soff と同基準)
	add	a, RETICLE_DIST		; ※機首オフセット(+2*4)は付けない。付けると着弾が照準より2セル先になる
	ld	hl, bla_tx
	add	hl, de
	ld	(hl), a
	ld	a, (ship_vy)
	add	a, 2			; 照準2x2の縦中心を自機3x3の縦中心に合わせる(+2セル版)
	ld	hl, bla_vy
	add	hl, de
	ld	(hl), a
	ld	hl, bla_prev
	add	hl, de
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ld	hl, bla_prev2
	add	hl, de
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	IFDEF	SOUND
	ld	hl, se_blaster		; [SND] ブラスター発射音
	call	snd_play_se
	ENDIF
	ld	a, BLASTER_DELAY
	ld	(bfire_tmr), a
	ret

blaster_move:
	xor	a
	ld	(spr_i), a
.ml:	ld	a, (spr_i)
	cp	MAX_BLASTER
	ret	z
	ld	e, a
	ld	d, 0
	ld	hl, bla_active
	add	hl, de
	ld	a, (hl)
	or	a
	jr	z, .mn
	ld	hl, bla_hx
	add	hl, de
	ld	a, (hl)
	add	a, BLASTER_SPEED
	ld	(hl), a
	; 着弾? hx >= tx
	ld	hl, bla_tx
	add	hl, de
	cp	(hl)
	jr	c, .mn			; まだ
	; 着弾: 非アクティブ化 + 爆発(着弾セル) ※地上物ヒットは M8-3
	ld	hl, bla_active
	add	hl, de
	ld	(hl), 0
	; col_ex/col_ey に着弾セルを入れて expl_spawn
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, bla_tx
	add	hl, de
	ld	a, (hl)
	srl	a
	srl	a			; cell col
	ld	(col_ex), a
	ld	hl, bla_vy
	add	hl, de
	ld	a, (hl)
	srl	a
	srl	a			; cell row
	ld	(col_ey), a
	; [M8-3] 地上物ヒット判定: tilemap_col = coarse_low + col_ex + GOBJ_OFFSET
	ld	a, (coarse)		; 下位8bit(骨組み)
	ld	hl, col_ex
	add	a, (hl)
	add	a, GOBJ_OFFSET
	ld	c, a
	ld	(hit_tcol), a		; Sol 判定用に tilemap 列を保存
	ld	a, (col_ey)
	call	gobj_check
	jr	nc, .trysol		; 地上物ミス → Sol 判定へ
	call	crater_write_obj	; 当たり: 2x2の4セルを crater タイルで EMM 上書き
	call	crater_enqueue		; [#2] GRAM クレーターを保留登録(pump が数フレームで描く)
	IFDEF	SOUND
	ld	hl, se_exp_ground	; [SND] 地上物破壊音
	call	snd_play_se
	ENDIF
	jr	.nobj
.trysol:
	IFDEF	GOBJ_TILEMAP
	call	sol_hit_check		; [M8-3 #3] Sol(隠し塔)命中でせり上がり発動
	ENDIF
	IFDEF	GROBDA_EXTDATA
	call	grobda_hit_check	; [M8⑥] Grobda 命中で破壊
	ENDIF
.nobj:
	call	expl_spawn
.mn:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jr	.ml

blaster_erase:
	xor	a
	ld	(spr_i), a
.el:	ld	a, (spr_i)
	cp	MAX_BLASTER
	ret	z
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bla_prev
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	a, h
	cp	0xFF
	jp	z, .en
	call	ene_clr2x2
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bla_prev2
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	call	ene_clr2x2
	; 非アクティブなら両窓 slot 無効化
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, bla_active
	add	hl, de
	ld	a, (hl)
	or	a
	jp	nz, .en
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bla_prev
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bla_prev2
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
.en:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jp	.el

blaster_draw:
	xor	a
	ld	(spr_i), a
.dl:	ld	a, (spr_i)
	cp	MAX_BLASTER
	ret	z
	ld	e, a
	ld	d, 0
	ld	hl, bla_active
	add	hl, de
	ld	a, (hl)
	or	a
	jp	z, .dn
	; soff = (vy>>2)*COLS + (hx>>2)
	ld	hl, bla_vy
	add	hl, de
	ld	a, (hl)
	srl	a
	srl	a
	ld	h, 0
	ld	l, a
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, hl
	add	hl, de			; *40
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	push	hl
	ld	hl, bla_hx
	add	hl, de
	ld	a, (hl)
	srl	a
	srl	a
	pop	hl
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	(spr_soff), hl
	; [M8] サイズ = 照準への残距離(bla_tx - bla_hx)で 大(0)→中(1)→小(2)
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, bla_tx
	add	hl, de
	ld	a, (hl)			; tx
	ld	hl, bla_hx
	add	hl, de
	sub	(hl)			; remaining = tx - hx
	ld	c, 0
	cp	27
	jr	nc, .bsz
	ld	c, 1
	cp	14
	jr	nc, .bsz
	ld	c, 2
.bsz:	ld	a, c
	add	a, a
	add	a, a			; size*4
	add	a, BLASTER_BASE
	ld	(spr_code), a
	; 窓1
	ld	hl, (spr_soff)
	ld	de, (cur_pos)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(spr_cell), hl
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bla_prev
	add	hl, de
	ld	a, (spr_cell)
	ld	(hl), a
	inc	hl
	ld	a, (spr_cell + 1)
	ld	(hl), a
	ld	hl, (spr_cell)
	ld	a, (spr_code)
	call	ene_put2x2
	; 窓2
	ld	hl, (cur_pos)
	ld	a, h
	xor	0x04
	ld	h, a
	ld	de, (spr_soff)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(spr_cell), hl
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bla_prev2
	add	hl, de
	ld	a, (spr_cell)
	ld	(hl), a
	inc	hl
	ld	a, (spr_cell + 1)
	ld	(hl), a
	ld	hl, (spr_cell)
	ld	a, (spr_code)
	call	ene_put2x2
.dn:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jp	.dl

;=====================================================================
; [M8-3 骨組み] 地上物の当たり判定 + クレーター上書き。
;   地上物は xevi が地形タイルに焼き込み、破壊時にクレータータイルで上書きする。
;   当たり判定は「ワールド(tilemap)座標リスト」を参照する。実データ(リスト+クレータ
;   タイル)は -DGOBJ_EXTDATA で roms/ から読む。未指定時は空リスト(当たり無し)。
;   画面セル列 X ↔ tilemap列: tilemap_col = coarse + X (実測: screen_col = tile_col - coarse)。
;   行はスクロールしないので tile_row = 画面セル行 col_ey。
;   実測根拠(crater_gram self-test): tile col211 を coarse191 で screen col20,
;   coarse198 で col13 に描画 => screen_col = tile_col - coarse, OFFSET=0。
GOBJ_OFFSET	EQU	0		; screen_col = tile_col - coarse (実測確定)
MAX_GOBJ	EQU	16		; エリアあたり地上物の最大(破壊フラグ用)

; crater_write_emm: EMM のタイルマップ(col,row)を crater_id で上書き(再展開で反映)。
;   入力: c=tilemap列(8bit, 骨組み), a=row(0-24), de=crater tile ID。
;   EMM addr = col*50 + row*2 (read_ids と同じ列*50 + 行*2)。
; crater_write_cell: EMM タイルマップ(col8=c, row=a) に de(2byte タイルデータ
;   アドレス)を書く。EMM addr = col*50 + row*2。
crater_write_cell:
	push	de			; crater addr 退避
	ld	h, 0
	ld	l, c
	ld	b, h
	push	bc			; (col)
	add	hl, hl
	add	hl, hl
	add	hl, hl			; *8
	ld	d, h
	ld	e, l
	add	hl, hl			; *16
	add	hl, de			; *24
	pop	bc
	add	hl, bc			; *25
	add	hl, hl			; *50
	ld	d, 0
	add	a, a			; row*2
	ld	e, a
	add	hl, de			; hl = col*50 + row*2
	IFDEF	ALLAREAS
	ld	de, (amb_lo16)		; [⑦(3)] + area_map_base
	add	hl, de
	ld	a, l
	ld	(emm_a0), a
	ld	a, h
	ld	(emm_a1), a
	ld	a, (amb_hi8)
	adc	a, 0
	ld	(emm_a2), a
	ELSE
	ld	a, l
	ld	(emm_a0), a
	ld	a, h
	ld	(emm_a1), a
	xor	a
	ld	(emm_a2), a
	ENDIF
	call	set_emm_addr
	pop	de			; crater addr
	ld	bc, EMM_DAT
	out	(c), e
	out	(c), d
	ret

; crater_write_obj: gc_ptr のエントリの 2x2(TL/TR/BL/BR)を crater で EMM 上書き。
;   エントリ形式(xevi, 6B): dw col, db row, db type, dw crater_base。
;   2x2 タイル = crater_base + 0/+48/+96/+144 (TILEBYTES=48, tile 連続)。
;   セル配置: TL=(col,row) TR=(col+1,row) BL=(col,row+1) BR=(col+1,row+1)。
crater_write_obj:
	ld	hl, (gc_ptr)
	ld	a, (hl)			; col lo
	ld	(gc_col), a
	inc	hl
	inc	hl			; -> row
	ld	a, (hl)
	ld	(gc_row), a
	inc	hl			; -> +3 size
	ld	a, (hl)
	ld	(gc_size), a
	inc	hl			; -> crater_base
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; de = crater_base
	ld	(gc_cbase), de
	ld	a, (gc_size)
	cp	2
	jp	z, crater_write_4x4	; size=2 → 4x4
	; size=1: TL (col, row) = base
	ld	a, (gc_col)
	ld	c, a
	ld	a, (gc_row)
	call	crater_write_cell
	; TR (col+1, row) = base + 48
	ld	de, (gc_cbase)
	ld	hl, TILEBYTES
	add	hl, de
	ex	de, hl
	ld	a, (gc_col)
	inc	a
	ld	c, a
	ld	a, (gc_row)
	call	crater_write_cell
	; BL (col, row+1) = base + 96
	ld	de, (gc_cbase)
	ld	hl, TILEBYTES * 2
	add	hl, de
	ex	de, hl
	ld	a, (gc_col)
	ld	c, a
	ld	a, (gc_row)
	inc	a
	call	crater_write_cell
	; BR (col+1, row+1) = base + 144
	ld	de, (gc_cbase)
	ld	hl, TILEBYTES * 3
	add	hl, de
	ex	de, hl
	ld	a, (gc_col)
	inc	a
	ld	c, a
	ld	a, (gc_row)
	inc	a
	call	crater_write_cell
	ret

;=====================================================================
; crater_write_4x4: size=2 の 4x4 クレーター。cell k(0..15)=base+k*48、
;   位置=(gc_col+k%4, gc_row+k/4)。row>=ROWS(画面外)はスキップ(addr は進める)。
;   ※新形式の 16addr が base+k*48 連続でない稀なケースは近似(base 導出)。
crater_write_4x4:
	ld	de, (gc_cbase)		; 現 addr(cell毎 +48)
	xor	a
	ld	(cwo_r), a
.c4_r:
	xor	a
	ld	(cwo_c), a
.c4_c:
	; row = gc_row + cwo_r
	ld	a, (gc_row)
	ld	hl, cwo_r
	add	a, (hl)
	cp	ROWS
	jr	nc, .c4_skip		; 画面外 row → cell スキップ
	ld	(cwo_row), a
	; col = gc_col + cwo_c
	ld	a, (gc_col)
	ld	hl, cwo_c
	add	a, (hl)
	ld	c, a			; c = col
	ld	a, (cwo_row)		; a = row
	push	de
	call	crater_write_cell	; de = cell addr
	pop	de
.c4_skip:
	ld	hl, TILEBYTES
	add	hl, de
	ex	de, hl			; addr += 48
	ld	a, (cwo_c)
	inc	a
	ld	(cwo_c), a
	cp	4
	jr	nz, .c4_c
	ld	a, (cwo_r)
	inc	a
	ld	(cwo_r), a
	cp	4
	jr	nz, .c4_r
	ret

;=====================================================================
; crater_gram: crater_write_obj が EMM を更新した後、表示中 GRAM にクレーターを
;   即時反映する。地上物(tilemap col W=gc_col, row R=gc_row)の 2x2 は tile W,W+1 を
;   変更したので、4px ページでは隣接ブレンドのため dc_base 列 W-1/W/W+1 の 3 列
;   (各 compose は列C と C+1 を使う=operand B は col+1)を再合成する。タイル ID は
;   W-1..W+2 の 4 列を private バッファ(CG_IDBUF=TBUF)へ読み、page0/1 で 3 列×2 行を
;   compose48 + scatter_chunk で GRAM へ散布。スクロールが跨フレームで使う
;   dc_base/shl_hi/shr_hi のみ save/restore(ec_row/IDBUFA/B は非使用、
;   chunk_start/cnt/ec_dst は本処理で設定)。
CG_IDBUF	EQU	TBUF		; 0xC400: 4*50=200B (W-1..W+2 のタイルID)
; [M8-3 #2] クレーター GRAM 再描画をフレーム分散する。ヒット時は EMM 更新後
;   crater_enqueue で ID をキャッシュし保留登録、crater_pump が毎フレーム1ユニット
;   (1ページ1列=2行)ずつ描く(全6ユニット=2ページ×3列)。表示中ページを先に描く。
;   これで着弾時の一括 ~2フレーム停止を避ける(見た目のクレーターは数フレームで完成)。

; crater_enqueue: EMM 更新後に GRAM クレーターを保留登録。保留中なら先に flush。
crater_enqueue:
	ld	a, (pc_active)
	or	a
	call	nz, crater_flush	; 重複ヒット: 前回を先に完了
	; ID キャッシュ(ページ非依存): W-1..W+2 -> CG_IDBUF[0..3]
	ld	a, (gc_col)
	dec	a
	ld	e, a
	ld	hl, CG_IDBUF
	call	read_ids
	ld	a, (gc_col)
	ld	e, a
	ld	hl, CG_IDBUF + 50
	call	read_ids
	ld	a, (gc_col)
	inc	a
	ld	e, a
	ld	hl, CG_IDBUF + 100
	call	read_ids
	ld	a, (gc_col)
	inc	a
	inc	a
	ld	e, a
	ld	hl, CG_IDBUF + 150
	call	read_ids
	; 位置を保存(以後のフレームで pump が使う)
	ld	a, (gc_row)
	ld	(pc_row), a
	ld	a, (col_ex)
	ld	(pc_ex), a
	ld	hl, (coarse)		; ヒット時の coarse を固定(pump は各フレームで使う)
	ld	(pc_coarse), hl		; 物体の GRAM リング位置はヒット時に確定・不変
	ld	a, (cur_pos + 1)	; 表示中ページ = cur_pos bit10
	and	0x04
	jr	z, .dp0
	ld	a, 1
.dp0:	ld	(pc_dpage), a
	xor	a
	ld	(pc_prog), a
	inc	a
	ld	(pc_active), a		; a=1
	ret

; crater_pump: 毎フレーム1回。保留クレーターがあれば1ユニットを描く。
crater_pump:
	ld	a, (pc_active)
	or	a
	ret	z
	IFDEF	CRATER_DBG
	ld	a, 0xAB
	ld	bc, 0x00FE
	out	(c), a
	ld	a, 0xAA
	ld	bc, 0x00FF
	out	(c), a			; PROBE 0xAAAB=43691 (pump 1ユニット実行)
	ENDIF
	; prog -> (page, k): 0-2=表示中ページ(先), 3-5=裏ページ
	ld	a, (pc_prog)
	cp	3
	jr	c, .front
	sub	3
	ld	(cg_k), a
	ld	a, (pc_dpage)
	xor	1
	ld	(cg_page), a
	jr	.doit
.front:
	ld	(cg_k), a
	ld	a, (pc_dpage)
	ld	(cg_page), a
.doit:
	; save 跨フレーム状態(スクロール chunk が使う)
	ld	hl, (dc_base)
	ld	(cg_sav_dc), hl
	ld	a, (shl_hi)
	ld	(cg_sav_shl), a
	ld	a, (shr_hi)
	ld	(cg_sav_shr), a
	call	crater_unit
	ld	hl, (cg_sav_dc)
	ld	(dc_base), hl
	ld	a, (cg_sav_shl)
	ld	(shl_hi), a
	ld	a, (cg_sav_shr)
	ld	(shr_hi), a
	ld	a, (pc_prog)
	inc	a
	ld	(pc_prog), a
	cp	6
	ret	nz
	xor	a
	ld	(pc_active), a		; 全6ユニット完了
	ret

; crater_flush: 保留クレーターを同期で全部描く(self-test/重複ヒット時)。
crater_flush:
	ld	a, (pc_active)
	or	a
	ret	z
	call	crater_pump
	jr	crater_flush

; crater_gram: 同期版(self-test 用)。enqueue + flush で即座に全描画。
crater_gram:
	IFDEF	CRATER_DBG
	ld	a, 0xAA
	ld	bc, 0x00FE
	out	(c), a
	ld	a, 0xAA
	ld	bc, 0x00FF
	out	(c), a			; PROBE 0xAAAA=43690 (同期 crater_gram)
	ENDIF
	call	crater_enqueue
	jp	crater_flush

; crater_unit: cg_page/cg_k の1ユニット(pc_row/pc_ex 基準, CG_IDBUF 使用)を GRAM へ。
crater_unit:
	ld	a, (cg_page)
	add	a, (SHLTAB >> 8)
	ld	(shl_hi), a
	ld	a, (cg_page)
	add	a, (SHRTAB >> 8)
	ld	(shr_hi), a
	; dc_base = (pc_coarse + pc_ex + k - 1 - COLS + page*OFF1024) & 0x7FF
	;   pc_coarse=ヒット時 coarse(固定)。分散中に coarse が進んでも GRAM 位置は不変。
	ld	hl, (pc_coarse)
	ld	a, (cg_page)
	or	a
	jr	z, .np0
	ld	de, OFF1024
	add	hl, de
.np0:
	ld	a, (pc_ex)
	ld	e, a
	ld	a, (cg_k)
	add	a, e
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	de, 1 + COLS
	or	a
	sbc	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(dc_base), hl
	ld	a, (pc_row)
	ld	(chunk_start), a
	ld	a, 2
	ld	(chunk_cnt), a
	ld	hl, COLBUF
	ld	(ec_dst), hl
	ld	a, (pc_row)
	ld	(cg_rr), a
	ld	b, 2
.rloop:
	push	bc
	ld	a, (cg_k)
	call	cg_colbase		; hl = CG_IDBUF + k*50
	ld	a, (cg_rr)
	add	a, a
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	(ec_ta), hl
	ld	a, (cg_k)
	inc	a
	call	cg_colbase
	ld	a, (cg_rr)
	add	a, a
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	(ec_tb), hl
	call	compose48
	ld	a, (cg_rr)
	inc	a
	ld	(cg_rr), a
	pop	bc
	djnz	.rloop
	call	scatter_chunk
	ret

; cg_colbase: a=0..3 -> hl = CG_IDBUF + a*50
cg_colbase:
	ld	hl, CG_IDBUF
	or	a
	ret	z
	ld	b, a
	ld	de, 50
.add:	add	hl, de
	djnz	.add
	ret

	IFDEF	GOBJ_TILEMAP
;=====================================================================
; [M8-3 #3] Sol(隠し塔): 通常は base tilemap に出ない(地形のまま)。ブラスター命中で
;   その 2x2 を f0→f3 とせり上がり上書き(xevi データ: col102,row2)。各フレームの
;   4タイルは base+0/+48/+96/+144 連続なので crater_write_obj/crater_enqueue を再利用。
SOL_COL		EQU	102
SOL_ROW		EQU	2
SOL_STEP	EQU	8		; アニメ 1コマあたりフレーム数(せり上がり速度)
sol_bases:	dw	0x4033, 0x40F3, 0x41B3, 0x4273	; f0,f1,f2,f3 の crater_base
sol_entry:	dw	SOL_COL		; crater_write_obj が読む 6B エントリ
		db	SOL_ROW
		db	1		; +3=size(Sol は 2x2=size1)
		dw	0		; crater_base(コマ毎に設定)

; sol_hit_check: 命中(hit_tcol,col_ey)が Sol 位置かつ未発動なら発動。
;   ALLAREAS: sol_list(エリアごと, 最大8体)を走査。非: ハードコード単一。
sol_hit_check:
	ld	a, (sol_state)
	or	a
	ret	nz			; 既に発動/露出済み
	IFDEF	ALLAREAS
	ld	a, (sol_n)
	or	a
	ret	z			; Sol 無し
	ld	b, a
	ld	ix, sol_list
.shl:
	ld	a, (hit_tcol)
	cp	(ix + 0)		; col lo
	jr	nz, .shnext
	ld	a, (col_ey)
	cp	(ix + 2)		; row
	jr	nz, .shnext
	; 一致: sol_col/row/base_ptr を設定(bases は ix+3)
	ld	a, (ix + 0)
	ld	(sol_col), a
	ld	a, (ix + 2)
	ld	(sol_row), a
	push	ix
	pop	hl
	ld	de, 3
	add	hl, de
	ld	(sol_base_ptr), hl
	jr	.shhit
.shnext:
	ld	de, 11
	add	ix, de
	djnz	.shl
	ret				; 一致無し
.shhit:
	ELSE
	ld	a, (hit_tcol)
	cp	SOL_COL
	ret	nz
	ld	a, (col_ey)
	cp	SOL_ROW
	ret	nz
	ld	a, SOL_COL
	ld	(sol_col), a
	ld	a, SOL_ROW
	ld	(sol_row), a
	ld	hl, sol_bases
	ld	(sol_base_ptr), hl
	ENDIF
	ld	a, 1
	ld	(sol_state), a		; f0 から開始
	xor	a
	ld	(sol_timer), a
	IFDEF	SOUND
	ld	hl, se_teleport		; [SND] Sol 出現音(テレポート)
	call	snd_play_se
	ENDIF
	ret

; sol_update: 毎フレーム1回(mainloop)。発動中なら SOL_STEP 毎に次コマを上書き。
sol_update:
	ld	a, (sol_state)
	or	a
	ret	z			; 非発動
	cp	5
	ret	z			; f3 完了(露出維持)
	ld	a, (sol_timer)
	or	a
	jr	z, .step
	dec	a
	ld	(sol_timer), a
	ret
.step:
	; sol_entry.base = sol_base_ptr[sol_state-1]
	ld	a, (sol_state)
	dec	a
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, (sol_base_ptr)
	add	hl, de
	ld	a, (hl)
	ld	(sol_entry + 4), a
	inc	hl
	ld	a, (hl)
	ld	(sol_entry + 5), a
	; sol_entry.col/row = sol_col/sol_row(エリアごと)
	ld	a, (sol_col)
	ld	(sol_entry + 0), a
	ld	a, (sol_row)
	ld	(sol_entry + 2), a
	; col_ex = sol_col - coarse_lo (画面列)
	ld	a, (sol_col)
	ld	hl, coarse
	sub	(hl)
	ld	(col_ex), a
	ld	hl, sol_entry
	ld	(gc_ptr), hl
	call	crater_write_obj	; 2x2 を現コマで EMM 上書き
	call	crater_enqueue		; GRAM へ(pump が分散描画)
	ld	a, (sol_state)
	inc	a
	cp	5
	jr	nz, .sol_cont		; まだ f3 前
	IFDEF	ALLAREAS
	xor	a			; [⑦] 露出完了→state=0(他の Sol を発動可能に。露出タイルは残る)
	ENDIF
	ld	(sol_state), a
	ret				; f3 まで出し切った
.sol_cont:
	ld	(sol_state), a
	ld	a, SOL_STEP
	ld	(sol_timer), a
	ret
	ENDIF

; gobj_at: (c=col8, a=row) に未破壊地上物があれば carry、無ければ nc。非破壊
;   (gc_ptr/破壊フラグを変更しない)。照準ロック表示判定用。
gobj_at:
	ld	(ga_row), a
	ld	a, c
	ld	(ga_col), a
	ld	a, (gobj_n)
	or	a
	ret	z
	ld	b, a			; 残り件数
	ld	hl, gobj_list
	xor	a
	ld	(ga_i), a
.l:	push	bc
	; 破壊済み?
	ld	a, (ga_i)
	ld	e, a
	ld	d, 0
	push	hl
	ld	hl, gobj_dead
	add	hl, de
	ld	a, (hl)
	pop	hl
	or	a
	jr	nz, .nx
	; col lo (offset0)
	ld	a, (hl)
	ld	e, a
	ld	a, (ga_col)
	cp	e
	jr	nz, .nx
	; row (offset2)
	push	hl
	inc	hl
	inc	hl
	ld	a, (hl)
	pop	hl
	ld	e, a
	ld	a, (ga_row)
	cp	e
	jr	nz, .nx
	pop	bc			; 一致
	scf
	ret
.nx:	ld	de, 6
	add	hl, de
	ld	a, (ga_i)
	inc	a
	ld	(ga_i), a
	pop	bc
	djnz	.l
	or	a			; 不一致
	ret

; gobj_check: 着弾の tilemap(col8=c, row=a) が未破壊の地上物と一致するか。
;   一致→ carry セット + gc_ptr=一致エントリ + 破壊フラグセット。不一致→ carry クリア。
;   1件 = dw col, db row, db type, dw crater_base = 6B。
gobj_check:
	ld	(gc_row), a
	ld	a, c
	ld	(gc_col), a
	ld	a, (gobj_n)
	or	a
	ret	z			; 空リスト
	ld	b, a			; b = 残り件数
	ld	hl, gobj_list
	xor	a
	ld	(gc_i), a
.l:	push	bc
	push	hl			; エントリ先頭
	ld	a, (hl)			; col lo
	ld	c, a
	inc	hl
	inc	hl			; -> row
	ld	a, (hl)
	ld	b, a			; b = row (件数はスタック)
	; 破壊済み?
	ld	a, (gc_i)
	ld	e, a
	ld	d, 0
	ld	hl, gobj_dead
	add	hl, de
	ld	a, (hl)
	or	a
	jr	nz, .nx
	ld	a, (gc_col)
	cp	c
	jr	nz, .nx
	ld	a, (gc_row)
	cp	b
	jr	nz, .nx
	; 一致!
	pop	hl			; エントリ先頭
	ld	(gc_ptr), hl
	pop	bc			; 件数破棄
	ld	a, (gc_i)		; 破壊フラグ
	ld	e, a
	ld	d, 0
	ld	hl, gobj_dead
	add	hl, de
	ld	(hl), 1
	scf
	ret
.nx:	pop	hl			; エントリ先頭
	ld	de, 6			; 1件=6B (dw col, db row, db type, dw crater_base)
	add	hl, de			; 次エントリ
	pop	bc			; 件数
	ld	a, (gc_i)
	inc	a
	ld	(gc_i), a
	djnz	.l
	or	a			; carry クリア
	ret

;=====================================================================
; 毎フレーム(vblank 中, ship_update の後)。発射/移動/消去/描画。
;   順序: 発射判定 → 全弾消去 → 移動/寿命 → 全弾描画 (重なり対策で分離)。
sprite_update:
	call	ship_tick		; [敵弾] 無敵タイマ減算
	call	fire_check		; トリガ1でザッパー発射
	call	fire_blaster		; トリガ2でブラスター発射
	call	enemy_spawn		; 一定間隔で右端から出現(game_over 中は内部で抑止)
	; scroll_moved = (cur_pos != prev_cur_pos) : cur_pos は SPEED フレームごとにしか
	;   変わらない。変わらないフレームは動いていない敵の描き直しを省略(idle-skip)。
	ld	hl, (cur_pos)
	ld	de, (prev_cur_pos)
	ld	(prev_cur_pos), hl
	ld	a, l
	sub	e
	ld	b, a
	ld	a, h
	sub	d
	or	b			; 0 なら変化なし
	ld	a, 0
	jr	z, .nomv
	inc	a
.nomv:	ld	(scroll_moved), a
	; 移動/アニメ(消去より先。敵は ene_dirty を立てる)
	call	bullets_move
	call	blaster_move		; 前進+着弾(expl_spawn)
	call	enemies_move		; [敵弾] 射撃AI(type≠0 自機狙い)は enemies_move に統合済
	IFDEF	GROBDA_EXTDATA
	call	grobda_move		; [M8⑥] Grobda 移動(道vy + スクロール追従)
	ENDIF
	call	expls_anim
	; 衝突判定(弾vs敵, 敵vs自機) → 当たった弾/敵を非アクティブ化 + 爆発発生
	call	collision_check
	; [M8-3] 照準ロック判定(地上物上なら赤点滅) → ret_code 決定
	call	reticle_lock_update
	; 全消去(前回位置)。敵は idle-skip, 非アクティブ化された敵も消去される。
	call	reticle_erase
	call	expls_erase
	call	enemies_erase
	IFDEF	GROBDA_EXTDATA
	call	grobda_erase		; [M8⑥]
	ENDIF
	call	bullets_erase
	call	blaster_erase
	; 全描画(アクティブのみ)。照準は最後=最前面。
	call	bullets_draw
	call	blaster_draw
	call	enemies_draw
	IFDEF	GROBDA_EXTDATA
	call	grobda_draw		; [M8⑥]
	ENDIF
	IFDEF	EBUL_SELFTEST
	; 決定論テスト: 30フレーム毎に col20,row2 から右下へ弾を発射
	ld	a, (eb_test_tmr)
	dec	a
	ld	(eb_test_tmr), a
	jr	nz, .ebnt
	ld	a, 30
	ld	(eb_test_tmr), a
	ld	hl, 0x1400		; x = col20<<8
	ld	de, 0x0200		; y = row2<<8
	ld	bc, 0x0040
	ld	(ebul_svx), bc		; vx = +0.25 cell/f
	ld	bc, 0x0080
	ld	(ebul_svy), bc		; vy = +0.5 cell/f
	call	ebul_spawn
.ebnt:
	ENDIF
	call	ebul_update		; [敵弾] 移動+消去+描画(1パス)
	call	bacura_update		; [敵] Bacura スクロール+衝突+描画
	call	expls_draw
	call	reticle_draw
	ret

; 衝突判定(セル空間)。弾セルが敵2x2矩形内 → 両方消滅 + 爆発。
;   敵2x2が自機3x3矩形と重なる → 敵消滅 + 爆発(自機は当面無傷)。
collision_check:
	; --- 弾 vs 敵 ---
	xor	a
	ld	(spr_i), a		; 弾 index
.bl:	ld	a, (spr_i)
	cp	MAX_BULLETS
	jp	nc, .evs		; 範囲外のため jp
	ld	e, a
	ld	d, 0
	ld	hl, bul_active
	add	hl, de
	ld	a, (hl)
	or	a
	jp	z, .bnext
	; 弾セル bcx=(hx>>2), bcy=(vy>>2)
	ld	hl, bul_hx
	add	hl, de
	ld	a, (hl)
	srl	a
	srl	a
	ld	(col_bcx), a
	ld	hl, bul_vy
	add	hl, de
	ld	a, (hl)
	srl	a
	srl	a
	ld	(col_bcy), a
	; 各敵と判定
	xor	a
	ld	(spr_j), a
.ej:	ld	a, (spr_j)
	cp	MAX_ENEMIES
	jr	nc, .bnext
	ld	e, a
	ld	d, 0
	ld	hl, ene_active
	add	hl, de
	ld	a, (hl)
	or	a
	jr	z, .ejn
	; bcx in [ex, ex+1] ?
	ld	hl, ene_ex
	add	hl, de
	ld	a, (col_bcx)
	sub	(hl)			; bcx - ex
	cp	2			; 0 or 1 ?
	jr	nc, .ejn
	ld	hl, ene_ey
	add	hl, de
	ld	a, (col_bcy)
	sub	(hl)
	cp	2
	jr	nc, .ejn
	; ヒット: 弾消滅, 敵消滅, 爆発(敵位置)
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, bul_active
	add	hl, de
	ld	(hl), 0
	ld	a, (spr_j)
	ld	e, a
	ld	d, 0
	ld	hl, ene_active
	add	hl, de
	ld	(hl), 0
	ld	hl, ene_dirty		; 消滅 → dirty(消去させる)
	add	hl, de
	ld	(hl), 1
	ld	hl, ene_ex
	add	hl, de
	ld	a, (hl)
	ld	(col_ex), a
	ld	hl, ene_ey
	add	hl, de
	ld	a, (hl)
	ld	(col_ey), a
	call	expl_spawn		; (col_ex, col_ey) に爆発
	IFDEF	SOUND
	ld	hl, se_flyhit		; [SND] 空中敵 hit 音
	call	snd_play_se
	ENDIF
	jr	.bnext			; この弾は消えたので次の弾へ
.ejn:	ld	a, (spr_j)
	inc	a
	ld	(spr_j), a
	jr	.ej
.bnext:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jp	.bl
	; --- 敵 vs 自機 ---
.evs:
	xor	a
	ld	(spr_j), a
.vl:	ld	a, (spr_j)
	cp	MAX_ENEMIES
	ret	z
	ld	e, a
	ld	d, 0
	ld	hl, ene_active
	add	hl, de
	ld	a, (hl)
	or	a
	jr	z, .vn
	; 自機セル scx=(ship_hx>>2) 幅3, scy=(ship_vy>>2) 高3
	; 矩形重なり: ex < scx+3 && ex+2 > scx && ey < scy+3 && ey+2 > scy
	ld	hl, ene_ex
	add	hl, de
	ld	a, (hl)
	ld	(col_ex), a		; ex
	ld	hl, ene_ey
	add	hl, de
	ld	a, (hl)
	ld	(col_ey), a
	ld	a, (ship_hx)
	srl	a
	srl	a
	ld	(col_scx), a		; scx
	ld	a, (ship_vy)
	srl	a
	srl	a
	ld	(col_scy), a
	; 矩形重なり: 敵[ex,ex+1] と 自機[scx,scx+2] が重なる ⇔ (ex-scx) in [-1..2]。
	;   (ex - scx + 1) in [0..3] で重なり。
	ld	a, (col_ex)
	ld	hl, col_scx
	sub	(hl)
	add	a, 1
	cp	4
	jr	nc, .vn			; X 非重なり
	ld	a, (col_ey)
	ld	hl, col_scy
	sub	(hl)
	add	a, 1
	cp	4
	jr	nc, .vn			; Y 非重なり
	; 重なり: 敵消滅 + 爆発
	ld	a, (spr_j)
	ld	e, a
	ld	d, 0
	ld	hl, ene_active
	add	hl, de
	ld	(hl), 0
	ld	hl, ene_dirty		; 消滅 → dirty
	add	hl, de
	ld	(hl), 1
	call	expl_spawn		; 敵爆発 (col_ex,col_ey)
	call	ship_hit		; [敵弾] 自機も被弾(無敵中は無視)
.vn:	ld	a, (spr_j)
	inc	a
	ld	(spr_j), a
	jr	.vl

; トリガ(bit5,負論理)で、連射間隔ごとに空きスロットへ弾を発射。
fire_check:
	ld	a, (fire_tmr)
	or	a
	jr	z, .rdy
	dec	a
	ld	(fire_tmr), a
	ret
.rdy:
	call	read_joy		; a = PSG port A (負論理)
	bit	5, a			; トリガ1
	ret	nz			; 押されていない(bit=1)
	; [M8] ザッパー2発: vy(X1縦方向)で上下に並べて同時発射。
	ld	a, (ship_vy)
	add	a, 2			; 上側 (自機上寄り)
	call	spawn_bullet
	ld	a, (ship_vy)
	add	a, 10			; 下側 (自機下寄り)
	call	spawn_bullet
	IFDEF	SOUND
	ld	hl, se_zapper		; [SND] ザッパー発射音
	call	snd_play_se
	ENDIF
	ld	a, FIRE_DELAY
	ld	(fire_tmr), a
	ret

; a = 発射 vy。空きスロットに弾1発を spawn (hx = ship_hx + 2セル前方)。
;   空きが無ければ何もしない。de/bc は壊してよい。
spawn_bullet:
	ld	(bul_spawn_vy), a
	ld	hl, bul_active
	ld	b, MAX_BULLETS
	ld	c, 0			; index
.fs:	ld	a, (hl)
	or	a
	jr	z, .sp
	inc	hl
	inc	c
	djnz	.fs
	ret				; 空き無し
.sp:	ld	(hl), 1			; active
	ld	a, c
	ld	e, a
	ld	d, 0
	ld	a, (ship_hx)
	add	a, 2 * 4		; 機首(2セル右)あたりから
	ld	hl, bul_hx
	add	hl, de
	ld	(hl), a
	ld	a, (bul_spawn_vy)
	ld	hl, bul_vy
	add	hl, de
	ld	(hl), a
	ld	hl, bul_prev		; prev 無効化(両窓)
	add	hl, de
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ld	hl, bul_prev2
	add	hl, de
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ret

; 全弾を前回位置(両窓)から消去。ループ index は RAM(spr_i)。clr_cell が bc を
;   壊すためレジスタ c には置かない。
bullets_erase:
	xor	a
	ld	(spr_i), a
.el:	ld	a, (spr_i)
	cp	MAX_BULLETS
	ret	z
	; prev 窓1 (bul_prev + 2*i)。active/非active を問わず、prev が有効なら両窓消去
	;   (非アクティブの消去をスキップするとゴーストが残る)。
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bul_prev
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; hl = cell(窓1)
	ld	a, h
	cp	0xFF
	jp	z, .en			; prev 無効 → 両窓とも何も無い
	CLRCELL				; 窓1 消去
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bul_prev2
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; hl = cell(窓2)
	CLRCELL				; 窓2 消去
	; 非アクティブ(画面外で消滅)なら両窓 slot を無効化(再消去/再利用の残留防止)
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, bul_active
	add	hl, de
	ld	a, (hl)
	or	a
	jp	nz, .en			; active なら draw が両窓 prev を再設定
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bul_prev
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bul_prev2
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
.en:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jp	.el

; 全弾を前進、画面外(cell col >= COLS-1)で消滅。
bullets_move:
	xor	a
	ld	(spr_i), a
.ml:	ld	a, (spr_i)
	cp	MAX_BULLETS
	ret	z
	ld	e, a
	ld	d, 0
	ld	hl, bul_active
	add	hl, de
	ld	a, (hl)
	or	a
	jr	z, .mn
	ld	hl, bul_hx
	add	hl, de
	ld	a, (hl)
	add	a, BULLET_SPEED
	ld	(hl), a
	srl	a
	srl	a
	cp	COLS - 1
	jr	c, .mn
	ld	hl, bul_active
	add	hl, de
	ld	(hl), 0			; 画面外 → 非アクティブ
.mn:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jr	.ml

; 全弾を新位置に描画(両窓), prev 記録。index は RAM(spr_i)。
bullets_draw:
	xor	a
	ld	(spr_i), a
.dl:	ld	a, (spr_i)
	cp	MAX_BULLETS
	ret	z
	ld	e, a
	ld	d, 0
	ld	hl, bul_active
	add	hl, de
	ld	a, (hl)
	or	a
	jp	z, .dn
	; soff = (vy>>2)*COLS + (hx>>2)
	ld	hl, bul_vy
	add	hl, de
	ld	a, (hl)
	srl	a
	srl	a			; 行
	ld	h, 0
	ld	l, a
	add	hl, hl
	add	hl, hl
	add	hl, hl			; *8
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, hl			; *32
	add	hl, de			; *40
	; + 列 (hx>>2)
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	push	hl
	ld	hl, bul_hx
	add	hl, de
	ld	a, (hl)
	srl	a
	srl	a
	pop	hl
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	(spr_soff), hl
	; code = BULLET_BASE + (vy&3)*4 + (hx&3)
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, bul_vy
	add	hl, de
	ld	a, (hl)
	and	3
	add	a, a
	add	a, a
	ld	b, a
	ld	hl, bul_hx
	add	hl, de
	ld	a, (hl)
	and	3
	add	a, b
	add	a, BULLET_BASE
	ld	(spr_code), a
	; 窓1: cell = (cur_pos + soff) & 0x7FF
	ld	hl, (spr_soff)
	ld	de, (cur_pos)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	; prev 記録(窓1) : bul_prev + 2*i
	ld	(spr_cell), hl
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bul_prev
	add	hl, de
	ld	a, (spr_cell)
	ld	(hl), a
	inc	hl
	ld	a, (spr_cell + 1)
	ld	(hl), a
	ld	hl, (spr_cell)
	ld	a, (spr_code)
	PUTCELL				; 窓1 描画
	; 窓2: cell = ((cur_pos ^ 0x400) + soff) & 0x7FF
	ld	hl, (cur_pos)
	ld	a, h
	xor	0x04
	ld	h, a
	ld	de, (spr_soff)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(spr_cell), hl
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bul_prev2
	add	hl, de
	ld	a, (spr_cell)
	ld	(hl), a
	inc	hl
	ld	a, (spr_cell + 1)
	ld	(hl), a
	ld	hl, (spr_cell)
	ld	a, (spr_code)
	PUTCELL				; 窓2 描画
.dn:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jp	.dl

;=====================================================================
; [M8④] 敵 PCG 生成: 3タイプ分を各 base から連番定義。各セル=24B(B/R/G×8)。
;   Toroid(16セル=4コマ) / Jara(16セル=4コマ) / Torkan(8セル=2コマ)。
gen_enemy:
	ld	hl, enemy_data		; Toroid
	ld	a, ENEMY_BASE
	ld	b, 16
	call	gen_pcg_set
	ld	hl, jara_data		; Jara
	ld	a, JARA_BASE
	ld	b, 16
	call	gen_pcg_set
	ld	hl, torkan_data		; Torkan
	ld	a, TORKAN_BASE
	ld	b, 8
	call	gen_pcg_set
	ld	hl, zoshi_data		; [敵] Zoshi(2コマ=8セル)
	ld	a, ZOSHI_BASE
	ld	b, 8
	call	gen_pcg_set
	ld	hl, bacura_cells	; [敵] Bacura 回転8コマの重複除去済み43セルを常駐
	ld	a, BACURA_RES_BASE
	ld	b, BACURA_RES_N
	call	gen_pcg_set
	ret
; gen_pcg_set: hl=データ先頭, a=先頭PCGコード, b=セル数。連番で定義。
gen_pcg_set:
	ld	(ene_code), a
.el:	push	bc
	ld	a, (ene_code)
	call	pcg_select
	ld	d, PCG_B >> 8
	call	def_plane		; B (hl += 8)
	ld	d, PCG_R >> 8
	call	def_plane		; R
	ld	d, PCG_G >> 8
	call	def_plane		; G (hl は次セル +24 へ)
	ld	a, (ene_code)
	inc	a
	ld	(ene_code), a
	pop	bc
	djnz	.el
	ret

; [M8④] アクティブな敵数を数える → a。
count_active:
	ld	hl, ene_active
	ld	b, MAX_ENEMIES
	ld	c, 0
.cal:	ld	a, (hl)
	or	a
	jr	z, .caz
	inc	c
.caz:	inc	hl
	djnz	.cal
	ld	a, c
	ret

; 一定間隔で右端から敵出現(空きスロットがあれば)。ey は数行を循環。
enemy_spawn:
	IFDEF	NO_ENEMY
	ret				; 検証用: 敵を出さない(弾/爆発のゴースト切り分け)
	ENDIF
	ld	a, (game_over)
	or	a
	ret	nz			; ゲームオーバー中は出現しない
	; [M8④] 難易度上昇(経過): difficulty<2 の間 diff_tmr を減らし 0 で +1。
	;   num_flying=3+difficulty(3→4→5)。型解禁: type<=difficulty のみ(他はToroid)。
	ld	a, (difficulty)
	cp	3			; [敵] Zoshi(type3)解禁のため cap を 3 に
	jr	nc, .dskip
	ld	hl, (diff_tmr)
	dec	hl
	ld	(diff_tmr), hl
	ld	a, h
	or	l
	jr	nz, .dskip
	ld	a, (difficulty)
	inc	a
	ld	(difficulty), a
	add	a, 3			; 3+difficulty
	cp	MAX_ENEMIES + 1
	jr	c, .nfok
	ld	a, MAX_ENEMIES		; num_flying は MAX_ENEMIES でクランプ
.nfok:
	ld	(num_flying), a
	ld	hl, DIFF_INTERVAL
	ld	(diff_tmr), hl
.dskip:
	ld	a, (spawn_tmr)
	or	a
	jr	z, .go
	dec	a
	ld	(spawn_tmr), a
	ret
.go:
	ld	a, ENEMY_SPAWN
	ld	(spawn_tmr), a
	; [M8④] 同時数制限: active >= 実効上限 なら出さない。
	;   [M8⑥(A)] 実効上限 = num_flying - 表示中Grobda数(最低1)。敵+Grobda を bounded に。
	ld	a, (num_flying)
	ld	(eff_cap), a
	IFDEF	GROBDA_EXTDATA
	call	count_grob_onscreen	; a = 表示中Grobda数
	ld	c, a
	ld	a, (eff_cap)
	sub	c
	jr	nc, .cok
	xor	a
.cok:	or	a
	jr	nz, .cnz
	inc	a			; 最低1
.cnz:	ld	(eff_cap), a		; 実効上限
	ENDIF
	call	count_active		; a = active 数 (b,c 破壊)
	ld	hl, eff_cap
	cp	(hl)
	ret	nc			; active >= 実効上限 → 出さない
	; 空きスロット探索
	xor	a
	ld	(spr_i), a
.fs:	ld	a, (spr_i)
	cp	MAX_ENEMIES
	ret	z			; 空き無し
	ld	e, a
	ld	d, 0
	ld	hl, ene_active
	add	hl, de
	ld	a, (hl)
	or	a
	jr	z, .spawn
	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jr	.fs
.spawn:
	ld	(hl), 1			; active
	ld	hl, ene_ex
	add	hl, de
	ld	a, COLS - 2		; 右端
	ld	(hl), a
	; ey: spawn_row を循環し row = 3*idx + 6 (= 6,9,12,15)
	ld	a, (spawn_row)
	inc	a
	cp	4
	jr	c, .rok
	xor	a
.rok:	ld	(spawn_row), a
	ld	c, a
	add	a, a			; 2*idx
	add	a, c			; 3*idx
	add	a, 6			; 3*idx + 6
	ld	hl, ene_ey
	add	hl, de
	ld	(hl), a
	ld	hl, ene_frm
	add	hl, de
	ld	(hl), 0
	ld	hl, ene_dir		; [M8④] 振動方向 初期=下
	add	hl, de
	ld	(hl), 0
	ld	hl, ene_phase		; [M8④] Toroid フェーズ 初期=approach
	add	hl, de
	ld	(hl), 0
	; [32方向] 16bit位置 = セル<<8(frac=0), 速度=0(per-typeが設定)
	ld	hl, ene_ex
	add	hl, de
	ld	b, (hl)			; b=ex
	ld	hl, ene_ey
	add	hl, de
	ld	c, (hl)			; c=ey
	ld	hl, ene_wx
	add	hl, de
	add	hl, de
	ld	(hl), 0
	inc	hl
	ld	(hl), b
	ld	hl, ene_wy
	add	hl, de
	add	hl, de
	ld	(hl), 0
	inc	hl
	ld	(hl), c
	ld	hl, ene_vx
	add	hl, de
	ld	(hl), 0
	ld	hl, ene_vy
	add	hl, de
	ld	(hl), 0
	; [M8④] タイプ = spawn_seq[spawn_idx] (エリア1はToroid主体, たまにJara/Torkan)
	push	de			; slot index 退避
	ld	a, (spawn_idx)
	ld	e, a
	ld	d, 0
	ld	hl, spawn_seq
	add	hl, de
	ld	a, (hl)			; 出現タイプ
	pop	de			; slot index
	; [M8④] 型解禁: type > difficulty なら Toroid(0) に落とす
	ld	c, a
	ld	a, (difficulty)
	cp	c
	jr	nc, .tclamp		; difficulty >= type → そのまま
	ld	c, 0			; 未解禁 → Toroid
.tclamp:
	ld	a, c
	ld	hl, ene_type
	add	hl, de
	ld	(hl), a
	ld	a, (spawn_idx)
	inc	a
	cp	SPAWN_SEQ_LEN
	jr	c, .tok
	xor	a
.tok:	ld	(spawn_idx), a
	; 負荷分散: スロット index でタイマー位相をずらす(1フレームに描き直す敵を減らす)
	ld	a, (spr_i)		; ene_atmr = slot+1 (アニメ位相ずらし)
	inc	a
	ld	hl, ene_atmr
	add	hl, de
	ld	(hl), a
	ld	a, (spr_i)		; ene_mtmr = (slot mod ENEMY_MOVE)+1 (移動位相ずらし)
	and	ENEMY_MOVE - 1
	inc	a
	ld	hl, ene_mtmr
	add	hl, de
	ld	(hl), a
	; [敵弾] 射撃タイマ初期化(slot で位相ずらし, type≠0 のみ ene_fire_check で使用)
	ld	a, (spr_i)
	add	a, a
	add	a, 60
	ld	hl, ene_ftmr
	add	hl, de
	ld	(hl), a
	ld	hl, ene_prev
	add	hl, de
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ld	hl, ene_prev2		; 窓2 も無効化(孤児防止)
	add	hl, de
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ret

; 全敵を前回位置(両窓, 2x2)から消去。
enemies_erase:
	xor	a
	ld	(spr_i), a
.el:	ld	a, (spr_i)
	cp	MAX_ENEMIES
	ret	z
	ld	e, a
	ld	d, 0
	; idle-skip: (scroll_moved OR dirty) でなければ消去しない(VRAMに残す)
	ld	a, (scroll_moved)
	ld	hl, ene_dirty
	add	hl, de
	or	(hl)
	jp	z, .en
	; prev 窓1
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, ene_prev
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	a, h
	cp	0xFF
	jr	z, .en			; prev 無効(未描画) → 消去不要
	call	ene_clr2x2		; de 保持
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, ene_prev2
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	call	ene_clr2x2
	; 非アクティブ(衝突で消滅)なら prev を無効化(次フレームの再消去防止)
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, ene_active
	add	hl, de
	ld	a, (hl)
	or	a
	jr	nz, .en			; active なら draw が prev 更新
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, ene_prev
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ld	a, (spr_i)		; 窓2 も無効化(孤児防止)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, ene_prev2
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
.en:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jp	.el

; 全敵のアニメ更新 + 左へ移動 + 画面外で消滅。移動/アニメしたら ene_dirty=1。
enemies_move:
	xor	a
	ld	(spr_i), a
.ml:	ld	a, (spr_i)
	cp	MAX_ENEMIES
	ret	z
	ld	e, a
	ld	d, 0
	ld	hl, ene_active
	add	hl, de
	ld	a, (hl)
	or	a
	jp	z, .mn
	ld	hl, ene_dirty		; dirty = 0 (初期)
	add	hl, de
	ld	(hl), 0
	; アニメ
	ld	hl, ene_atmr
	add	hl, de
	dec	(hl)
	jr	nz, .amv
	ld	(hl), ENEMY_ANIM
	ld	hl, ene_frm
	add	hl, de
	ld	a, (hl)
	inc	a
	; [M8④] nframes でクランプ(タイプ毎にコマ数が違う)
	push	hl			; ene_frm ptr
	push	de			; slot index
	push	af			; 新フレーム
	ld	hl, ene_type
	add	hl, de
	ld	a, (hl)			; type
	ld	c, a
	add	a, a
	add	a, c			; type*3
	inc	a			; +1 = nframes オフセット
	ld	e, a
	ld	d, 0
	ld	hl, entype_tab
	add	hl, de
	ld	b, (hl)			; nframes
	pop	af			; 新フレーム
	cp	b
	jr	c, .fok
	xor	a			; nframes 到達 → 0 へ
.fok:
	pop	de			; slot index
	pop	hl			; ene_frm ptr
	ld	(hl), a
	ld	hl, ene_dirty		; アニメ変化 → dirty
	add	hl, de
	ld	(hl), 1
.amv:
	; [32方向] move-tick(ene_mtmr)ごとに位置積分(ene_wx+=sext(vx)*ENEMY_MOVE)+cell導出+消滅判定。
	;   描画はセル単位なので tick 更新で視覚は従来同等、per-frame 積分よりコストは 1/ENEMY_MOVE。
	ld	hl, ene_mtmr
	add	hl, de
	dec	(hl)
	jp	nz, .mn
	ld	(hl), ENEMY_MOVE
	call	ene_integrate
	ld	hl, ene_ex
	add	hl, de
	ld	a, (hl)
	cp	COLS			; ex>=COLS(左underflow0xFF/右40+) → 消滅
	jp	c, .onscr
	ld	hl, ene_active
	add	hl, de
	ld	(hl), 0
	jp	.mn
.onscr:
	; [M8④] per-type 速度設定。de=slot。traj_id を取得して分岐。
	;   Toroid: 横入り→STOP列で停止(pause)→後退(右へ) / Jara: 左進+上下振動 /
	;   Torkan: 斜めダイブ(左2倍+下)。
	push	de			; slot index
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, ene_type
	add	hl, de
	ld	a, (hl)
	ld	c, a
	add	a, a
	add	a, c
	inc	a
	inc	a			; type*3+2
	ld	e, a
	ld	d, 0
	ld	hl, entype_tab
	add	hl, de
	ld	a, (hl)			; traj_id
	pop	de			; slot index 復元
	or	a
	jp	z, .e_tor		; 0=Toroid
	cp	2
	jp	z, .e_tk		; 2=Torkan
	; --- 1=Jara: vx=-32(左1px/f), vy=±32(ene_dir 振動, ey 4..20) ---
	ld	hl, ene_vx
	add	hl, de
	ld	(hl), -32
	ld	hl, ene_dir
	add	hl, de
	ld	a, (hl)
	or	a
	jr	nz, .e_jup
	ld	hl, ene_vy		; dir=0: 下(vy=+32)
	add	hl, de
	ld	(hl), 32
	ld	hl, ene_ey
	add	hl, de
	ld	a, (hl)
	cp	20
	jp	c, .e_off
	ld	hl, ene_dir
	add	hl, de
	ld	(hl), 1
	jp	.e_off
.e_jup:	ld	hl, ene_vy		; dir=1: 上(vy=-32)
	add	hl, de
	ld	(hl), -32
	ld	hl, ene_ey
	add	hl, de
	ld	a, (hl)
	cp	4
	jp	nc, .e_off
	ld	hl, ene_dir
	add	hl, de
	ld	(hl), 0
	jp	.e_off
.e_tor:	; --- 0=Toroid: phase machine(0=approach/1=pause/2=retreat) ---
	ld	hl, ene_phase
	add	hl, de
	ld	a, (hl)
	or	a
	jr	nz, .e_tor12
	ld	hl, ene_vx		; phase0 approach: vx=-32(左)
	add	hl, de
	ld	(hl), -32
	ld	hl, ene_vy
	add	hl, de
	ld	(hl), 0
	ld	hl, ene_ex
	add	hl, de
	ld	a, (hl)
	cp	TOROID_STOP + 1
	jp	nc, .e_off		; まだ手前 → 継続
	ld	hl, ene_phase		; STOP 到達 → pause
	add	hl, de
	ld	(hl), 1
	ld	hl, ene_vx		; 停止(vx=0)
	add	hl, de
	ld	(hl), 0
	ld	hl, ene_ptmr
	add	hl, de
	ld	(hl), TOROID_PAUSE
	jp	.e_off
.e_tor12:
	cp	2
	jr	z, .e_tor_ret
	ld	hl, ene_ptmr		; phase1 pause: タイマー
	add	hl, de
	dec	(hl)
	jp	nz, .e_off
	ld	hl, ene_phase
	add	hl, de
	ld	(hl), 2			; → retreat
	ld	hl, ene_vx		; retreat 右(vx=+32)
	add	hl, de
	ld	(hl), 32
	jp	.e_off
.e_tor_ret:				; phase2 retreat: vx=+32(右)
	ld	hl, ene_vx
	add	hl, de
	ld	(hl), 32
	jp	.e_off
.e_tk:	; --- 2=Torkan: vx=-64(左2px/f ダイブ) + vy=+32(ey<22で下) ---
	ld	hl, ene_vx
	add	hl, de
	ld	(hl), -64
	ld	hl, ene_ey
	add	hl, de
	ld	a, (hl)
	cp	22
	jr	c, .e_tk_dn
	ld	hl, ene_vy		; ey>=22: 下げ止め
	add	hl, de
	ld	(hl), 0
	jp	.e_off
.e_tk_dn:
	ld	hl, ene_vy
	add	hl, de
	ld	(hl), 32
.e_off:
	; [敵弾] 射撃統合: type≠0 かつ !game_over なら ftmr 減算、0 で自機狙い発射。
	;   (de=spr_i*1。ene_fire は spr_i を使い de を破壊するので後で復帰)
	ld	a, (game_over)
	or	a
	jr	nz, .nofire
	ld	hl, ene_type
	add	hl, de
	ld	a, (hl)
	or	a
	jr	z, .nofire		; Toroid は撃たない
	ld	hl, ene_ftmr
	add	hl, de
	dec	(hl)
	jr	nz, .nofire
	call	ene_fire
	ld	a, (framecnt)
	and	7
	inc	a
	add	a, a
	add	a, a
	add	a, a			; ((framecnt&7)+1)*8
	ld	c, a
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, ene_ftmr
	add	hl, de
	ld	(hl), c
.nofire:
	; off-screen 判定は .amv(毎フレーム積分後)に移動済
.mn:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jp	.ml

;=====================================================================
; [敵32方向] ene_integrate: ene_wx += sext(vx)*ENEMY_MOVE, ene_wy += sext(vy)*ENEMY_MOVE,
;   ene_ex/ey=(>>8)。速度は 1/32px/frame なので tick(ENEMY_MOVE frame)あたり *ENEMY_MOVE。
;   セル(高位バイト)変化で ene_dirty=1(描画 idle-skip)。de=slot。a/b/c/hl 破壊。
ene_integrate:
	ld	hl, ene_vx		; vx → bc=sext16(vx)*ENEMY_MOVE
	add	hl, de
	ld	a, (hl)
	ld	b, 0
	or	a
	jp	p, .xp
	dec	b			; b=0xFF(負)
.xp:	ld	c, a
	IF ENEMY_MOVE >= 2
	sla	c
	rl	b
	ENDIF
	IF ENEMY_MOVE >= 4
	sla	c
	rl	b
	ENDIF
	IF ENEMY_MOVE >= 8
	sla	c
	rl	b
	ENDIF
	ld	hl, ene_wx		; &ene_wx[slot](dw) += bc
	add	hl, de
	add	hl, de
	ld	a, (hl)
	add	a, c
	ld	(hl), a
	inc	hl
	ld	a, (hl)
	adc	a, b
	ld	(hl), a
	ld	c, a			; c=新 ex(=hi)
	ld	hl, ene_ex
	add	hl, de
	ld	a, (hl)
	cp	c
	jr	z, .xsame
	ld	(hl), c
	ld	hl, ene_dirty
	add	hl, de
	ld	(hl), 1
.xsame:
	ld	hl, ene_vy		; vy → bc=sext16(vy)*ENEMY_MOVE
	add	hl, de
	ld	a, (hl)
	ld	b, 0
	or	a
	jp	p, .yp
	dec	b
.yp:	ld	c, a
	IF ENEMY_MOVE >= 2
	sla	c
	rl	b
	ENDIF
	IF ENEMY_MOVE >= 4
	sla	c
	rl	b
	ENDIF
	IF ENEMY_MOVE >= 8
	sla	c
	rl	b
	ENDIF
	ld	hl, ene_wy
	add	hl, de
	add	hl, de
	ld	a, (hl)
	add	a, c
	ld	(hl), a
	inc	hl
	ld	a, (hl)
	adc	a, b
	ld	(hl), a
	ld	c, a			; 新 ey
	ld	hl, ene_ey
	add	hl, de
	ld	a, (hl)
	cp	c
	ret	z
	ld	(hl), c
	ld	hl, ene_dirty
	add	hl, de
	ld	(hl), 1
	ret

;=====================================================================
; [敵32方向] get_index_for_angle: 自機への32方向index(0..31)。
;   ROM get_index_for_angle@0x0EB2 と同一(atan2不要のoctant法)。
;   入力 gia_dx,gia_dy = 符号付セル差(ship_cell - self_cell。8px=1セル粒度=ROM忠実)。
;   出力 A=idx(0..31)。aim 時のみ呼ぶ(spawn/再照準)。octant_angle_tbl は enemy_ai.inc。
get_index_for_angle:
	ld	a, (gia_dx)		; ax=|dx|
	call	gia_abs
	ld	(gia_ax), a
	ld	a, (gia_dy)		; c=ay=|dy|
	call	gia_abs
	ld	c, a
	ld	a, (gia_ax)
	cp	c			; ax>=ay ?
	jr	nc, .flat		; flat(steep=0): big=ax, small=ay
	ld	a, 1			; steep=1: big=ay, small=ax
	ld	(gia_steep), a
	ld	a, c
	ld	(gia_big), a
	ld	a, (gia_ax)
	ld	(gia_small), a
	jr	.ratio
.flat:
	xor	a
	ld	(gia_steep), a
	ld	a, (gia_ax)
	ld	(gia_big), a
	ld	a, c
	ld	(gia_small), a
.ratio:
	ld	a, (gia_big)
	or	a
	jr	z, .idx0		; big=0 → idx0
	ld	a, (gia_small)		; hl = small*32
	ld	l, a
	ld	h, 0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, (gia_big)
	ld	e, a
	ld	d, 0			; de=big
	ld	c, 0			; ratio
.rl:	or	a
	sbc	hl, de
	jr	c, .rdone
	inc	c
	jr	.rl
.rdone:
	ld	a, c			; ang = octant_angle_tbl[ratio]
	ld	e, a
	ld	d, 0
	ld	hl, octant_angle_tbl
	add	hl, de
	ld	b, (hl)			; ang
	ld	a, (gia_steep)
	or	a
	jr	z, .nosteep
	ld	a, 0x41			; steep: 0x41-ang
	sub	b
	ld	b, a
.nosteep:
	ld	a, (gia_dx)
	or	a
	jp	p, .nodxn
	ld	a, 0x81			; dx<0: 0x81-ang
	sub	b
	ld	b, a
.nodxn:
	ld	a, (gia_dy)
	or	a
	jp	p, .nodyn
	xor	a			; dy<0: -ang
	sub	b
	ld	b, a
.nodyn:
	ld	a, b			; idx=(ang>>3)&0x1F
	srl	a
	srl	a
	srl	a
	and	0x1F
	ret
.idx0:	xor	a
	ret

gia_abs:				; a=|a| (符号付8bit)
	or	a
	ret	p
	neg
	ret

; 全敵を新位置に描画(両窓, 2x2), prev 記録。
enemies_draw:
	xor	a
	ld	(spr_i), a
.dl:	ld	a, (spr_i)
	cp	MAX_ENEMIES
	ret	z
	ld	e, a
	ld	d, 0
	ld	hl, ene_active
	add	hl, de
	ld	a, (hl)
	or	a
	jp	z, .dn
	; idle-skip: prev 無効(新規/消去後)なら必ず描画。prev 有効なら
	;   (scroll_moved OR dirty) のときだけ描き直す。
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, ene_prev + 1	; 窓1 prev の高位
	add	hl, de
	ld	a, (hl)
	cp	0xFF
	jr	z, .draw		; prev 無効 → 描画
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	a, (scroll_moved)
	ld	hl, ene_dirty
	add	hl, de
	or	(hl)
	jp	z, .dn			; 変化なし → 描画スキップ
.draw:
	ld	a, (spr_i)		; de = spr_i に復帰
	ld	e, a
	ld	d, 0
	; soff = ey*COLS + ex
	ld	hl, ene_ey
	add	hl, de
	ld	a, (hl)
	ld	h, 0
	ld	l, a
	add	hl, hl
	add	hl, hl
	add	hl, hl			; *8
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, hl			; *32
	add	hl, de			; *40
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	push	hl
	ld	hl, ene_ex
	add	hl, de
	ld	a, (hl)
	pop	hl
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	(spr_soff), hl
	; [M8] code base = (type の pcg_base) + frame*4
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, ene_frm
	add	hl, de
	ld	a, (hl)
	add	a, a
	add	a, a			; frame*4
	ld	b, a			; 退避
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, ene_type
	add	hl, de
	ld	a, (hl)			; type
	ld	c, a
	add	a, a
	add	a, c			; type*3
	ld	e, a
	ld	d, 0
	ld	hl, entype_tab
	add	hl, de
	ld	a, (hl)			; pcg_base
	add	a, b			; + frame*4
	ld	(spr_code), a
	; 窓1
	ld	hl, (spr_soff)
	ld	de, (cur_pos)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(spr_cell), hl
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, ene_prev
	add	hl, de
	ld	a, (spr_cell)
	ld	(hl), a
	inc	hl
	ld	a, (spr_cell + 1)
	ld	(hl), a
	ld	hl, (spr_cell)
	ld	a, (spr_code)
	call	ene_put2x2
	; 窓2
	ld	hl, (cur_pos)
	ld	a, h
	xor	0x04
	ld	h, a
	ld	de, (spr_soff)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(spr_cell), hl
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, ene_prev2
	add	hl, de
	ld	a, (spr_cell)
	ld	(hl), a
	inc	hl
	ld	a, (spr_cell + 1)
	ld	(hl), a
	ld	hl, (spr_cell)
	ld	a, (spr_code)
	call	ene_put2x2
.dn:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jp	.dl

; hl=top-left セル, a=先頭コード。2x2 (code+0,+1 / +2,+3) を配置。
ene_put2x2:
	ld	(ene_tl), hl
	ld	(ene_bc), a
	PUTCELL				; TL (hl=TL, a=code)
	ld	hl, (ene_tl)
	inc	hl
	ld	a, (ene_bc)
	inc	a
	PUTCELL				; TR
	ld	hl, (ene_tl)
	ld	de, COLS
	add	hl, de
	ld	a, (ene_bc)
	add	a, 2
	PUTCELL				; BL
	ld	hl, (ene_tl)
	ld	de, COLS + 1
	add	hl, de
	ld	a, (ene_bc)
	add	a, 3
	PUTCELL				; BR
	ret

; bac_offs: Bacura 2幅×4高の8セル描画オフセット(TL からのセル相対, draw順 r0L,r0R,…,r3R)。
bac_offs:
	dw	0, 1
	dw	COLS, COLS + 1
	dw	2 * COLS, 2 * COLS + 1
	dw	3 * COLS, 3 * COLS + 1

; bac_put2x4: hl=TL, de=frametab 行(8コード)。16x32 の 8セルを配置(frametab 駆動)。
bac_put2x4:
	ld	(bp_tl), hl
	ld	(bp_ptr), de
	ld	hl, bac_offs
	ld	(bp_offp), hl
	ld	b, 8
.l:	push	bc
	ld	hl, (bp_offp)		; セルオフセット
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	(bp_offp), hl
	ld	hl, (bp_tl)
	add	hl, de			; pos = TL + offs
	ld	de, (bp_ptr)		; コード
	ld	a, (de)
	inc	de
	ld	(bp_ptr), de
	PUTCELL				; a=code, hl=pos
	pop	bc
	djnz	.l
	ret

; ene_clr2x4: hl=TL。Bacura 8セルを消去。
ene_clr2x4:
	ld	(bp_tl), hl
	ld	hl, bac_offs
	ld	(bp_offp), hl
	ld	b, 8
.l:	push	bc
	ld	hl, (bp_offp)
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	(bp_offp), hl
	ld	hl, (bp_tl)
	add	hl, de
	CLRCELL
	pop	bc
	djnz	.l
	ret

	IFDEF	GROBDA_EXTDATA
;=====================================================================
; [M8⑥] Grobda(動く地上戦車, 動的スプライト)。world列 grob_wcol でスクロール追従
;   (screen_ex=(wcol-coarse_lo)&0xFF, <COLS で表示)。道沿いに vy(6..18)上下移動。
;   PCG=GROBDA_BASE(0xF8, 2コマ)。[次段] 照準接近で前進(flee) + ブラスター被弾で爆発。
GROBDA_BASE	EQU	0xF8
MAX_GROBDA	EQU	3
GROB_MOVE	EQU	12		; vy 1歩の間隔(フレーム)
GROB_AMOVE	EQU	16		; アニメ間隔
GROB_FLEE	EQU	5		; 照準がこのセル差以内で前進(逃げる)
	IFDEF	ALLAREAS
grob_wtab:	ds	MAX_GROBDA	; [⑦] 初期 world列(gobj_load がエリアごとに設定)
grob_rtab:	ds	MAX_GROBDA	; エリアごとの row(vy)
grob_n:		db	0		; エリアの Grobda 数
	ELSE
grob_wtab:	db	94, 91, 88	; 初期 world列(xevi type5, row9)
	ENDIF

gen_grobda:
	ld	hl, grobda_data
	ld	a, GROBDA_BASE
	ld	b, 8
	jp	gen_pcg_set

grobda_init:
	xor	a
	ld	(spr_i), a
.il:	ld	a, (spr_i)
	cp	MAX_GROBDA
	ret	z
	ld	e, a
	ld	d, 0
	IFDEF	ALLAREAS
	; [⑦] active = (spr_i < grob_n)。超過 slot は非アクティブ
	ld	a, (grob_n)
	ld	c, a
	ld	a, (spr_i)
	cp	c
	jr	c, .gi_on
	ld	hl, grob_active
	add	hl, de
	ld	(hl), 0
	jp	.gi_next
.gi_on:
	ENDIF
	ld	hl, grob_active
	add	hl, de
	ld	(hl), 1
	ld	hl, grob_wtab
	add	hl, de
	ld	a, (hl)
	ld	hl, grob_wcol
	add	hl, de
	ld	(hl), a
	ld	hl, grob_vy
	add	hl, de
	IFDEF	ALLAREAS
	push	hl
	ld	hl, grob_rtab
	add	hl, de
	ld	a, (hl)			; エリアごとの row
	pop	hl
	ld	(hl), a
	ELSE
	ld	(hl), 9
	ENDIF
	ld	hl, grob_frm
	add	hl, de
	ld	(hl), 0
	ld	hl, grob_dir
	add	hl, de
	ld	(hl), 0
	ld	a, (spr_i)
	inc	a
	ld	hl, grob_mtmr
	add	hl, de
	ld	(hl), a
	ld	hl, grob_atmr
	add	hl, de
	ld	(hl), a
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, grob_prev
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ld	hl, grob_prev2
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
.gi_next:
	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jp	.il

; 毎フレーム: アニメ(2コマ) + 道(vy)移動/flee + screen_ex 計算。
grobda_move:
	; 照準の画面セル(flee判定用)
	ld	a, (ship_hx)
	add	a, RETICLE_DIST
	srl	a
	srl	a
	ld	(grob_retx), a
	ld	a, (ship_vy)
	add	a, 2
	srl	a
	srl	a
	ld	(grob_rety), a
	xor	a
	ld	(spr_i), a
.ml:	ld	a, (spr_i)
	cp	MAX_GROBDA
	ret	z
	ld	e, a
	ld	d, 0
	ld	hl, grob_active
	add	hl, de
	ld	a, (hl)
	or	a
	jp	z, .mn
	ld	hl, grob_wcol		; grob_ex = (wcol - coarse_lo) & 0xFF
	add	hl, de
	ld	a, (hl)
	push	de
	ld	hl, coarse
	sub	(hl)
	pop	de
	ld	hl, grob_ex
	add	hl, de
	ld	(hl), a
	ld	hl, grob_dirty		; dirty=0 初期
	add	hl, de
	ld	(hl), 0
	ld	hl, grob_atmr		; アニメ
	add	hl, de
	dec	(hl)
	jr	nz, .amv
	ld	(hl), GROB_AMOVE
	ld	hl, grob_frm
	add	hl, de
	ld	a, (hl)
	inc	a
	and	1
	ld	(hl), a
	ld	hl, grob_dirty		; アニメ変化 → dirty
	add	hl, de
	ld	(hl), 1
.amv:
	ld	hl, grob_mtmr		; 道(vy)移動
	add	hl, de
	dec	(hl)
	jp	nz, .mn
	ld	(hl), GROB_MOVE
	ld	hl, grob_dirty		; 移動 → dirty
	add	hl, de
	ld	(hl), 1
	; flee: 照準が近い(dx,dy<=GROB_FLEE)なら前進(wcol++ = 右へ逃げる)
	call	grob_near_reticle	; de=slot → C(near)
	jr	nc, .road
	ld	hl, grob_wcol
	add	hl, de
	inc	(hl)
	jp	.mn
.road:
	ld	hl, grob_dir
	add	hl, de
	ld	a, (hl)
	or	a
	jr	nz, .gup
	ld	hl, grob_vy		; dir=0 下
	add	hl, de
	ld	a, (hl)
	inc	a
	ld	(hl), a
	cp	18
	jp	c, .mn
	ld	hl, grob_dir
	add	hl, de
	ld	(hl), 1
	jp	.mn
.gup:	ld	hl, grob_vy		; dir=1 上
	add	hl, de
	ld	a, (hl)
	dec	a
	ld	(hl), a
	cp	6
	jp	nc, .mn
	ld	hl, grob_dir
	add	hl, de
	ld	(hl), 0
.mn:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jp	.ml

; grobda_soff: de=slot。on-screen(grob_ex<COLS)なら spr_soff 設定+scf、off は nc。
grobda_soff:
	ld	hl, grob_ex
	add	hl, de
	ld	a, (hl)
	cp	COLS
	ret	nc
	ld	c, a			; ex
	ld	hl, grob_vy
	add	hl, de
	ld	a, (hl)
	ld	h, 0
	ld	l, a
	add	hl, hl
	add	hl, hl
	add	hl, hl			; *8
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, hl			; *32
	add	hl, de			; *40
	ld	a, c
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	(spr_soff), hl
	scf
	ret

grobda_erase:
	xor	a
	ld	(spr_i), a
.el:	ld	a, (spr_i)
	cp	MAX_GROBDA
	ret	z
	; idle-skip: scroll 不変かつ不動なら消さない
	ld	a, (scroll_moved)
	or	a
	jr	nz, .edo
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, grob_dirty
	add	hl, de
	ld	a, (hl)
	or	a
	jr	z, .en
.edo:
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, grob_prev
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	a, h
	cp	0xFF
	jr	z, .en
	call	ene_clr2x2
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, grob_prev2
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	call	ene_clr2x2
.en:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jr	.el

grobda_draw:
	xor	a
	ld	(spr_i), a
.dl:	ld	a, (spr_i)
	cp	MAX_GROBDA
	ret	z
	ld	e, a
	ld	d, 0
	ld	hl, grob_active
	add	hl, de
	ld	a, (hl)
	or	a
	jp	z, .dinv
	; idle-skip: scroll 不変かつ不動なら描き直さない(prev 維持)
	ld	a, (scroll_moved)
	or	a
	jr	nz, .ddo
	ld	hl, grob_dirty
	add	hl, de
	ld	a, (hl)
	or	a
	jp	z, .dnx
.ddo:
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	call	grobda_soff		; de=slot → spr_soff / Cフラグ
	jp	nc, .dinv		; off-screen → 描かず prev 無効化
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, grob_frm
	add	hl, de
	ld	a, (hl)
	add	a, a
	add	a, a
	add	a, GROBDA_BASE
	ld	(spr_code), a
	; 窓1
	ld	hl, (spr_soff)
	ld	de, (cur_pos)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(spr_cell), hl
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, grob_prev
	add	hl, de
	ld	a, (spr_cell)
	ld	(hl), a
	inc	hl
	ld	a, (spr_cell + 1)
	ld	(hl), a
	ld	hl, (spr_cell)
	ld	a, (spr_code)
	call	ene_put2x2
	; 窓2
	ld	hl, (cur_pos)
	ld	a, h
	xor	0x04
	ld	h, a
	ld	de, (spr_soff)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(spr_cell), hl
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, grob_prev2
	add	hl, de
	ld	a, (spr_cell)
	ld	(hl), a
	inc	hl
	ld	a, (spr_cell + 1)
	ld	(hl), a
	ld	hl, (spr_cell)
	ld	a, (spr_code)
	call	ene_put2x2
	jr	.dnx
.dinv:
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, grob_prev
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ld	hl, grob_prev2
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
.dnx:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jp	.dl

; grob_near_reticle: de=slot。照準セルと Grobda(grob_ex,grob_vy)の差が共に
;   GROB_FLEE 以内なら scf、否 nc。de 保存。
grob_near_reticle:
	ld	hl, grob_ex
	add	hl, de
	ld	a, (hl)
	ld	hl, grob_retx
	sub	(hl)
	call	grob_abs
	cp	GROB_FLEE + 1
	jr	nc, .far
	ld	hl, grob_vy
	add	hl, de
	ld	a, (hl)
	ld	hl, grob_rety
	sub	(hl)
	call	grob_abs
	cp	GROB_FLEE + 1
	jr	nc, .far
	scf
	ret
.far:	or	a
	ret
; grob_abs: a = |a| (符号付き)
grob_abs:
	bit	7, a
	ret	z
	neg
	ret

; grobda_hit_check: ブラスター着弾(col_ex,col_ey 画面セル)が Grobda に当たれば
;   破壊(active=0)。爆発は呼出側 .nobj の expl_spawn が出す。
;   ※blaster_move のループ変数 spr_i を壊さないよう専用 grob_hi を使う。
grobda_hit_check:
	xor	a
	ld	(grob_hi), a
.hl:	ld	a, (grob_hi)
	cp	MAX_GROBDA
	ret	z
	ld	e, a
	ld	d, 0
	ld	hl, grob_active
	add	hl, de
	ld	a, (hl)
	or	a
	jr	z, .hn
	ld	hl, grob_ex
	add	hl, de
	ld	a, (hl)
	cp	COLS
	jr	nc, .hn			; off-screen
	ld	c, a			; grob_ex
	ld	a, (col_ex)
	sub	c
	call	grob_abs
	cp	2			; |dx|<=1
	jr	nc, .hn
	ld	hl, grob_vy
	add	hl, de
	ld	a, (hl)
	ld	c, a
	ld	a, (col_ey)
	sub	c
	call	grob_abs
	cp	2			; |dy|<=1
	jr	nc, .hn
	ld	hl, grob_active		; ヒット → 破壊
	add	hl, de
	ld	(hl), 0
	ld	hl, grob_dirty		; 今フレーム消去させる(ゴースト防止)
	add	hl, de
	ld	(hl), 1
	ret
.hn:	ld	a, (grob_hi)
	inc	a
	ld	(grob_hi), a
	jr	.hl

; [M8⑥(A)] 画面内の Grobda 数 → a。敵の実効同時上限を下げる(負荷分散)。
count_grob_onscreen:
	xor	a
	ld	(grob_hi), a
	ld	c, 0
.l:	ld	a, (grob_hi)
	cp	MAX_GROBDA
	jr	z, .done
	ld	e, a
	ld	d, 0
	ld	hl, grob_active
	add	hl, de
	ld	a, (hl)
	or	a
	jr	z, .nx
	ld	hl, grob_ex
	add	hl, de
	ld	a, (hl)
	cp	COLS
	jr	nc, .nx
	inc	c
.nx:	ld	a, (grob_hi)
	inc	a
	ld	(grob_hi), a
	jr	.l
.done:	ld	a, c
	ret
	ENDIF

; hl=top-left セル。2x2 を消去。
ene_clr2x2:
	ld	(ene_tl), hl
	CLRCELL
	ld	hl, (ene_tl)
	inc	hl
	CLRCELL
	ld	hl, (ene_tl)
	ld	de, COLS
	add	hl, de
	CLRCELL
	ld	hl, (ene_tl)
	ld	de, COLS + 1
	add	hl, de
	CLRCELL
	ret

;=====================================================================
; [敵弾] スパリオ: EBULLET_BASE(0x00..0x03, 16x16=2x2セル)。位置は 16bit 固定小数
;   (セル<<8 | frac)、画面セル座標。速度 ebul_vx/vy(dw 符号付, frac/frame)。
;   enemies 同様テキスト面の 2 窓(cur_pos / cur_pos^page)へ 2x2 描画。
EBULLET_BASE	EQU	0x00
MAX_EBUL	EQU	4		; (perf) 同時敵弾数

gen_ebullet:
	ld	a, EBULLET_BASE
	ld	b, 4
	ld	hl, ebullet_data
	jp	gen_pcg_set

ebul_init:
	ld	hl, ebul_active
	ld	b, MAX_EBUL
	xor	a
.il:	ld	(hl), a
	inc	hl
	djnz	.il
	ld	(ebul_cnt), a		; = 0
	ret

; ebul_spawn: HL=x(cell<<8|frac), DE=y, (ebul_svx)/(ebul_svy)=速度。空き slot へ。
ebul_spawn:
	ld	(eb_sx), hl
	ld	(eb_sy), de
	ld	c, 0			; index
	ld	hl, ebul_active
.fl:	ld	a, (hl)
	or	a
	jr	z, .found
	inc	hl
	inc	c
	ld	a, c
	cp	MAX_EBUL
	jr	nz, .fl
	ret				; 空き無し
.found:
	ld	(hl), 1			; active
	ld	hl, ebul_cnt
	inc	(hl)
	; e = index*2 (dw 配列)
	ld	a, c
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, ebul_x
	add	hl, de
	ld	bc, (eb_sx)
	ld	(hl), c
	inc	hl
	ld	(hl), b			; x
	ld	hl, ebul_y
	add	hl, de
	ld	bc, (eb_sy)
	ld	(hl), c
	inc	hl
	ld	(hl), b			; y
	ld	hl, ebul_vx
	add	hl, de
	ld	bc, (ebul_svx)
	ld	(hl), c
	inc	hl
	ld	(hl), b
	ld	hl, ebul_vy
	add	hl, de
	ld	bc, (ebul_svy)
	ld	(hl), c
	inc	hl
	ld	(hl), b
	; prev 無効(0xFFFF)
	ld	hl, ebul_prev1
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ld	hl, ebul_prev2
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ret

; ene_fire_check: 各敵(type≠0)の射撃タイマを更新、0 で自機狙いの弾を発射。
ENE_FIRE_SPEED	EQU	0x60		; 直進速度(frac/frame, 0.375セル)
ENE_FIRE_DIAG	EQU	0x44		; 斜め速度(≈0.375*0.707)
ene_fire_check:
	ld	a, (game_over)
	or	a
	ret	nz			; ゲームオーバー中は発射しない
	xor	a
	ld	(spr_i), a
.fcl:	ld	a, (spr_i)
	cp	MAX_ENEMIES
	ret	z
	ld	e, a
	ld	d, 0
	ld	hl, ene_active
	add	hl, de
	ld	a, (hl)
	or	a
	jp	z, .fcn
	ld	hl, ene_type
	add	hl, de
	ld	a, (hl)
	or	a
	jr	z, .fcn			; Toroid(0) は撃たない
	ld	hl, ene_ftmr
	add	hl, de
	dec	(hl)
	jr	nz, .fcn
	call	ene_fire		; spr_i の敵が発射
	; タイマ再設定 = ((framecnt&7)+1)*8
	ld	a, (framecnt)
	and	7
	inc	a
	add	a, a
	add	a, a
	add	a, a
	ld	c, a
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, ene_ftmr
	add	hl, de
	ld	(hl), c
.fcn:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jp	.fcl

; ene_fire: spr_i の敵から自機狙いの弾を1発。8方向近似。
ene_fire:
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, ene_ex
	add	hl, de
	ld	a, (hl)
	ld	(ef_ex), a
	ld	hl, ene_ey
	add	hl, de
	ld	a, (hl)
	ld	(ef_ey), a
	; dx = (ship_hx>>2) - ef_ex
	ld	a, (ship_hx)
	srl	a
	srl	a
	ld	hl, ef_ex
	sub	(hl)
	ld	(ef_dx), a
	; dy = (ship_vy>>2) - ef_ey
	ld	a, (ship_vy)
	srl	a
	srl	a
	ld	hl, ef_ey
	sub	(hl)
	ld	(ef_dy), a
	; adx = |dx|
	ld	a, (ef_dx)
	bit	7, a
	jr	z, .padx
	neg
.padx:	ld	(ef_adx), a
	; ady = |dy|
	ld	a, (ef_dy)
	bit	7, a
	jr	z, .pady
	neg
.pady:	ld	(ef_ady), a
	; 方向: adx >= 2*ady → 横 / ady >= 2*adx → 縦 / else 斜め
	ld	a, (ef_ady)
	add	a, a
	ld	b, a			; 2*ady
	ld	a, (ef_adx)
	cp	b
	jr	nc, .horiz
	ld	a, (ef_adx)
	add	a, a
	ld	b, a			; 2*adx
	ld	a, (ef_ady)
	cp	b
	jr	nc, .vert
	; 斜め
	ld	a, ENE_FIRE_DIAG
	ld	(ef_vmx), a
	ld	(ef_vmy), a
	jr	.applysign
.horiz:	ld	a, ENE_FIRE_SPEED
	ld	(ef_vmx), a
	xor	a
	ld	(ef_vmy), a
	jr	.applysign
.vert:	xor	a
	ld	(ef_vmx), a
	ld	a, ENE_FIRE_SPEED
	ld	(ef_vmy), a
.applysign:
	; vx = sign(dx) 適用
	ld	a, (ef_vmx)
	ld	e, a
	ld	d, 0
	ld	a, (ef_dx)
	bit	7, a
	jr	z, .vxp
	ld	hl, 0
	or	a
	sbc	hl, de
	ld	(ebul_svx), hl
	jr	.vydo
.vxp:	ld	(ebul_svx), de
.vydo:	ld	a, (ef_vmy)
	ld	e, a
	ld	d, 0
	ld	a, (ef_dy)
	bit	7, a
	jr	z, .vyp
	ld	hl, 0
	or	a
	sbc	hl, de
	ld	(ebul_svy), hl
	jr	.spawn
.vyp:	ld	(ebul_svy), de
.spawn:
	; x = ef_ex<<8, y = ef_ey<<8
	ld	a, (ef_ex)
	ld	h, a
	ld	l, 0
	ld	a, (ef_ey)
	ld	d, a
	ld	e, 0
	call	ebul_spawn
	ret

; ship_hit: 自機被弾。無敵/ゲームオーバー中は無視。爆発+残機-1。残機0でゲームオーバー。
;   敵弾はクリアしない(無敵 64 フレームで保護、弾は通常 cull)。
SHIP_INV_FRAMES	EQU	64
ship_hit:
	ld	a, (game_over)
	or	a
	ret	nz
	ld	a, (ship_inv)
	or	a
	ret	nz			; 無敵中
	; 爆発を自機セルへ
	ld	a, (ship_hx)
	srl	a
	srl	a
	ld	(col_ex), a
	ld	a, (ship_vy)
	srl	a
	srl	a
	ld	(col_ey), a
	call	expl_spawn
	IFDEF	SOUND
	ld	hl, se_exp_aerial	; [SND] 自機爆発音
	call	snd_play_se
	ENDIF
	; 残機
	ld	a, (lives)
	dec	a
	ld	(lives), a
	jr	z, .shgo
	ld	a, SHIP_INV_FRAMES
	ld	(ship_inv), a		; 復活無敵
	ret
.shgo:
	ld	a, 1
	ld	(game_over), a
	ret

; ship_tick: 毎フレーム。無敵タイマ減算。
ship_tick:
	ld	a, (ship_inv)
	or	a
	ret	z
	dec	a
	ld	(ship_inv), a
	ret

; ebul_erase_prev: eb_i の prev1/prev2 が有効(≠0xFFFF)なら 2窓を ene_clr2x2 で消去。
ebul_erase_prev:
	ld	a, (eb_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, ebul_prev1
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; hl = prev1
	ld	a, h
	and	l
	inc	a
	ret	z			; 0xFFFF → 消去不要
	call	ene_clr2x2		; prev1
	ld	a, (eb_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, ebul_prev2
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	call	ene_clr2x2		; prev2
	ret

; ebul_update: 毎フレーム。各弾を 移動→cull/被弾→idle-skip描画(セル不変なら省略)。
ebul_update:
	ld	a, (ebul_cnt)
	or	a
	ret	z			; アクティブ弾なし → early-out
	xor	a
	ld	(eb_i), a
.ul:	ld	a, (eb_i)
	cp	MAX_EBUL
	ret	z
	ld	e, a
	ld	d, 0
	ld	hl, ebul_active
	add	hl, de
	ld	a, (hl)
	or	a
	jp	z, .next
	; (消去は idle-skip のため描画/消滅時のみ。ebul_erase_prev)
	; --- 移動: x += vx, y += vy ---
	ld	a, (eb_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, ebul_x
	add	hl, de
	ld	c, (hl)
	inc	hl
	ld	b, (hl)			; bc = x
	push	hl
	ld	hl, ebul_vx
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; hl = vx
	add	hl, bc			; x + vx
	pop	de			; de -> ebul_x[i]+1 (high)
	ld	a, l
	ld	(ebul_xtmp), a
	ld	a, h
	ld	(ebul_xtmp + 1), a
	; 書き戻し
	ld	a, (eb_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, ebul_x
	add	hl, de
	ld	a, (ebul_xtmp)
	ld	(hl), a
	inc	hl
	ld	a, (ebul_xtmp + 1)
	ld	(hl), a			; x 保存。high=col
	; y += vy
	ld	hl, ebul_y
	add	hl, de
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	push	hl
	ld	hl, ebul_vy
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	add	hl, bc
	pop	bc
	ld	a, l
	ld	(ebul_ytmp), a
	ld	a, h
	ld	(ebul_ytmp + 1), a
	ld	a, (eb_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, ebul_y
	add	hl, de
	ld	a, (ebul_ytmp)
	ld	(hl), a
	inc	hl
	ld	a, (ebul_ytmp + 1)
	ld	(hl), a
	; --- cull: col=xtmp hi, row=ytmp hi。>=COLS/ROWS(左右/上下はみ出し)で消去 ---
	ld	a, (ebul_xtmp + 1)	; col
	cp	COLS
	jp	nc, .kill
	ld	a, (ebul_ytmp + 1)	; row
	cp	ROWS
	jp	nc, .kill
	; --- 自機衝突: 弾2x2 と 自機3x3 の重なり(col in [sc-1,sc+2], row in [sr-1,sr+2]) ---
	ld	a, (ship_hx)
	srl	a
	srl	a
	ld	c, a			; sc
	ld	a, (ebul_xtmp + 1)
	sub	c
	add	a, 1			; col - sc + 1
	cp	4
	jr	nc, .noship
	ld	a, (ship_vy)
	srl	a
	srl	a
	ld	c, a			; sr
	ld	a, (ebul_ytmp + 1)
	sub	c
	add	a, 1
	cp	4
	jr	nc, .noship
	call	ship_hit
	jp	.kill			; 被弾した弾は消滅(消去は .kill で active=0, 描画せず)
.noship:
	; --- 描画: soff = row*40 + col ---
	ld	a, (ebul_ytmp + 1)
	ld	l, a
	ld	h, 0
	add	hl, hl
	add	hl, hl
	add	hl, hl			; *8
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, hl			; *32
	add	hl, de			; *40
	ld	a, (ebul_xtmp + 1)
	ld	e, a
	ld	d, 0
	add	hl, de			; soff
	ld	(eb_soff), hl
	; 窓1 cell = newc1 = (soff+cur_pos)&0x7FF
	ld	de, (cur_pos)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(eb_newc1), hl		; newc1
	; --- idle-skip: prev1 が有効 かつ prev1==newc1 なら再描画省略 ---
	ld	a, (eb_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, ebul_prev1
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; hl = prev1
	ld	a, h
	and	l
	inc	a
	jr	z, .dodraw		; prev1==0xFFFF(新規)→描画
	ld	de, (eb_newc1)
	ld	a, l
	cp	e
	jr	nz, .dodraw
	ld	a, h
	cp	d
	jr	nz, .dodraw
	jp	.next			; セル不変 → スキップ(スプライト維持)
.dodraw:
	call	ebul_erase_prev		; 旧位置を消去(有効時のみ)
	; 窓1 描画 @ newc1
	ld	hl, (eb_newc1)
	ld	a, EBULLET_BASE
	call	ene_put2x2
	ld	a, (eb_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, ebul_prev1
	add	hl, de
	ld	de, (eb_newc1)
	ld	(hl), e
	inc	hl
	ld	(hl), d
	; 窓2: cell=(soff + (cur_pos^0x0400))&0x7FF
	ld	hl, (cur_pos)
	ld	a, h
	xor	0x04
	ld	h, a
	ld	de, (eb_soff)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	push	hl
	ld	a, EBULLET_BASE
	call	ene_put2x2
	ld	a, (eb_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, ebul_prev2
	add	hl, de
	pop	bc
	ld	(hl), c
	inc	hl
	ld	(hl), b
	jp	.next
.kill:
	call	ebul_erase_prev		; 消滅: 旧位置を消去
	ld	a, (eb_i)
	ld	e, a
	ld	d, 0
	ld	hl, ebul_active
	add	hl, de
	ld	(hl), 0
	ld	hl, ebul_cnt
	dec	(hl)
.next:
	ld	a, (eb_i)
	inc	a
	ld	(eb_i), a
	jp	.ul

;=====================================================================
; [敵] Bacura: 破壊不可の板(回転は MVP 省略)。world列固定でスクロール(scroll_moved で sx--)。
;   ショット貫通(弾衝突なし)・接触死(自機3x3 と重なりで ship_hit)。BAC_SPAWN_INTERVAL 毎に
;   1体増殖(最大 MAX_BACURA)。area 切替で bacura_init でクリア。
MAX_BACURA	EQU	2
BAC_SPAWN_INTERVAL	EQU	60

bacura_init:
	ld	hl, bac_active
	ld	b, MAX_BACURA
	xor	a
.bil:	ld	(hl), a
	inc	hl
	djnz	.bil
	ld	a, BAC_SPAWN_INTERVAL
	ld	(bac_spawn_tmr), a
	ret

; bacura_spawn: 空き slot に 1体。sx=COLS+1(画面外右), row=(framecnt&0x0F)+2
bacura_spawn:
	ld	c, 0
	ld	hl, bac_active
.bfl:	ld	a, (hl)
	or	a
	jr	z, .bfound
	inc	hl
	inc	c
	ld	a, c
	cp	MAX_BACURA
	jr	nz, .bfl
	ret				; 空き無し
.bfound:
	ld	(hl), 1
	ld	a, c
	ld	e, a
	ld	d, 0
	ld	hl, bac_sx
	add	hl, de
	ld	(hl), COLS + 1
	ld	hl, bac_row
	add	hl, de
	ld	a, (framecnt)
	and	0x0F
	add	a, 2
	ld	(hl), a
	ld	a, c
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bac_prev1
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ld	hl, bac_prev2
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ret

; bacura_erase_prev: bac_i の prev1/prev2 が有効なら 2窓消去
bacura_erase_prev:
	ld	a, (bac_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bac_prev1
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	a, h
	and	l
	inc	a
	ret	z
	call	ene_clr2x4
	ld	a, (bac_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bac_prev2
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	call	ene_clr2x4
	ret

bacura_update:
	; 回転コマ = 進んだ距離(coarse スクロールセル)&7 → ROMどおり距離連動(スクロール停止で停止)。
	;   全 Bacura 共通。coarse 1セル進むごとに1コマ(一周8セル)。PCG書換ゼロ。
	ld	a, (coarse)
	and	7
	add	a, a			; コマ*8 = frametab 行オフセット
	ld	e, a
	ld	d, 0
	ld	hl, bacura_frametab
	add	hl, de
	ld	(bac_ftptr), hl
	ld	a, (bac_spawn_tmr)
	dec	a
	ld	(bac_spawn_tmr), a
	jr	nz, .bns
	ld	a, BAC_SPAWN_INTERVAL
	ld	(bac_spawn_tmr), a
	call	bacura_spawn
.bns:
	xor	a
	ld	(bac_i), a
.bul:	ld	a, (bac_i)
	cp	MAX_BACURA
	ret	z
	ld	e, a
	ld	d, 0
	ld	hl, bac_active
	add	hl, de
	ld	a, (hl)
	or	a
	jp	z, .bnext
	; scroll_moved なら sx--
	ld	a, (scroll_moved)
	or	a
	jr	z, .bnosc
	ld	hl, bac_sx
	add	hl, de
	dec	(hl)
.bnosc:
	ld	hl, bac_sx
	add	hl, de
	ld	a, (hl)
	ld	(bac_cursx), a
	cp	0x80
	jr	c, .bnotgone
	; 左に抜けた → 消去 + deactivate
	call	bacura_erase_prev
	ld	a, (bac_i)
	ld	e, a
	ld	d, 0
	ld	hl, bac_active
	add	hl, de
	ld	(hl), 0
	jp	.bnext
.bnotgone:
	cp	COLS
	jr	c, .bvis
	; 右・非表示 → 消去 + prev無効
	call	bacura_erase_prev
	ld	a, (bac_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bac_prev1
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	jp	.bnext
.bvis:
	; 衝突: bac 2x2(sx,row) vs 自機 3x3
	ld	a, (ship_hx)
	srl	a
	srl	a
	ld	c, a
	ld	a, (bac_cursx)
	sub	c
	add	a, 1
	cp	4
	jr	nc, .bnohit
	ld	a, (bac_i)
	ld	e, a
	ld	d, 0
	ld	hl, bac_row
	add	hl, de
	ld	a, (hl)
	ld	b, a
	ld	a, (ship_vy)
	srl	a
	srl	a
	ld	c, a
	ld	a, b
	sub	c
	add	a, 2			; Bacura 高さ4セル vs 自機3セル → 重なり窓6
	cp	6
	jr	nc, .bnohit
	call	ship_hit		; 接触死(無敵中は無視)
.bnohit:
	; idle-skip: scroll_moved でなく prev1 有効なら再描画省略(静止)
	ld	a, (scroll_moved)
	or	a
	jr	nz, .bdraw
	ld	a, (bac_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bac_prev1
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	a, (hl)			; prev1 hi
	inc	a			; 0xFF→0x00
	jp	nz, .bnext		; prev 有効(hi≠0xFF)かつ非スクロール → スキップ
.bdraw:
	; 描画: soff=row*40+sx, 2窓(erase prev + put)
	call	bacura_erase_prev
	ld	a, (bac_i)
	ld	e, a
	ld	d, 0
	ld	hl, bac_row
	add	hl, de
	ld	a, (hl)
	ld	l, a
	ld	h, 0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, hl
	add	hl, de			; row*40
	ld	a, (bac_cursx)
	ld	e, a
	ld	d, 0
	add	hl, de			; soff
	ld	(bac_soff), hl
	ld	de, (cur_pos)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	push	hl
	ld	de, (bac_ftptr)		; 現コマの frametab 行(8コード)
	call	bac_put2x4
	ld	a, (bac_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bac_prev1
	add	hl, de
	pop	bc
	ld	(hl), c
	inc	hl
	ld	(hl), b
	ld	hl, (cur_pos)
	ld	a, h
	xor	0x04
	ld	h, a
	ld	de, (bac_soff)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	push	hl
	ld	de, (bac_ftptr)		; 現コマの frametab 行(8コード)
	call	bac_put2x4
	ld	a, (bac_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, bac_prev2
	add	hl, de
	pop	bc
	ld	(hl), c
	inc	hl
	ld	(hl), b
.bnext:
	ld	a, (bac_i)
	inc	a
	ld	(bac_i), a
	jp	.bul

;=====================================================================
; 爆発 PCG 生成: expl_data(3フレーム×96B = 12セル×24B)を EXPL_BASE から定義。
gen_expl:
	ld	hl, expl_data
	ld	a, EXPL_BASE
	ld	(ene_code), a
	ld	b, 12
.el:	push	bc
	ld	a, (ene_code)
	call	pcg_select
	ld	d, PCG_B >> 8
	call	def_plane
	ld	d, PCG_R >> 8
	call	def_plane
	ld	d, PCG_G >> 8
	call	def_plane
	ld	a, (ene_code)
	inc	a
	ld	(ene_code), a
	pop	bc
	djnz	.el
	ret

; (col_ex, col_ey) に爆発を1つ発生(空きスロットがあれば)。spr_k を使用
;   (collision_check が spr_i/spr_j を使用中のため別 index)。
expl_spawn:
	xor	a
	ld	(spr_k), a
.fs:	ld	a, (spr_k)
	cp	MAX_EXPL
	ret	z			; 空き無し
	ld	e, a
	ld	d, 0
	ld	hl, exp_active
	add	hl, de
	ld	a, (hl)
	or	a
	jr	z, .go
	ld	a, (spr_k)
	inc	a
	ld	(spr_k), a
	jr	.fs
.go:
	ld	(hl), 1
	ld	hl, exp_ex
	add	hl, de
	ld	a, (col_ex)
	ld	(hl), a
	ld	hl, exp_ey
	add	hl, de
	ld	a, (col_ey)
	ld	(hl), a
	ld	hl, exp_frm
	add	hl, de
	ld	(hl), 0
	ld	hl, exp_tmr
	add	hl, de
	ld	(hl), EXPL_ANIM
	ld	hl, exp_prev
	add	hl, de
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ld	hl, exp_prev2		; 窓2 も無効化
	add	hl, de
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ret

; 全爆発を前回位置(両窓, 2x2)から消去。
expls_erase:
	xor	a
	ld	(spr_i), a
.el:	ld	a, (spr_i)
	cp	MAX_EXPL
	ret	z
	; [M8] idle-skip: active かつ変化なし(scroll_moved/dirty 無)なら消去しない。
	;   非active(死亡)は常に両窓消去+無効化(ゴースト防止)。
	ld	e, a
	ld	d, 0
	ld	hl, exp_active
	add	hl, de
	ld	a, (hl)
	or	a
	jr	z, .doer
	ld	a, (scroll_moved)
	ld	hl, exp_dirty
	add	hl, de
	or	(hl)
	jp	z, .en
.doer:
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, exp_prev
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	a, h
	cp	0xFF
	jp	z, .en
	call	ene_clr2x2
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, exp_prev2
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	call	ene_clr2x2
	; 非アクティブなら両窓 slot を無効化
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, exp_active
	add	hl, de
	ld	a, (hl)
	or	a
	jp	nz, .en
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, exp_prev
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, exp_prev2
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
.en:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jp	.el

; 全爆発のコマ送り。3コマ終了で消滅。
expls_anim:
	xor	a
	ld	(spr_i), a
.al:	ld	a, (spr_i)
	cp	MAX_EXPL
	ret	z
	ld	e, a
	ld	d, 0
	ld	hl, exp_active
	add	hl, de
	ld	a, (hl)
	or	a
	jr	z, .an
	ld	hl, exp_dirty		; [M8] dirty=0 (初期)。アニメ変化/消滅で 1
	add	hl, de
	ld	(hl), 0
	ld	hl, exp_tmr
	add	hl, de
	dec	(hl)
	jr	nz, .an
	ld	(hl), EXPL_ANIM
	ld	hl, exp_frm
	add	hl, de
	ld	a, (hl)
	inc	a
	ld	(hl), a
	push	af			; 新フレーム退避
	ld	hl, exp_dirty		; アニメ変化 → dirty
	add	hl, de
	ld	(hl), 1
	pop	af			; 新フレーム復帰
	cp	3
	jr	c, .an
	ld	hl, exp_active
	add	hl, de
	ld	(hl), 0			; 3コマ終了 → 消滅
.an:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jr	.al

; 全爆発を描画(両窓, 2x2), prev 記録。
expls_draw:
	xor	a
	ld	(spr_i), a
.dl:	ld	a, (spr_i)
	cp	MAX_EXPL
	ret	z
	ld	e, a
	ld	d, 0
	ld	hl, exp_active
	add	hl, de
	ld	a, (hl)
	or	a
	jp	z, .dn
	; [M8] idle-skip: prev 有効 かつ変化なし(scroll_moved/dirty 無)なら描き直さない
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, exp_prev + 1	; 窓1 prev 高位
	add	hl, de
	ld	a, (hl)
	cp	0xFF
	jr	z, .edraw		; prev 無効 → 描画
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	a, (scroll_moved)
	ld	hl, exp_dirty
	add	hl, de
	or	(hl)
	jp	z, .dn			; 変化なし → skip
.edraw:
	ld	a, (spr_i)		; de = i に復元
	ld	e, a
	ld	d, 0
	; soff = ey*COLS + ex
	ld	hl, exp_ey
	add	hl, de
	ld	a, (hl)
	ld	h, 0
	ld	l, a
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, hl
	add	hl, de			; *40
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	push	hl
	ld	hl, exp_ex
	add	hl, de
	ld	a, (hl)
	pop	hl
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	(spr_soff), hl
	; code base = EXPL_BASE + frame*4
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, exp_frm
	add	hl, de
	ld	a, (hl)
	add	a, a
	add	a, a
	add	a, EXPL_BASE
	ld	(spr_code), a
	; 窓1
	ld	hl, (spr_soff)
	ld	de, (cur_pos)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(spr_cell), hl
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, exp_prev
	add	hl, de
	ld	a, (spr_cell)
	ld	(hl), a
	inc	hl
	ld	a, (spr_cell + 1)
	ld	(hl), a
	ld	hl, (spr_cell)
	ld	a, (spr_code)
	call	ene_put2x2
	; 窓2
	ld	hl, (cur_pos)
	ld	a, h
	xor	0x04
	ld	h, a
	ld	de, (spr_soff)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(spr_cell), hl
	ld	a, (spr_i)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, exp_prev2
	add	hl, de
	ld	a, (spr_cell)
	ld	(hl), a
	inc	hl
	ld	a, (spr_cell + 1)
	ld	(hl), a
	ld	hl, (spr_cell)
	ld	a, (spr_code)
	call	ene_put2x2
.dn:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jp	.dl

;=====================================================================
; データ / RAM
SPR_ZERO:	db	0,0,0,0,0,0,0,0		; def_plane 用 0 プレーン

sp_vh:		db	0
sp_vv:		db	0
spr_soff:	dw	0
spr_code:	db	0
spr_i:		db	0		; ループ index (レジスタ c は clr/put_cell で壊れるため)
spr_cell:	dw	0		; 描画先セル一時
fire_tmr:	db	0
bul_spawn_vy:	db	0		; spawn_bullet の発射 vy 一時
bul_active:	ds	MAX_BULLETS
bul_hx:		ds	MAX_BULLETS		; 横 2px単位
bul_vy:		ds	MAX_BULLETS		; 縦 2ライン単位
bul_prev:	ds	MAX_BULLETS * 2		; 前回 top-left セル 窓1
bul_prev2:	ds	MAX_BULLETS * 2		; 窓2
bfire_tmr:	db	0		; [M8] ブラスター連射タイマ
bla_active:	ds	MAX_BLASTER
bla_hx:		ds	MAX_BLASTER		; 横 2px単位
bla_vy:		ds	MAX_BLASTER		; 縦 2ライン単位
bla_tx:		ds	MAX_BLASTER		; 着弾 x (hx 2px単位)
bla_prev:	ds	MAX_BLASTER * 2		; 前回 top-left 窓1
bla_prev2:	ds	MAX_BLASTER * 2		; 窓2
; [M8-3] 地上物: 破壊フラグ(RAM) + 当たり判定スクラッチ。
gobj_dead:	ds	MAX_GOBJ		; 破壊済みフラグ(1=破壊)
gc_row:		db	0
gc_col:		db	0
gc_i:		db	0
gc_ptr:		dw	0		; gobj_check が一致したエントリの先頭アドレス
cg_sav_dc:	dw	0		; crater_gram: 跨フレーム状態の退避
cg_sav_shl:	db	0
cg_sav_shr:	db	0
cg_page:	db	0
cg_k:		db	0
cg_rr:		db	0
gc_cbase:	dw	0		; crater_write_obj: crater_base 一時保持
gc_size:	db	1		; crater_write_obj: 1=2x2, 2=4x4
cwo_r:		db	0		; crater_write_4x4 行カウンタ
cwo_c:		db	0		; crater_write_4x4 列カウンタ
cwo_row:	db	0		; crater_write_4x4 現 row 一時
pc_active:	db	0		; [#2] 保留クレーター有効
pc_prog:	db	0		; 進捗 0..5 (2ページ×3列)
pc_row:		db	0		; 保留クレーターの row(R)
pc_ex:		db	0		; 保留クレーターの画面列(col_ex)
pc_dpage:	db	0		; ヒット時の表示中ページ(先に描く)
pc_coarse:	dw	0		; ヒット時の coarse(16bit, 分散描画中 GRAM 位置を固定)
hit_tcol:	db	0		; 直近ブラスター着弾の tilemap 列(Sol 判定用)
sol_state:	db	0		; [#3] Sol: 0=隠, 1..4=f0..f3 描画待ち, 5=露出完了
sol_timer:	db	0		; Sol アニメのコマ内カウンタ
sol_col:	db	0		; 発動中 Sol の col(エリアごと)
sol_row:	db	0		; 発動中 Sol の row
sol_base_ptr:	dw	0		; 発動中 Sol の 4フレーム base 配列へのポインタ
	IFDEF	ALLAREAS
sol_n:		db	0		; [⑦] エリアの Sol 数(gobj_load が設定)
sol_list:	ds	11 * 8		; 1件=col(2),row(1),4フレームbase(8B)。最大8体
	ENDIF
; 地上物リスト: 1件 = dw tilemap_col, db row, db type, dw crater_id (6B)。
;   実データは -DGOBJ_EXTDATA で roms/ から(gobj_n/gobj_list を定義)。既定は空。
	IFDEF	GOBJ_EXTDATA
gobj_list:				; データ先頭(roms が count を equ で定義)
	INCLUDE	"roms/area1_gobj.inc"
gobj_n:		db	gobj_area1_count
	ELSE
	IFDEF	ALLAREAS
gobj_n:		db	0		; [⑦(4)] RAM 可変リスト(gobj_load が切替毎に再構築, 最大11件)
gobj_list:	ds	6 * 32
	ELSE
gobj_n:		db	0		; 骨組み: 空リスト(当たり無し)
gobj_list:
	ENDIF
	ENDIF

ene_code:	db	0
ene_tl:		dw	0
ene_bc:		db	0
spawn_tmr:	db	0
spawn_row:	db	0
ene_active:	ds	MAX_ENEMIES
ene_ex:		ds	MAX_ENEMIES		; セル列 (8px) = ene_wx>>8 (描画/衝突用)
ene_ey:		ds	MAX_ENEMIES		; セル行   = ene_wy>>8
ene_wx:		ds	MAX_ENEMIES * 2		; [32方向] 16bit固定小数位置 cell<<8|frac(frac=1/32px)
ene_wy:		ds	MAX_ENEMIES * 2
ene_vx:		ds	MAX_ENEMIES		; [32方向] 速度(符号付, 1/32px/frame)
ene_vy:		ds	MAX_ENEMIES
ene_frm:	ds	MAX_ENEMIES		; アニメフレーム 0..3
ene_type:	ds	MAX_ENEMIES		; [M8] 敵タイプ index (0..EN_NTYPES-1)
ene_dir:	ds	MAX_ENEMIES		; [M8④] Jara 振動方向(0=下/1=上)
ene_phase:	ds	MAX_ENEMIES		; [M8④] Toroid フェーズ(0=approach/1=pause/2=retreat)
ene_ptmr:	ds	MAX_ENEMIES		; [M8④] Toroid pause タイマー
ene_ftmr:	ds	MAX_ENEMIES		; [敵弾] 射撃タイマ(0 で発射)
; [敵弾] スパリオ
ebul_active:	ds	MAX_EBUL
ebul_x:		ds	MAX_EBUL * 2		; cell<<8|frac
ebul_y:		ds	MAX_EBUL * 2
ebul_vx:	ds	MAX_EBUL * 2		; 符号付 frac/frame
ebul_vy:	ds	MAX_EBUL * 2
ebul_prev1:	ds	MAX_EBUL * 2		; 窓1 前回セル(0xFFFF=無効)
ebul_prev2:	ds	MAX_EBUL * 2		; 窓2 前回セル
ebul_svx:	dw	0			; spawn 速度(呼出側が設定)
ebul_svy:	dw	0
eb_sx:		dw	0
eb_sy:		dw	0
eb_i:		db	0
eb_soff:	dw	0
eb_newc1:	dw	0		; idle-skip 用 窓1新セル
ebul_cnt:	db	0		; アクティブ弾数(early-out 用)
ebul_xtmp:	dw	0
ebul_ytmp:	dw	0
eb_test_tmr:	db	1		; EBUL_SELFTEST スポーンタイマー
ef_ex:		db	0		; ene_fire 作業
ef_ey:		db	0
ef_dx:		db	0
ef_dy:		db	0
ef_adx:		db	0
ef_ady:		db	0
ef_vmx:		db	0
ef_vmy:		db	0
lives:		db	3		; [敵弾] 残機(0 でゲームオーバー)
ship_inv:	db	0		; 復活無敵フレーム
game_over:	db	0		; 1=ゲームオーバー
; [敵] Bacura
bac_active:	ds	MAX_BACURA
bac_sx:		ds	MAX_BACURA	; 画面列(0xFF.. = 左に抜けた)
bac_row:	ds	MAX_BACURA
bac_prev1:	ds	MAX_BACURA * 2	; 窓1 前回セル
bac_prev2:	ds	MAX_BACURA * 2
bac_spawn_tmr:	db	0
bac_i:		db	0
bac_soff:	dw	0
bac_cursx:	db	0
bac_ftptr:	dw	0		; 現回転コマの frametab 行ポインタ(bacura_update 冒頭で算出)
bp_tl:		dw	0		; bac_put2x4/ene_clr2x4 作業: TL
bp_ptr:		dw	0		; 〃 frametab コードポインタ
bp_offp:	dw	0		; 〃 bac_offs ポインタ
; [敵32方向] get_index_for_angle 作業
gia_dx:		db	0		; 符号付セル差(ship-self)
gia_dy:		db	0
gia_ax:		db	0
gia_big:	db	0
gia_small:	db	0
gia_steep:	db	0
spawn_type:	db	0		; (旧)未使用
spawn_idx:	db	0		; [M8④] spawn_seq のインデックス
num_flying:	db	3		; [M8④] 現在の同時出現上限(3→4→5)
difficulty:	db	0		; [M8④] 難易度(0..2)。経過で上昇、型解禁を制御
diff_tmr:	dw	DIFF_INTERVAL	; [M8④] 次の難易度上昇までのフレーム
eff_cap:	db	3		; [M8⑥(A)] 今回の実効同時上限(num_flying - 表示中Grobda)
	IFDEF	GROBDA_EXTDATA
grob_active:	ds	MAX_GROBDA	; [M8⑥] Grobda 生存
grob_wcol:	ds	MAX_GROBDA	; world列(8bit, スクロール追従)
grob_vy:	ds	MAX_GROBDA	; 行(道位置 6..18)
grob_frm:	ds	MAX_GROBDA	; アニメ(0/1)
grob_dir:	ds	MAX_GROBDA	; 道振動方向
grob_mtmr:	ds	MAX_GROBDA	; 移動タイマー
grob_atmr:	ds	MAX_GROBDA	; アニメタイマー
grob_ex:	ds	MAX_GROBDA	; 今フレームの screen列(>=COLS=off)
grob_dirty:	ds	MAX_GROBDA	; [M8⑥] 今フレーム移動/アニメしたか(idle-skip用)
grob_prev:	ds	MAX_GROBDA * 2	; 前回 top-left 窓1
grob_prev2:	ds	MAX_GROBDA * 2	; 窓2
grob_retx:	db	0		; [M8⑥] 照準の画面セル列(flee判定)
grob_rety:	db	0		; 照準の画面セル行
grob_hi:	db	0		; grobda_hit_check 専用ループ(spr_i を壊さない)
	ENDIF
; [M8④] 出現タイプ列(エリア1≒Toroid主体, たまに Jara(1)/Torkan(2))。12個循環。
SPAWN_SEQ_LEN	EQU	12
spawn_seq:	db	0,0,0,3, 1,0,0,0, 2,0,3,1	; [敵] 3=Zoshi(difficulty>=3 で解禁)
; [M8④] 敵タイプ記述子: per type = (pcg_base, nframes, traj_id)。
;   traj_id: 0=Toroid(直進) / 1=Jara(縦振動) / 2=Torkan(斜めダイブ)。
entype_tab:
	db	ENEMY_BASE,  4, 0	; type0: Toroid(円盤, 直進)
	db	JARA_BASE,   4, 1	; type1: Jara(縦振動+左進)
	db	TORKAN_BASE, 2, 2	; type2: Torkan(斜めダイブ, 高速)
	db	ZOSHI_BASE,  2, 1	; type3: Zoshi(2コマ, 縦振動+左進=Jara traj 流用, 撃つ)
ene_atmr:	ds	MAX_ENEMIES
ene_mtmr:	ds	MAX_ENEMIES
ene_prev:	ds	MAX_ENEMIES * 2		; 前回 top-left セル 窓1
ene_prev2:	ds	MAX_ENEMIES * 2		; 窓2
ene_dirty:	ds	MAX_ENEMIES		; 今フレーム移動/アニメ/消滅したか(idle-skip用)
prev_cur_pos:	dw	0xFFFF			; 前フレームの cur_pos
scroll_moved:	db	1			; cur_pos が変わった(=全描き直し)か

spr_j:		db	0		; collision 内側ループ index
spr_k:		db	0		; expl_spawn の index
col_bcx:	db	0		; 衝突: 弾セル列
col_bcy:	db	0		; 弾セル行
col_ex:		db	0		; 衝突: 敵/爆発 列
col_ey:		db	0		; 行
col_scx:	db	0		; 自機セル列
col_scy:	db	0		; 自機セル行
exp_active:	ds	MAX_EXPL
exp_ex:		ds	MAX_EXPL
exp_ey:		ds	MAX_EXPL
exp_frm:	ds	MAX_EXPL	; コマ 0..2
exp_tmr:	ds	MAX_EXPL
exp_prev:	ds	MAX_EXPL * 2
exp_prev2:	ds	MAX_EXPL * 2
exp_dirty:	ds	MAX_EXPL	; [M8] アニメ変化/消滅フラグ(idle-skip用)
ret_prev:	dw	0xFFFF		; 照準の前回 top-left (窓1)
ret_prev2:	dw	0xFFFF		; 窓2
ret_lhx:	db	0xFF		; idle-skip: 前回描画時の ship_hx
ret_lvy:	db	0xFF		; 前回描画時の ship_vy
ret_skip:	db	0		; このフレーム照準描画をスキップしたか
ret_code:	db	RETICLE_BASE	; [M8-3] 今フレーム描画する照準 PCG(シアン/赤)
ret_lcode:	db	RETICLE_BASE	; 前回描画した PCG コード(idle-skip 判定用)
ga_col:		db	0		; gobj_at 作業
ga_row:		db	0
ga_i:		db	0
rr_src:		dw	0		; gen_reticle 作業(現セルのソース)
rr_mode:	db	0		; 0=シアン(B+G) 1=赤(R)
rr_zero:	db	0,0,0,0,0,0,0,0	; 使わないプレーンに書く 0 データ

;=====================================================================
; 敵ベースデータ (4フレーム × 96B = 384B)。差し替えフック:
;   -DENEMY_EXTDATA 指定時は roms/enemy_ext.inc を使う(外部提供, gitignore)。
;   未指定(既定)は下のオリジナル円盤(灰色の八角形↔菱形 + 赤コア)を使う。
;   色(公式Dスロット): ボディ=R のみ→tc2→slot2(明灰), 中心=B+G→tc5→slot5(赤)。
;   per-pixel tc=スロット, ATR=0x27。形はオリジナル(著名キャラは不使用)。
; [敵弾] スパリオ PCG(16x16=4セル=96B)。xevi_enemy_sprites.py 生成(非コミット)。
ebullet_data:
	INCBIN	"roms/arcade/xevious-out/enemy_ebullet.bin"

; [敵] Zoshi PCG(2コマ×16x16=8セル=192B)。xevi_enemy_sprites.py 生成(非コミット)。
zoshi_data:
	INCBIN	"roms/arcade/xevious-out/enemy_zoshi_0.bin"
	INCBIN	"roms/arcade/xevious-out/enemy_zoshi_1.bin"
; [敵] Bacura 回転8コマ(16x32=2幅×4高)の重複除去済み43セル(各24B)。rot180適用。
;   tools/gen_bac_res.py が bacura_rot0..7.bin から生成(非コミット)。
bacura_cells:
	INCBIN	"roms/arcade/xevious-out/bacura_cells.bin"
; bacura_frametab: 8コマ×8セル(draw順)→実PCGコード。
	INCLUDE	"roms/arcade/xevious-out/bacura_frametab.inc"
; [敵32方向] 角度→速度表(ang_aimed/fast/slow/sheo 各32方向 dY,dX 1/32px) + octant_angle_tbl。
;   tools/xevi_enemy_ai.py が ROM から生成(非コミット)。get_index_for_angle が octant_angle_tbl を使用。
	INCLUDE	"roms/arcade/xevious-out/enemy_ai.inc"

enemy_data:
	IFDEF	ENEMY_EXTDATA
	INCLUDE	"roms/enemy_ext.inc"	; 外部提供 384B db (gitignore)
	ELSE
; --- frame0: 八角形 + 赤コア ---
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01	; TL B
	db	0x07,0x1F,0x3F,0x7F,0xFF,0xFF,0xFF,0xFE	; TL R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01	; TL G
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80	; TR B
	db	0xE0,0xF8,0xFC,0xFE,0xFF,0xFF,0xFF,0x7F	; TR R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80	; TR G
	db	0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL B
	db	0xFE,0xFF,0xFF,0xFF,0x7F,0x3F,0x1F,0x07	; BL R
	db	0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL G
	db	0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BR B
	db	0x7F,0xFF,0xFF,0xFF,0xFE,0xFC,0xF8,0xE0	; BR R
	db	0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BR G
; --- frame1: 菱形 + 赤コア ---
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01	; TL B
	db	0x01,0x03,0x07,0x0F,0x1F,0x3F,0x7F,0xFE	; TL R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01	; TL G
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80	; TR B
	db	0x80,0xC0,0xE0,0xF0,0xF8,0xFC,0xFE,0x7F	; TR R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80	; TR G
	db	0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL B
	db	0xFE,0x7F,0x3F,0x1F,0x0F,0x07,0x03,0x01	; BL R
	db	0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL G
	db	0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BR B
	db	0x7F,0xFE,0xFC,0xF8,0xF0,0xE0,0xC0,0x80	; BR R
	db	0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BR G
; --- frame2 = frame0 ---
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01
	db	0x07,0x1F,0x3F,0x7F,0xFF,0xFF,0xFF,0xFE
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80
	db	0xE0,0xF8,0xFC,0xFE,0xFF,0xFF,0xFF,0x7F
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80
	db	0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00
	db	0xFE,0xFF,0xFF,0xFF,0x7F,0x3F,0x1F,0x07
	db	0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00
	db	0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00
	db	0x7F,0xFF,0xFF,0xFF,0xFE,0xFC,0xF8,0xE0
	db	0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00
; --- frame3 = frame1 ---
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01
	db	0x01,0x03,0x07,0x0F,0x1F,0x3F,0x7F,0xFE
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80
	db	0x80,0xC0,0xE0,0xF0,0xF8,0xFC,0xFE,0x7F
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80
	db	0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00
	db	0xFE,0x7F,0x3F,0x1F,0x0F,0x07,0x03,0x01
	db	0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00
	db	0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00
	db	0x7F,0xFE,0xFC,0xF8,0xF0,0xE0,0xC0,0x80
	db	0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00
	ENDIF

;=====================================================================
; [M8④] Jara(4コマ=16セル=384B)。-DJARA_EXTDATA で roms/jara_ext.inc。
;   未指定は Toroid を流用(汎用円盤)。
	IFDEF	JARA_EXTDATA
jara_data:
	INCLUDE	"roms/jara_ext.inc"
	ELSE
jara_data	EQU	enemy_data
	ENDIF
; [M8④] Torkan(2コマ=8セル=192B)。-DTORKAN_EXTDATA で roms/torkan_ext.inc。
;   未指定は Toroid 先頭2コマを流用。
	IFDEF	TORKAN_EXTDATA
torkan_data:
	INCLUDE	"roms/torkan_ext.inc"
	ELSE
torkan_data	EQU	enemy_data
	ENDIF
; [M8⑥] Grobda PCG データ(2コマ=8セル=192B)。-DGROBDA_EXTDATA で有効。
	IFDEF	GROBDA_EXTDATA
grobda_data:
	INCLUDE	"roms/grobda_ext.inc"
	ENDIF

;=====================================================================
; 爆発ベースデータ (3フレーム × 96B = 288B)。オリジナルの膨張バースト
;   (4x4 → 8x8 → 12x12 の橙色の破裂)。色=橙(slot7=tc7=B+R+G 全プレーン)。
;   各セル B=R=G=同形状。著名キャラは不使用。
expl_data:
; --- frame0: 4x4 中心 ---
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03	; TL B
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03	; TL R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03	; TL G
	db	0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0xC0	; TR B
	db	0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0xC0	; TR R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0xC0	; TR G
	db	0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00	; BL B
	db	0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00	; BL R
	db	0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00	; BL G
	db	0xC0,0xC0,0x00,0x00,0x00,0x00,0x00,0x00	; BR B
	db	0xC0,0xC0,0x00,0x00,0x00,0x00,0x00,0x00	; BR R
	db	0xC0,0xC0,0x00,0x00,0x00,0x00,0x00,0x00	; BR G
; --- frame1: 8x8 ---
	db	0x00,0x00,0x00,0x00,0x0F,0x0F,0x0F,0x0F	; TL B
	db	0x00,0x00,0x00,0x00,0x0F,0x0F,0x0F,0x0F	; TL R
	db	0x00,0x00,0x00,0x00,0x0F,0x0F,0x0F,0x0F	; TL G
	db	0x00,0x00,0x00,0x00,0xF0,0xF0,0xF0,0xF0	; TR B
	db	0x00,0x00,0x00,0x00,0xF0,0xF0,0xF0,0xF0	; TR R
	db	0x00,0x00,0x00,0x00,0xF0,0xF0,0xF0,0xF0	; TR G
	db	0x0F,0x0F,0x0F,0x0F,0x00,0x00,0x00,0x00	; BL B
	db	0x0F,0x0F,0x0F,0x0F,0x00,0x00,0x00,0x00	; BL R
	db	0x0F,0x0F,0x0F,0x0F,0x00,0x00,0x00,0x00	; BL G
	db	0xF0,0xF0,0xF0,0xF0,0x00,0x00,0x00,0x00	; BR B
	db	0xF0,0xF0,0xF0,0xF0,0x00,0x00,0x00,0x00	; BR R
	db	0xF0,0xF0,0xF0,0xF0,0x00,0x00,0x00,0x00	; BR G
; --- frame2: 12x12 ---
	db	0x00,0x00,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F	; TL B
	db	0x00,0x00,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F	; TL R
	db	0x00,0x00,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F	; TL G
	db	0x00,0x00,0xFC,0xFC,0xFC,0xFC,0xFC,0xFC	; TR B
	db	0x00,0x00,0xFC,0xFC,0xFC,0xFC,0xFC,0xFC	; TR R
	db	0x00,0x00,0xFC,0xFC,0xFC,0xFC,0xFC,0xFC	; TR G
	db	0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x00,0x00	; BL B
	db	0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x00,0x00	; BL R
	db	0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x00,0x00	; BL G
	db	0xFC,0xFC,0xFC,0xFC,0xFC,0xFC,0x00,0x00	; BR B
	db	0xFC,0xFC,0xFC,0xFC,0xFC,0xFC,0x00,0x00	; BR R
	db	0xFC,0xFC,0xFC,0xFC,0xFC,0xFC,0x00,0x00	; BR G

;=====================================================================
; [M8] 照準 PCG データ (RETICLE_BASE から TL,TR,BL,BR の4セル)。
;   オリジナルの角ブラケット型クロスヘア。orange(tc7)=B+R+G 全プレーン同パターン。
;   通常色=シアン(tc6=R+G, B=0 → パレット slot6=0x27)。ロック時の赤点滅は M8-3。
;   -DRETICLE_EXTDATA で xevi の実照準スプライト(roms/reticle_ext.inc)に差替可。
	IFDEF	RETICLE_EXTDATA
reticle_data:
	INCLUDE	"roms/reticle_ext.inc"
	ELSE
reticle_data:
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TL B
	db	0xE0,0x80,0x80,0x00,0x00,0x00,0x00,0x00	; TL R
	db	0xE0,0x80,0x80,0x00,0x00,0x00,0x00,0x00	; TL G
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TR B
	db	0x07,0x01,0x01,0x00,0x00,0x00,0x00,0x00	; TR R
	db	0x07,0x01,0x01,0x00,0x00,0x00,0x00,0x00	; TR G
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL B
	db	0x00,0x00,0x00,0x00,0x00,0x80,0x80,0xE0	; BL R
	db	0x00,0x00,0x00,0x00,0x00,0x80,0x80,0xE0	; BL G
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BR B
	db	0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x07	; BR R
	db	0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x07	; BR G
	ENDIF

;=====================================================================
; [M8] ブラスター弾 PCG データ (BLASTER_BASE から TL,TR,BL,BR)。
;   中央寄せの 8x8 充填正方形(オリジナル)。白(tc1)=B プレーンのみ(slot1)。
;   -DBLASTER_EXTDATA で xevi の実弾スプライト(roms/blaster_ext.inc)に差替可。
	IFDEF	BLASTER_EXTDATA
blaster_data:
	INCLUDE	"roms/blaster_ext.inc"
	ELSE
blaster_data:
	; size0: 大 8x8 (中央寄せ)
	db	0x00,0x00,0x00,0x00,0x0F,0x0F,0x0F,0x0F	; TL B
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TL R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TL G
	db	0x00,0x00,0x00,0x00,0xF0,0xF0,0xF0,0xF0	; TR B
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TR R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TR G
	db	0x0F,0x0F,0x0F,0x0F,0x00,0x00,0x00,0x00	; BL B
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL G
	db	0xF0,0xF0,0xF0,0xF0,0x00,0x00,0x00,0x00	; BR B
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BR R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BR G
	; size1: 中 6x6
	db	0x00,0x00,0x00,0x00,0x00,0x07,0x07,0x07	; TL B
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TL R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TL G
	db	0x00,0x00,0x00,0x00,0x00,0xE0,0xE0,0xE0	; TR B
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TR R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TR G
	db	0x07,0x07,0x07,0x00,0x00,0x00,0x00,0x00	; BL B
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL G
	db	0xE0,0xE0,0xE0,0x00,0x00,0x00,0x00,0x00	; BR B
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BR R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BR G
	; size2: 小 4x4
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03	; TL B
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TL R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TL G
	db	0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0xC0	; TR B
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TR R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TR G
	db	0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00	; BL B
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL G
	db	0xC0,0xC0,0x00,0x00,0x00,0x00,0x00,0x00	; BR B
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BR R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BR G
	ENDIF
