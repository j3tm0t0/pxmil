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
MAX_BULLETS	EQU	4		; 同時発射数
TRIG1		EQU	0x20		; PSG port A bit5 (トリガ1, 負論理)
ENEMY_BASE	EQU	0xB0		; 敵 PCG 先頭 (0xB0..0xBF, 4フレーム x 2x2)
MAX_ENEMIES	EQU	4		; 同時出現数
ENEMY_SPEED	EQU	1		; 横移動(セル/移動tick) ※敵は8pxグリッド
EXPL_BASE	EQU	0xC0		; 爆発 PCG 先頭 (0xC0..0xCB, 3フレーム x 2x2)
MAX_EXPL	EQU	4		; 同時爆発数

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
; 毎フレーム(vblank 中, ship_update の後)。発射/移動/消去/描画。
;   順序: 発射判定 → 全弾消去 → 移動/寿命 → 全弾描画 (重なり対策で分離)。
sprite_update:
	call	fire_check		; トリガで発射
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
	call	enemies_move
	call	expls_anim
	; 衝突判定(弾vs敵, 敵vs自機) → 当たった弾/敵を非アクティブ化 + 爆発発生
	call	collision_check
	; 全消去(前回位置)。敵は idle-skip, 非アクティブ化された敵も消去される。
	call	expls_erase
	call	enemies_erase
	call	bullets_erase
	; 全描画(アクティブのみ)
	call	bullets_draw
	call	enemies_draw
	call	expls_draw
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
	; 空きスロットを探す
	ld	hl, bul_active
	ld	b, MAX_BULLETS
	ld	c, 0			; index
.fs:	ld	a, (hl)
	or	a
	jr	z, .spawn
	inc	hl
	inc	c
	djnz	.fs
	ret				; 空き無し
.spawn:
	; bul_active[c]=1, hx=ship_hx(+前方へ少し), vy=ship_vy
	ld	(hl), 1
	ld	a, c
	ld	e, a
	ld	d, 0
	ld	a, (ship_hx)
	add	a, 2 * 4		; 機首(2セル右)あたりから
	ld	hl, bul_hx
	add	hl, de
	ld	(hl), a
	ld	a, (ship_vy)
	inc	a			; 中央寄り(自機3x3の中段)
	inc	a
	inc	a
	inc	a
	ld	hl, bul_vy
	add	hl, de
	ld	(hl), a
	ld	hl, bul_prev
	add	hl, de
	add	hl, de
	ld	(hl), 0xFF		; prev 無効(窓1)
	inc	hl
	ld	(hl), 0xFF
	ld	hl, bul_prev2		; 窓2 も無効化(スロット再利用の残留防止)
	add	hl, de
	add	hl, de
	ld	(hl), 0xFF
	inc	hl
	ld	(hl), 0xFF
	ld	a, FIRE_DELAY
	ld	(fire_tmr), a
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
	cp	COLS			; 0 未満で 0xFF 等 >= COLS → 消滅
	jr	c, .mn
	ld	hl, ene_active
	add	hl, de
	ld	(hl), 0
.mn:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jr	.ml

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
	; code base = ENEMY_BASE + frame*4
	ld	a, (spr_i)
	ld	e, a
	ld	d, 0
	ld	hl, ene_frm
	add	hl, de
	ld	a, (hl)
	add	a, a
	add	a, a			; frame*4
	add	a, ENEMY_BASE
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
	; active/非active 問わず prev 有効なら両窓消去 (非アクティブ消去スキップでゴースト)
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
bul_active:	ds	MAX_BULLETS
bul_hx:		ds	MAX_BULLETS		; 横 2px単位
bul_vy:		ds	MAX_BULLETS		; 縦 2ライン単位
bul_prev:	ds	MAX_BULLETS * 2		; 前回 top-left セル 窓1
bul_prev2:	ds	MAX_BULLETS * 2		; 窓2

ene_code:	db	0
ene_tl:		dw	0
ene_bc:		db	0
spawn_tmr:	db	0
spawn_row:	db	0
ene_active:	ds	MAX_ENEMIES
ene_ex:		ds	MAX_ENEMIES		; セル列 (8px)
ene_ey:		ds	MAX_ENEMIES		; セル行
ene_frm:	ds	MAX_ENEMIES		; アニメフレーム 0..3
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
