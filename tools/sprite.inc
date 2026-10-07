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
MAX_BULLETS	EQU	6		; 同時存在数 (ザッパー2発 x 3組)。PCG は共有16版で不変
TRIG1		EQU	0x20		; PSG port A bit5 (トリガ1, 負論理)
ENEMY_BASE	EQU	0xB0		; 敵 PCG 先頭 (0xB0..0xBF, 4フレーム x 2x2)
MAX_ENEMIES	EQU	4		; 同時出現数
ENEMY_SPEED	EQU	1		; 横移動(セル/移動tick) ※敵は8pxグリッド
EN_NTYPES	EQU	2		; [M8] 敵タイプ数 (足場: type0=直進/type1=斜め)
EXPL_BASE	EQU	0xC0		; 爆発 PCG 先頭 (0xC0..0xCB, 3フレーム x 2x2)
MAX_EXPL	EQU	2		; 同時爆発数 (ピーク負荷削減)
RETICLE_BASE	EQU	0xCC		; [M8] 照準 PCG (0xCC..0xCF, 2x2 の角ブラケット)
RETICLE_DIST	EQU	10 * 4		; 自機前方の表示距離 (hx 2px単位, 10セル先)
BLASTER_BASE	EQU	0xD0		; [M8] ブラスター弾 PCG (0xD0..0xD3, 2x2)
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
	call	gen_reticle
	call	gen_blaster
	ld	hl, 0xFFFF		; 照準 prev 無効化
	ld	(ret_prev), hl
	ld	(ret_prev2), hl
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
gen_reticle:
	ld	hl, reticle_data
	ld	a, RETICLE_BASE
	ld	(ene_code), a
	ld	b, 4
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
	add	a, 6			; 自機中央
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
	call	reticle_soff
	; 窓1
	ld	hl, (spr_soff)
	ld	de, (cur_pos)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(ret_prev), hl
	ld	a, RETICLE_BASE
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
	ld	a, RETICLE_BASE
	call	ene_put2x2
	ret

;=====================================================================
; [M8] ブラスター: トリガ2(bit6)で地上攻撃。自機前方へ飛び、照準位置(RETICLE_DIST
;   先)で着弾=爆発(地上物ヒットは M8-3 で接続)。2x2 のオリジナル小弾(青 tc6)。
gen_blaster:
	ld	hl, blaster_data
	ld	a, BLASTER_BASE
	ld	(ene_code), a
	ld	b, 4
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
	ld	a, (ship_hx)		; 着弾 x = 機首 + RETICLE_DIST
	add	a, 2 * 4
	add	a, RETICLE_DIST
	ld	hl, bla_tx
	add	hl, de
	ld	(hl), a
	ld	a, (ship_vy)
	add	a, 6			; 自機中央(照準と同じ高さ)
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
	ld	a, (col_ey)
	call	gobj_check
	jr	nc, .nobj		; 当たり無し
	ld	a, (gc_col)		; 当たり: EMM タイルを crater に上書き(de=crater)
	ld	c, a
	ld	a, (gc_row)
	call	crater_write_emm
	; TODO(次段): 表示中 GRAM の両ページ・両シフト版も即時上書き
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
	ld	a, BLASTER_BASE
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
	ld	a, BLASTER_BASE
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
	ld	a, BLASTER_BASE
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
;   画面セル列 X ↔ tilemap列: tilemap_col = coarse + X + GOBJ_OFFSET (xevi と共同確認中)。
GOBJ_OFFSET	EQU	40		; prefill の -COLS 由来(実測確認予定)
MAX_GOBJ	EQU	16		; エリアあたり地上物の最大(破壊フラグ用)

; crater_write_emm: EMM のタイルマップ(col,row)を crater_id で上書き(再展開で反映)。
;   入力: c=tilemap列(8bit, 骨組み), a=row(0-24), de=crater tile ID。
;   EMM addr = col*50 + row*2 (read_ids と同じ列*50 + 行*2)。
crater_write_emm:
	push	de			; crater id 退避
	; hl = col*50
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
	; + row*2
	ld	d, 0
	add	a, a			; row*2
	ld	e, a
	add	hl, de
	ld	a, l
	ld	(emm_a0), a
	ld	a, h
	ld	(emm_a1), a
	xor	a
	ld	(emm_a2), a
	call	set_emm_addr
	pop	de			; crater id
	ld	bc, EMM_DAT
	out	(c), e			; lo
	out	(c), d			; hi
	ret

; gobj_check: 着弾の tilemap(col8=c, row=a) が未破壊の地上物と一致するか。
;   一致→ carry セット + 戻り: ix or 変数に index、de=crater id。不一致→ carry クリア。
;   骨組み: gobj_n エントリを線形走査(1件 = dw col, db row, db type, dw crater = 6B)。
gobj_check:
	ld	(gc_row), a
	ld	a, c
	ld	(gc_col), a
	ld	a, (gobj_n)
	or	a
	ret	z			; 空リスト → 当たり無し(carry クリア)
	ld	b, a
	ld	hl, gobj_list
	xor	a
	ld	(gc_i), a
.l:	push	bc
	; col 一致? (下位8bitのみ, 骨組み)
	ld	a, (hl)			; col lo
	ld	c, a
	inc	hl
	inc	hl			; col hi をスキップ(骨組みは8bit)
	ld	a, (hl)			; row
	inc	hl
	ld	e, a			; row
	ld	a, (hl)			; type (未使用)
	inc	hl
	ld	a, (hl)			; crater lo
	ld	(gc_crlo), a
	inc	hl
	ld	a, (hl)			; crater hi
	ld	(gc_crhi), a
	inc	hl			; hl = 次エントリ
	; 破壊済み?
	push	hl
	ld	a, (gc_i)
	ld	l, a
	ld	h, 0
	ld	de, gobj_dead
	add	hl, de
	ld	a, (hl)
	pop	hl
	or	a
	jr	nz, .nx			; 破壊済みスキップ
	ld	a, (gc_col)
	cp	c
	jr	nz, .nx
	ld	a, (gc_row)
	cp	e
	jr	nz, .nx
	; 一致!
	pop	bc
	ld	a, (gc_i)		; 破壊フラグを立てる
	ld	l, a
	ld	h, 0
	ld	de, gobj_dead
	add	hl, de
	ld	(hl), 1
	ld	a, (gc_crlo)
	ld	e, a
	ld	a, (gc_crhi)
	ld	d, a			; de = crater id
	scf
	ret
.nx:	ld	a, (gc_i)
	inc	a
	ld	(gc_i), a
	pop	bc
	djnz	.l
	or	a			; carry クリア
	ret

;=====================================================================
; 毎フレーム(vblank 中, ship_update の後)。発射/移動/消去/描画。
;   順序: 発射判定 → 全弾消去 → 移動/寿命 → 全弾描画 (重なり対策で分離)。
sprite_update:
	call	fire_check		; トリガ1でザッパー発射
	call	fire_blaster		; トリガ2でブラスター発射
	call	enemy_spawn		; 一定間隔で右端から出現
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
	call	enemies_move
	call	expls_anim
	; 衝突判定(弾vs敵, 敵vs自機) → 当たった弾/敵を非アクティブ化 + 爆発発生
	call	collision_check
	; 全消去(前回位置)。敵は idle-skip, 非アクティブ化された敵も消去される。
	call	reticle_erase
	call	expls_erase
	call	enemies_erase
	call	bullets_erase
	call	blaster_erase
	; 全描画(アクティブのみ)。照準は最後=最前面。
	call	bullets_draw
	call	blaster_draw
	call	enemies_draw
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
	call	expl_spawn		; (col_ex,col_ey)
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
	call	clr_cell		; 窓1 消去
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
	call	clr_cell		; 窓2 消去
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
	call	put_cell		; 窓1 描画
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
	call	put_cell		; 窓2 描画
.dn:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jp	.dl

;=====================================================================
; 敵 PCG 生成: enemy_data(4フレーム×96B = 16セル×24B, TL/TR/BL/BR順)を
;   ENEMY_BASE から 16 セル連番で定義。
gen_enemy:
	ld	hl, enemy_data
	ld	a, ENEMY_BASE
	ld	(ene_code), a
	ld	b, 16
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

; 一定間隔で右端から敵出現(空きスロットがあれば)。ey は数行を循環。
enemy_spawn:
	IFDEF	NO_ENEMY
	ret				; 検証用: 敵を出さない(弾/爆発のゴースト切り分け)
	ENDIF
	ld	a, (spawn_tmr)
	or	a
	jr	z, .go
	dec	a
	ld	(spawn_tmr), a
	ret
.go:
	ld	a, ENEMY_SPAWN
	ld	(spawn_tmr), a
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
	; [M8] タイプを割当て spawn_type を循環 (足場)
	ld	a, (spawn_type)
	ld	hl, ene_type
	add	hl, de
	ld	(hl), a
	inc	a
	cp	EN_NTYPES
	jr	c, .tok
	xor	a
.tok:	ld	(spawn_type), a
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
	jr	z, .mn
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
	and	3
	ld	(hl), a
	ld	hl, ene_dirty		; アニメ変化 → dirty
	add	hl, de
	ld	(hl), 1
.amv:
	; 移動
	ld	hl, ene_mtmr
	add	hl, de
	dec	(hl)
	jr	nz, .mn
	ld	(hl), ENEMY_MOVE
	ld	hl, ene_dirty		; 移動 → dirty
	add	hl, de
	ld	(hl), 1
	ld	hl, ene_ex
	add	hl, de
	ld	a, (hl)
	sub	ENEMY_SPEED
	ld	(hl), a
	; [M8] 軌道ディスパッチ: type の traj_id で縦移動 (足場。0=直進/1=斜め)
	push	af			; ene_ex(新) を off-screen 判定用に保存
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, ene_type
	add	hl, de
	ld	a, (hl)			; type
	ld	c, a
	add	a, a
	add	a, c			; type*3
	inc	a
	inc	a			; +2 = traj_id オフセット
	ld	e, a
	ld	d, 0
	ld	hl, entype_tab
	add	hl, de
	ld	a, (hl)			; traj_id
	cp	1
	jr	nz, .trajdone		; 0=直進は縦移動なし
	ld	a, (spr_i)		; traj1=斜め: ene_ey を 1 増やす(clamp 22)
	ld	e, a
	ld	d, 0
	ld	hl, ene_ey
	add	hl, de
	ld	a, (hl)
	cp	22
	jr	nc, .trajdone
	inc	a
	ld	(hl), a
.trajdone:
	pop	af			; ene_ex 復帰
	ld	e, a			; ene_ex 退避
	ld	a, (spr_i)		; de = i に復元 (traj 分岐で de が壊れているため)
	ld	c, a
	ld	a, e
	ld	e, c
	ld	d, 0
	cp	COLS			; 0 未満で 0xFF 等 >= COLS → 消滅
	jr	c, .mn
	ld	hl, ene_active
	add	hl, de
	ld	(hl), 0
.mn:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jp	.ml

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
	ld	a, (ene_bc)
	call	put_cell		; TL
	ld	hl, (ene_tl)
	inc	hl
	ld	a, (ene_bc)
	inc	a
	call	put_cell		; TR
	ld	hl, (ene_tl)
	ld	de, COLS
	add	hl, de
	ld	a, (ene_bc)
	add	a, 2
	call	put_cell		; BL
	ld	hl, (ene_tl)
	ld	de, COLS + 1
	add	hl, de
	ld	a, (ene_bc)
	add	a, 3
	call	put_cell		; BR
	ret

; hl=top-left セル。2x2 を消去。
ene_clr2x2:
	ld	(ene_tl), hl
	call	clr_cell
	ld	hl, (ene_tl)
	inc	hl
	call	clr_cell
	ld	hl, (ene_tl)
	ld	de, COLS
	add	hl, de
	call	clr_cell
	ld	hl, (ene_tl)
	ld	de, COLS + 1
	add	hl, de
	call	clr_cell
	ret

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
gc_crlo:	db	0
gc_crhi:	db	0
; 地上物リスト: 1件 = dw tilemap_col, db row, db type, dw crater_id (6B)。
;   実データは -DGOBJ_EXTDATA で roms/ から(gobj_n/gobj_list を定義)。既定は空。
	IFDEF	GOBJ_EXTDATA
	INCLUDE	"roms/area1_gobj.inc"
	ELSE
gobj_n:		db	0		; 骨組み: 空リスト(当たり無し)
gobj_list:
	ENDIF

ene_code:	db	0
ene_tl:		dw	0
ene_bc:		db	0
spawn_tmr:	db	0
spawn_row:	db	0
ene_active:	ds	MAX_ENEMIES
ene_ex:		ds	MAX_ENEMIES		; セル列 (8px)
ene_ey:		ds	MAX_ENEMIES		; セル行
ene_frm:	ds	MAX_ENEMIES		; アニメフレーム 0..3
ene_type:	ds	MAX_ENEMIES		; [M8] 敵タイプ index (0..EN_NTYPES-1)
spawn_type:	db	0		; 次に出す敵タイプ(循環)
; [M8] 敵タイプ記述子: per type = (pcg_base, nframes, traj_id)。
;   traj_id: 0=直進(左)/1=斜め(左下に漂う)。PCG は distinct データ到着まで共用。
entype_tab:
	db	ENEMY_BASE, 4, 0	; type0: 円盤・直進
	db	ENEMY_BASE, 4, 1	; type1: 円盤・斜め(オリジナル軌道。PCGは後で差替)
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

;=====================================================================
; 敵ベースデータ (4フレーム × 96B = 384B)。差し替えフック:
;   -DENEMY_EXTDATA 指定時は roms/enemy_ext.inc を使う(外部提供, gitignore)。
;   未指定(既定)は下のオリジナル円盤(灰色の八角形↔菱形 + 赤コア)を使う。
;   色(公式Dスロット): ボディ=R のみ→tc2→slot2(明灰), 中心=B+G→tc5→slot5(赤)。
;   per-pixel tc=スロット, ATR=0x27。形はオリジナル(著名キャラは不使用)。
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

;=====================================================================
; [M8] ブラスター弾 PCG データ (BLASTER_BASE から TL,TR,BL,BR)。
;   中央寄せの 8x8 充填正方形(オリジナル)。白(tc1)=B プレーンのみ(slot1)。
blaster_data:
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
