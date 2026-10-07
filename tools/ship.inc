; =====================================================================
; ship.inc — 自機(オリジナル機体)オーバーレイ 共通モジュール
;   emmscroll.asm(8色) / emmscroll64.asm(turboZ 64色) 両対応。
;   PCG で自機を地形の上に重ね、ジョイスティックで移動。横は 2px スムーズ
;   (起動時に base 16x16 から 0/2/4/6px の 4 シフト版を生成)。
;   IFDEF SHIP のガードを付けて INCLUDE すること。
;
; --- 呼び出し側が INCLUDE 前に EQU 定義しておく必須シンボル ---
;   (sjasmplus の IFNDEF は EQU ラベルに効かないため、ここでは既定値を
;    補わない。呼び出し側で必ず定義する)
;     COLS, ROWS           テキスト桁/行 (40/25)
;     OFF1024              2 窓目オフセット (1024)
;     PORT_SCRN            0x1FD0
;     SCRN_PCG             0x20 (0x1FD0 の PCGMODE ビット)
;     TVRAM, TATTR         テキスト VRAM/属性 I/O 上位 (0x3000/0x2000)
;     PCG_B, PCG_R, PCG_G  PCG プレーン定義ポート (0x1500/1600/1700)
;     PCG_DEFCELL          0x07FF
;     PORT_PSGREG/PSGDAT   0x1C00 / 0x1B00 (ジョイスティック)
;     SHIP_ATR             自機セル属性 (8色=0x20|色ビット / turboZ=0x20|パレット選択)
;     SHIPGEN              シフト版生成バッファ 72バイトの空き RAM
;                          (※ emmscroll64 は COLBUF(0xC600-) と衝突するので要変更)
;     SHIP_HX0             初期横位置 (2px単位。列18 位相0 = 18*4=72)
;     SHIP_ROW0            初期行 (12)
;     MOVE_DELAY           縦移動の間隔フレーム (3)。横は毎フレーム 2px
;   変数: cur_pos (dw) — 現フレームの表示開始セル。呼び出し側が毎フレーム設定。
;
; --- 公開ルーチン ---
;   ship_init    起動時 1 回。PCGMODE を立て、4 シフト版を生成し、位置初期化。
;   ship_update  毎フレーム (vblank 中)。ジョイスティック移動 + オーバーレイ。
;   scrn_out     a=0x1FD0 基本ビット。PCGMODE を必ず立てて書込 + シャドウ保存。
;
; --- xevi-extract への引き継ぎ: 64 色版に組み込む際の必須変更 ---
;   (1) PORT_SCRN への書込を全て `call scrn_out` 経由にする (scatter の
;       bank0/bank1 切替も含む。でないと散布中に自機が消える)。
;   (2) POS 確定後に `ld (cur_pos), hl` を追加。
;   (3) SHIP_ATR を定義し、turboZ はテキストパレット(0x1FB9+slot)で色を設定。
;   (4) 上記「必須シンボル」のうち 64 色版に無いもの(PCG_B/R/G, SCRN_PCG,
;       PORT_PSG*, SHIP_ATR, SHIPGEN, SHIP_HX0, SHIP_ROW0 等)を定義。
;       SHIPGEN は COLBUF と重ならない空き RAM に。
;   (5) PCGMODE 可能な状態で `call ship_init` を 1 回、毎フレーム vblank で
;       `call ship_update`。
; =====================================================================

SHIP_BASE	EQU	0x80		; 自機 PCG コード先頭 (0x80..0x97, 24セル)
SHIP_VSTRIDE	EQU	6		; 1 シフト版 = 3列x2行 = 6セル
SHIP_HXMAX	EQU	(COLS - 3) * 4 + 3	; ship_hx 上限 (col2 が行内に収まる)

;=====================================================================
; a=0x1FD0 基本ビット。PCGMODE(SCRN_PCG) を必ず立てて書込み、値をシャドウ保存。
;   自機表示にはどの PORT_SCRN 書込でも PCGMODE が要るため、呼び出し側は
;   PORT_SCRN 書込を全てこれ経由にする。
scrn_out:
	or	SCRN_PCG
	ld	(scrn_shadow), a
	ld	bc, PORT_SCRN
	out	(c), a
	ret

;=====================================================================
; 起動時 1 回。PCGMODE を立て、シフト版を生成し、位置を初期化。
ship_init:
	xor	a
	call	scrn_out		; bank0 + PCGMODE
	call	gen_ship
	ld	a, SHIP_HX0
	ld	(ship_hx), a
	ld	a, SHIP_ROW0
	ld	(ship_row), a
	ld	hl, 0xFFFF
	ld	(ship_prev), hl
	ld	(ship_prev2), hl
	ret

;=====================================================================
; 毎フレーム (vblank 中)。ジョイスティック移動 + オーバーレイ。
ship_update:
	call	move_ship
	call	overlay_ship
	ret

;=====================================================================
; [M6] 自機(オリジナル機体)の PCG を定義。base 16x16(shipdata) から
;   4 シフト版(0/2/4/6px) を 24bit 右シフトで生成し、各版 3列x2行を定義。
;   計 24 セル (0x80..0x97)。PCG モード有効時に呼ぶ。
gen_ship:
	xor	a
	ld	(gs_v), a		; v = 0..3
.vloop:
	xor	a
	ld	(gs_row), a		; row = 0,1
.rloop:
	call	gen_one_vr		; SHIPGEN に 3列ぶん生成
	call	def_vr_cells		; 3 セル定義
	ld	a, (gs_row)
	inc	a
	ld	(gs_row), a
	cp	2
	jr	nz, .rloop
	ld	a, (gs_v)
	inc	a
	ld	(gs_v), a
	cp	4
	jr	nz, .vloop
	ret

; (gs_v,gs_row) の 1行(16px)を位相 p=2v で右シフトし 3列(24px)を SHIPGEN へ。
;   SHIPGEN レイアウト: col0=[+0..23], col1=[+24..47], col2=[+48..71]
;   各 24 バイト = B8,R8,G8 (plane*8+raster)。
gen_one_vr:
	; src 左セル = shipdata + row*48, 右セル = +24
	ld	hl, shipdata
	ld	a, (gs_row)
	or	a
	jr	z, .r0
	ld	de, 48
	add	hl, de
.r0:	ld	(gs_lsrc), hl
	ld	de, 24
	add	hl, de
	ld	(gs_rsrc), hl
	ld	a, (gs_v)		; p = 2*v
	add	a, a
	ld	(gs_p), a
	xor	a
	ld	(gs_r), a		; idx 0..23 (= plane*8+raster)
.loop:
	ld	a, (gs_r)
	ld	e, a
	ld	d, 0
	ld	hl, (gs_lsrc)
	add	hl, de
	ld	b, (hl)			; b0 = 左バイト
	ld	hl, (gs_rsrc)
	add	hl, de
	ld	c, (hl)			; b1 = 右バイト
	ld	d, 0			; b2 = 0
	ld	a, (gs_p)
	or	a
	jr	z, .noshift
.sh:	srl	b			; [b0 b1 b2] 24bit 論理右シフト = 画面右へ
	rr	c
	rr	d
	dec	a
	jr	nz, .sh
.noshift:
	ld	a, c
	ld	(gs_c1), a
	ld	a, d
	ld	(gs_c2), a
	ld	hl, SHIPGEN
	ld	a, (gs_r)
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	(hl), b			; col0
	ld	de, 24
	add	hl, de
	ld	a, (gs_c1)
	ld	(hl), a			; col1
	add	hl, de
	ld	a, (gs_c2)
	ld	(hl), a			; col2
	ld	a, (gs_r)
	inc	a
	ld	(gs_r), a
	cp	24
	jr	nz, .loop
	ret

; SHIPGEN の 3列を PCG セルに定義。code = SHIP_BASE + v*6 + row*3 + col。
def_vr_cells:
	ld	a, (gs_v)
	add	a, a			; 2v
	ld	e, a
	add	a, a			; 4v
	add	a, e			; 6v
	ld	e, a
	ld	a, (gs_row)
	ld	d, a
	add	a, a			; 2row
	add	a, d			; 3row
	add	a, e			; 6v + 3row
	add	a, SHIP_BASE
	ld	(gs_code), a
	ld	hl, SHIPGEN
	ld	b, 3			; 3 列
.cl:
	push	bc
	ld	a, (gs_code)
	call	pcg_select
	ld	d, PCG_B >> 8
	call	def_plane		; B (hl += 8)
	ld	d, PCG_R >> 8
	call	def_plane		; R
	ld	d, PCG_G >> 8
	call	def_plane		; G (hl は次列 +24 へ)
	ld	a, (gs_code)
	inc	a
	ld	(gs_code), a
	pop	bc
	djnz	.cl
	ret

; a=code を PCG 定義セル(0x7FF)に選択 (ANK=code, ATR=PCG)
pcg_select:
	ld	e, a			; code
	ld	b, (TVRAM >> 8) | (PCG_DEFCELL >> 8)	; 0x37
	ld	c, PCG_DEFCELL & 0xFF			; 0xFF
	out	(c), e			; ANK[0x7FF]=code
	ld	b, (TATTR >> 8) | (PCG_DEFCELL >> 8)	; 0x27
	ld	a, 0x20
	out	(c), a			; ATR[0x7FF]=PCG
	ret

; d=プレーンポート上位(0x15/16/17), hl->8バイト。port=(d<<8)|(line<<1)
def_plane:
	ld	e, 0
.l:	ld	a, e
	add	a, a			; line<<1
	ld	c, a
	ld	b, d
	ld	a, (hl)
	out	(c), a
	inc	hl
	inc	e
	ld	a, e
	cp	8
	jr	nz, .l
	ret

;=====================================================================
; ジョイスティック読み取り → a (PSG port A, 負論理, bit=0 で押下)。
;   OUT 0x1C00,14 でレジスタ選択 → IN 0x1B00。
read_joy:
	ld	bc, PORT_PSGREG
	ld	a, 0x0E			; PSG reg 14 (port A = joystick)
	out	(c), a
	ld	bc, PORT_PSGDAT
	in	a, (c)
	ret

;=====================================================================
; 自機移動。横は ship_hx(2px単位) を毎フレーム ±1、縦は ship_row(8px) を
;   MOVE_DELAY フレームごと ±1。いずれも境界付き。
;   --- 方向マッピング (X1 モニタを 90度回した向き前提。bit 割当で調整可) ---
;     bit0=上, bit1=下, bit2=左, bit3=右 (X1 標準 PSG port A, 負論理)。
;     既定: 上→行-1, 下→行+1, 左→hx-1(左2px), 右→hx+1(右2px)。
;     90度回転で実機の見た目に合わせる場合はここの対応を入れ替える。
move_ship:
	call	read_joy
	ld	e, a			; e = joy (負論理)
	; --- 横: 毎フレーム 2px ---
	bit	2, e			; 左
	jr	nz, .nl
	ld	a, (ship_hx)
	or	a
	jr	z, .nl
	dec	a
	ld	(ship_hx), a
.nl:	bit	3, e			; 右
	jr	nz, .nr
	ld	a, (ship_hx)
	cp	SHIP_HXMAX
	jr	nc, .nr
	inc	a
	ld	(ship_hx), a
.nr:
	; --- 縦: MOVE_DELAY フレームごと 1 行(8px) ---
	ld	a, (move_dly)
	or	a
	jr	z, .vgo
	dec	a
	ld	(move_dly), a
	ret
.vgo:
	ld	d, 0			; d = 縦移動したか
	bit	0, e			; 上
	jr	nz, .nu
	ld	a, (ship_row)
	or	a
	jr	z, .nu
	dec	a
	ld	(ship_row), a
	ld	d, 1
.nu:	bit	1, e			; 下
	jr	nz, .nd
	ld	a, (ship_row)
	cp	ROWS - 2		; 行は 0..23 (2行ぶん)
	jr	nc, .nd
	inc	a
	ld	(ship_row), a
	ld	d, 1
.nd:
	ld	a, d
	or	a
	ret	z
	ld	a, MOVE_DELAY
	ld	(move_dly), a
	ret

;=====================================================================
; 自機を現フレームの表示 POS に合わせてテキストVRAMに配置 (スクロール追従)。
;   前回位置を消去し、新位置(cur_pos + 画面オフセット)に 2x2 を書く。
overlay_ship:
	; 前回消去 (両窓)
	ld	hl, (ship_prev)
	ld	a, h
	cp	0xFF
	jr	z, .noerase
	call	erase_3x2
	ld	hl, (ship_prev2)
	call	erase_3x2
.noerase:
	; soff = ship_row*40 + (ship_hx>>2) (画面セルオフセット)
	ld	h, 0
	ld	a, (ship_row)
	ld	l, a
	add	hl, hl
	add	hl, hl
	add	hl, hl			; *8
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, hl			; *32
	add	hl, de			; *40
	ld	a, (ship_hx)
	srl	a
	srl	a			; cell列 = hx>>2
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	(ship_soff), hl
	; 版 v = ship_hx & 3 → 先頭コード = SHIP_BASE + v*6
	ld	a, (ship_hx)
	and	3
	add	a, a			; 2v
	ld	e, a
	add	a, a			; 4v
	add	a, e			; 4v + 2v = 6v
	add	a, SHIP_BASE
	ld	(ship_code), a
	; 窓1: top-left = (cur_pos + soff) & 0x7FF
	ld	hl, (ship_soff)
	ld	de, (cur_pos)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(ship_prev), hl
	call	write_ship_3x2
	; 窓2: top-left = ((cur_pos ^ 0x400) + soff) & 0x7FF  (+1024 側の窓)
	ld	hl, (cur_pos)
	ld	a, h
	xor	0x04			; 0x0400 = +1024 窓
	ld	h, a
	ld	de, (ship_soff)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(ship_prev2), hl
	call	write_ship_3x2
	ret

; hl=top-left。選択版の 3列x2行 (6セル) を配置。コードは ship_code から連番。
;   row0: code+0,+1,+2  row1: code+3,+4,+5
write_ship_3x2:
	ld	(ws_tl), hl
	ld	a, (ship_code)
	ld	(ws_code), a
	ld	hl, (ws_tl)		; row0
	ld	b, 3
.r0:	push	bc
	push	hl
	ld	a, (ws_code)
	call	put_cell
	pop	hl
	inc	hl
	ld	a, (ws_code)
	inc	a
	ld	(ws_code), a
	pop	bc
	djnz	.r0
	ld	hl, (ws_tl)		; row1 = top + COLS
	ld	de, COLS
	add	hl, de
	ld	b, 3
.r1:	push	bc
	push	hl
	ld	a, (ws_code)
	call	put_cell
	pop	hl
	inc	hl
	ld	a, (ws_code)
	inc	a
	ld	(ws_code), a
	pop	bc
	djnz	.r1
	ret

; hl=cell(要マスク), a=code。ANK=code, ATR=SHIP_ATR。
put_cell:
	ld	e, a			; code
	ld	a, h
	and	0x07
	ld	h, a
	or	(TVRAM >> 8)
	ld	b, a
	ld	c, l
	out	(c), e			; ANK=code
	ld	a, h
	or	(TATTR >> 8)
	ld	b, a
	ld	a, SHIP_ATR
	out	(c), a			; ATR
	ret

; hl=top-left。3列x2行 を消去(ANK=0,ATR=0)。
erase_3x2:
	ld	(ws_tl), hl
	ld	b, 3			; row0
.r0:	push	bc
	push	hl
	call	clr_cell
	pop	hl
	inc	hl
	pop	bc
	djnz	.r0
	ld	hl, (ws_tl)		; row1 = top + COLS
	ld	de, COLS
	add	hl, de
	ld	b, 3
.r1:	push	bc
	push	hl
	call	clr_cell
	pop	hl
	inc	hl
	pop	bc
	djnz	.r1
	ret

clr_cell:
	ld	a, h
	and	0x07
	ld	h, a
	or	(TVRAM >> 8)
	ld	b, a
	ld	c, l
	xor	a
	out	(c), a			; ANK=0
	ld	a, h
	or	(TATTR >> 8)
	ld	b, a
	xor	a
	out	(c), a			; ATR=0
	ret

;=====================================================================
; RAM 変数 / データ
scrn_shadow:	db	0		; 0x1FD0 の最後に書いた値 (PCGMODE 付き)
; --- 自機 (M6, 開発中) ---
cur_pos:	dw	0		; このフレームの表示開始セル
ship_hx:	db	0		; 自機 横位置 (2px単位, 0..SHIP_HXMAX)
ship_row:	db	0		; 自機 画面セル行 (0..23)
ship_prev:	dw	0		; 前回の自機 top-left セル 窓1 (0xFFFF=無効)
ship_prev2:	dw	0		; 窓2
ship_soff:	dw	0		; 画面セルオフセット
ship_code:	db	0		; このフレームの先頭 PCG コード (版選択後)
ws_tl:		dw	0
ws_code:	db	0		; write_ship_3x2 の連番コード作業用
move_dly:	db	0		; 縦移動の遅延カウンタ
; --- gen_ship 作業変数 ---
gs_v:		db	0
gs_row:		db	0
gs_p:		db	0		; 位相 p=2v
gs_r:		db	0		; idx 0..23 (plane*8+raster)
gs_lsrc:	dw	0		; src 左セル先頭
gs_rsrc:	dw	0		; src 右セル先頭
gs_c1:		db	0		; シフト結果 col1
gs_c2:		db	0		; シフト結果 col2
gs_code:	db	0		; def_vr_cells 連番コード

; === 自機ベースデータ (shipdata) フォーマット ===
;   16x16 を 8x8 の 2x2 セルに分割。セル順 = TL, TR, BL, BR
;     TL=(x0-7,y0-7) TR=(x8-15,y0-7) BL=(x0-7,y8-15) BR=(x8-15,y8-15)
;   各セル = プレーン B, R, G の順。各プレーン = 8 ラスタ(y昇順)の 1 バイト。
;   バイト bit7 = 左端ピクセル, bit0 = 右端 (X1 GRAM/PCG と同じ)。
;   計 4セル x 3プレーン x 8ラスタ = 96 バイト。
;   起動時に gen_ship がこのベースから 0/2/4/6px の 4 シフト版(各3x2セル)を
;   24bit 右シフトで生成する (シフト版はここには持たない)。
;   生成/差し替えは tools/png2ship.py (16x16 RGB PNG -> この db ブロック)。
;   8色版はこのビットがそのまま色。turboZ 64色版はテキストパレットで色を
;   付けるので、色は呼び出し側で選び、ここはシルエット(例:全部R)でよい。
; 現機体 = オリジナルの矢印型(著作権配慮)。R プレーンのみ(赤)。右向き。
shipdata:
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TL B
	db	0x00,0x00,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F	; TL R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TL G
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TR B
	db	0x00,0x0C,0x3C,0xFC,0x3C,0x0C,0x00,0x00	; TR R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TR G
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL B
	db	0x3F,0x3F,0x3F,0x3F,0x3F,0x00,0x00,0x00	; BL R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL G
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BR B
	db	0x00,0x00,0x0C,0x3C,0xFC,0x3C,0x0C,0x00	; BR R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BR G
