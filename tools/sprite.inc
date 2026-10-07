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
BULLET_SPEED	EQU	4		; 1フレームの前進 (2px単位; 4=8px/frame)
FIRE_DELAY	EQU	6		; 連射間隔(フレーム)
TRIG1		EQU	0x20		; PSG port A bit5 (トリガ1, 負論理)

;=====================================================================
; 起動時: 弾 PCG(16版)生成 + テーブル初期化。PCGモード有効時に呼ぶ。
sprite_init:
	call	gen_bullet
	; 弾テーブルを全て非アクティブ(prev=0xFFFF)に
	ld	hl, bul_active
	ld	b, MAX_BULLETS
.ci:	ld	(hl), 0
	inc	hl
	djnz	.ci
	xor	a
	ld	(fire_tmr), a
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
	call	bullets_erase		; 全弾を前回位置から消去(両窓)
	call	bullets_move		; 前進 + 画面外で消滅
	call	bullets_draw		; 新位置へ描画(両窓), prev 記録
	ret

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
	ld	(hl), 0xFF		; prev 無効(下位)
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
	ld	e, a
	ld	d, 0
	ld	hl, bul_active
	add	hl, de
	ld	a, (hl)
	or	a
	jr	z, .en			; 非アクティブ
	; prev 窓1 (bul_prev + 2*i)
	ld	a, (spr_i)
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
	jr	z, .en			; prev 無効
	call	clr_cell		; 窓1 消去 (de は保持される)
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
	call	clr_cell
.en:	ld	a, (spr_i)
	inc	a
	ld	(spr_i), a
	jr	.el

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
