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
;     SHIP_VY0             初期縦位置 (2ライン単位。行12 位相0 = 12*4=48)
;     SHIP_SCRN_BASE       ship_init で PCGMODE を立てる際の 0x1FD0 ベース
;                          (8色=0, 64色=SCRN_15K[0x02] など画面モードを保つ値)
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
;       PORT_PSG*, SHIP_ATR, SHIPGEN, SHIP_HX0, SHIP_VY0 等)を定義。
;       SHIPGEN は COLBUF と重ならない空き RAM に。
;   (5) PCGMODE 可能な状態で `call ship_init` を 1 回、毎フレーム vblank で
;       `call ship_update`。
; =====================================================================

; 横・縦とも 2px 単位で滑らかに動かすため、横4シフト x 縦4シフト = 16 版を持つ。
;   各版 = 3列x3行 = 9セル。計 16*9 = 144 セル (SHIP_BASE..+143)。
;   版番号 = (ship_vy&3)*4 + (ship_hx&3)。code = SHIP_BASE + 版*9 + cr*3 + col。
; [perf(2)] PUTCELL/CLRCELL: put_cell/clr_cell のインライン版(call/ret 除去)。
;   hl=セル(0x07xx), a=ANK コード(PUTCELL のみ)。a/b/c/e 破壊、**hl は保存**(ld h,a 廃止)。
;   hl 保存によりアンロール描画で push/pop hl 無しに inc hl で次セルへ進める。最初の使用前に定義。
	MACRO	PUTCELL
	ld	e, a
	ld	a, h
	and	0x07
	or	(TVRAM >> 8)
	ld	b, a
	ld	c, l
	out	(c), e
	ld	a, h			; h は元のまま → 再度 &7 して ATR ポートへ
	and	0x07
	or	(TATTR >> 8)
	ld	b, a
	ld	a, SHIP_ATR
	out	(c), a
	ENDM
	MACRO	CLRCELL
	ld	a, h
	and	0x07
	or	(TVRAM >> 8)
	ld	b, a
	ld	c, l
	xor	a
	out	(c), a
	ld	a, h
	and	0x07
	or	(TATTR >> 8)
	ld	b, a
	xor	a
	out	(c), a
	ENDM
	; [perf] PUTCELL_ANK: ANK(コード)のみ書く。ATR は書かない版。回転だけのフレーム
	;   (位置不変)は ATR が既に正しいので ANK だけで足り、1セルあたり OUT 1回分を削減。
	MACRO	PUTCELL_ANK
	ld	e, a
	ld	a, h
	and	0x07
	or	(TVRAM >> 8)
	ld	b, a
	ld	c, l
	out	(c), e
	ENDM

SHIP_BASE	EQU	0x10		; 自機 PCG コード先頭 (0x10..0x9F, 144セル)
SHIP_VSTRIDE	EQU	9		; 1 版 = 3列x3行 = 9セル
SHIP_HXMAX	EQU	(COLS - 3) * 4 + 3	; ship_hx 上限 (col2 が行内に収まる)
SHIP_VYMAX	EQU	(ROWS - 3) * 4 + 3	; ship_vy 上限 (行2 が画面内に収まる)
CSTAGE		EQU	SHIPGEN + 144		; 縦シフト作業 (3plane x 24 = 72B)

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
	ld	a, SHIP_SCRN_BASE	; 画面モードのベースビット (8色=0, 64色=SCRN_15K)
	call	scrn_out		; ベース + PCGMODE
	call	gen_ship
	ld	a, SHIP_HX0
	ld	(ship_hx), a
	ld	a, SHIP_VY0
	ld	(ship_vy), a
	ld	hl, 0xFFFF
	ld	(ship_prev), hl
	ld	(ship_prev2), hl
	IFDEF	SPR_1WIN
	ld	(ship_pp0), hl
	ld	(ship_pp1), hl
	ENDIF
	ret

;=====================================================================
; 毎フレーム (vblank 中)。ジョイスティック移動 + オーバーレイ。
ship_update:
	ld	a, (ship_st)
	or	a
	jr	nz, .nomove		; ALIVE 以外は移動しない(overlay_ship が隠蔽)
	call	move_ship
.nomove:
	call	overlay_ship
	ret

;=====================================================================
; [M6] 自機の PCG を定義。base 16x16(shipdata) から 横4 x 縦4 = 16 版
;   (各 3列x3行) を生成。横は 24bit 右シフト、縦はラスタ行をバイト単位で
;   2*vv 下へずらすだけ (ビット演算なし)。計 144 セル。PCG モード有効時に呼ぶ。
;   版生成は H ステージ(4回)+ 縦ずらし分割。SHIPGEN=H段(144B), CSTAGE=縦段(72B)。
gen_ship:
	xor	a
	ld	(gs_vh), a		; vh = 0..3 (横シフト)
.vhl:
	call	gen_hstage		; SHIPGEN に H 版(3col x 3plane x 16raster)生成
	xor	a
	ld	(gs_vv), a		; vv = 0..3 (縦シフト)
.vvl:
	call	gen_vsplit		; 縦 2*vv ずらし → 9 セル定義
	ld	a, (gs_vv)
	inc	a
	ld	(gs_vv), a
	cp	4
	jr	nz, .vvl
	ld	a, (gs_vh)
	inc	a
	ld	(gs_vh), a
	cp	4
	jr	nz, .vhl
	ret

; H ステージ: gs_vh で base を水平シフトし SHIPGEN[col*48 + pl*16 + r] を作る。
;   col=0..2, pl=0..2(B,R,G), r=0..15 (16ラスタ = 上下 2 セル分)。
gen_hstage:
	ld	a, (gs_vh)
	add	a, a
	ld	(gs_p), a		; p = 2*vh
	xor	a
	ld	(gs_r), a		; r = 0..15
.rl:
	ld	a, (gs_r)
	cp	8
	ld	hl, shipdata		; r<8 → 上段 (TL,TR)
	jr	c, .rtop
	ld	de, 48			; r>=8 → 下段 (BL,BR)
	add	hl, de
	sub	8
.rtop:
	ld	(gs_rr), a		; rr = r & 7
	ld	(gs_lsrc), hl		; 左セル先頭
	ld	de, 24
	add	hl, de
	ld	(gs_rsrc), hl		; 右セル先頭
	xor	a
	ld	(gs_pl), a		; pl = 0..2
.pl:
	ld	a, (gs_pl)		; byte offset = pl*8 + rr
	add	a, a
	add	a, a
	add	a, a
	ld	hl, gs_rr
	add	a, (hl)
	ld	e, a
	ld	d, 0
	ld	hl, (gs_lsrc)
	add	hl, de
	ld	b, (hl)			; 左バイト
	ld	hl, (gs_rsrc)
	add	hl, de
	ld	c, (hl)			; 右バイト
	ld	d, 0			; b2 = 0
	ld	a, (gs_p)
	or	a
	jr	z, .nos
.shh:	srl	b			; [b0 b1 b2] 右シフト = 画面右へ
	rr	c
	rr	d
	dec	a
	jr	nz, .shh
.nos:
	ld	a, c
	ld	(gs_c1), a
	ld	a, d
	ld	(gs_c2), a
	; dest = SHIPGEN + pl*16 + r (col0), +48(col1), +96(col2)
	ld	a, (gs_pl)
	add	a, a
	add	a, a
	add	a, a
	add	a, a			; pl*16
	ld	hl, gs_r
	add	a, (hl)
	ld	e, a
	ld	d, 0
	ld	hl, SHIPGEN
	add	hl, de
	ld	(hl), b			; col0
	ld	de, 48
	add	hl, de
	ld	a, (gs_c1)
	ld	(hl), a			; col1
	add	hl, de
	ld	a, (gs_c2)
	ld	(hl), a			; col2
	ld	a, (gs_pl)
	inc	a
	ld	(gs_pl), a
	cp	3
	jr	nz, .pl
	ld	a, (gs_r)
	inc	a
	ld	(gs_r), a
	cp	16
	jp	nz, .rl			; ループ本体が大きく jr 範囲外なので jp
	ret

; 縦ずらし分割: SHIPGEN の各列を 2*vv ラスタ下へずらし、3x3 セルを定義。
;   版 = vv*4 + vh。code 先頭 = SHIP_BASE + 版*9。
gen_vsplit:
	ld	a, (gs_vv)		; 版 = vv*4 + vh
	add	a, a
	add	a, a
	ld	hl, gs_vh
	add	a, (hl)
	ld	b, a			; 版
	add	a, a
	add	a, a
	add	a, a			; 版*8
	add	a, b			; 版*9
	add	a, SHIP_BASE
	ld	(gs_code), a		; 版の先頭コード
	xor	a
	ld	(gs_col), a
.cl:
	call	build_cstage		; CSTAGE に col の 3plane x 24raster (縦ずらし済)
	xor	a
	ld	(gs_cr), a
.crl:
	call	def_cell
	ld	a, (gs_cr)
	inc	a
	ld	(gs_cr), a
	cp	3
	jr	nz, .crl
	ld	a, (gs_col)
	inc	a
	ld	(gs_col), a
	cp	3
	jr	nz, .cl
	ret

; CSTAGE[pl*24 + raster] を作る: 24 を 0 埋め後、SHIPGEN の col 列(16ラスタ)を
;   2*vv オフセットへコピー。pl=0..2。
build_cstage:
	xor	a
	ld	(gs_pl), a
.bpl:
	ld	a, (gs_pl)		; CSTAGE + pl*24
	ld	l, a
	ld	h, 0
	add	hl, hl
	add	hl, hl
	add	hl, hl			; pl*8
	ld	d, h
	ld	e, l
	add	hl, hl			; pl*16
	add	hl, de			; pl*24
	ld	de, CSTAGE
	add	hl, de
	push	hl			; 0 埋め 24
	ld	(hl), 0
	ld	d, h
	ld	e, l
	inc	de
	ld	bc, 23
	ldir
	pop	hl
	ld	a, (gs_vv)		; dst = +2*vv
	add	a, a
	ld	e, a
	ld	d, 0
	add	hl, de
	ex	de, hl			; de = dst
	ld	a, (gs_col)		; src = SHIPGEN + col*48 + pl*16
	ld	l, a
	ld	h, 0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl			; col*16
	ld	b, h
	ld	c, l
	add	hl, hl			; col*32
	add	hl, bc			; col*48
	ld	bc, SHIPGEN
	add	hl, bc
	ld	a, (gs_pl)
	add	a, a
	add	a, a
	add	a, a
	add	a, a			; pl*16
	ld	c, a
	ld	b, 0
	add	hl, bc			; hl = src
	ld	bc, 16
	ldir				; 16 ラスタコピー
	ld	a, (gs_pl)
	inc	a
	ld	(gs_pl), a
	cp	3
	jr	nz, .bpl
	ret

; CSTAGE のセル (gs_cr, gs_col) を PCG 定義。code = gs_code + cr*3 + col。
def_cell:
	IFDEF	SHIP_CELL_DUMP
	call	dump_cell		; [一時] 実生成セルを PROBE ダンプ(dedup表作成用)
	ENDIF
	ld	a, (gs_cr)		; cr*8 (CSTAGE 内ラスタ offset)
	add	a, a
	add	a, a
	add	a, a
	ld	(gs_croff), a
	ld	a, (gs_cr)		; 元コード = gs_code + cr*3 + col
	ld	b, a
	add	a, a
	add	a, b			; cr*3
	ld	hl, gs_col
	add	a, (hl)
	ld	hl, gs_code
	add	a, (hl)			; 元コード(SHIP_BASE+版*9+off)
	sub	SHIP_BASE		; → ship_codetab index(版*9+off)
	ld	e, a
	ld	d, 0
	ld	hl, ship_codetab
	add	hl, de
	ld	a, (hl)			; dedup後の実PCGコード(重複セルは共有)
	call	pcg_select
	ld	a, (gs_croff)		; B: CSTAGE + 0 + cr*8
	ld	e, a
	ld	d, 0
	ld	hl, CSTAGE
	add	hl, de
	ld	d, PCG_B >> 8
	call	def_plane
	ld	a, (gs_croff)		; R: CSTAGE + 24 + cr*8
	ld	e, a
	ld	d, 0
	ld	hl, CSTAGE + 24
	add	hl, de
	ld	d, PCG_R >> 8
	call	def_plane
	ld	a, (gs_croff)		; G: CSTAGE + 48 + cr*8
	ld	e, a
	ld	d, 0
	ld	hl, CSTAGE + 48
	add	hl, de
	ld	d, PCG_G >> 8
	call	def_plane
	ret

	IFDEF	SHIP_CELL_DUMP
; [一時] 現セルを PROBE 出力。code marker(PROBE=0xC000|code) + 24B(hi=0)。
;   gs_croff を scratch に使う(def_cell が後で再設定)。
dump_cell:
	ld	a, (gs_cr)		; code = gs_code + cr*3 + col
	ld	b, a
	add	a, a
	add	a, b			; cr*3
	ld	hl, gs_col
	add	a, (hl)
	ld	hl, gs_code
	add	a, (hl)
	ld	e, a			; marker: PROBE 0xC000|code
	ld	bc, 0x00FE
	out	(c), e
	ld	a, 0xC0
	ld	bc, 0x00FF
	out	(c), a
	ld	a, (gs_cr)		; croff = cr*8
	add	a, a
	add	a, a
	add	a, a
	ld	(gs_croff), a
	ld	a, (gs_croff)		; B: CSTAGE + croff
	ld	e, a
	ld	d, 0
	ld	hl, CSTAGE
	add	hl, de
	ld	b, 8
	call	.out8
	ld	a, (gs_croff)		; R: CSTAGE + 24 + croff
	ld	e, a
	ld	d, 0
	ld	hl, CSTAGE + 24
	add	hl, de
	ld	b, 8
	call	.out8
	ld	a, (gs_croff)		; G: CSTAGE + 48 + croff
	ld	e, a
	ld	d, 0
	ld	hl, CSTAGE + 48
	add	hl, de
	ld	b, 8
	call	.out8
	ret
.out8:					; PROBE b bytes from (hl), hi=0
	ld	a, (hl)
	ld	e, a
	push	bc
	ld	bc, 0x00FE
	out	(c), e
	ld	e, 0
	ld	bc, 0x00FF
	out	(c), e
	pop	bc
	inc	hl
	djnz	.out8
	ret
	ENDIF

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
; 自機移動。横 ship_hx(2px単位)・縦 ship_vy(2ライン単位) とも毎フレーム ±1
;   (= 2px/フレーム, アーケード風の滑らかさ)。いずれも境界付き。
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
	; --- 縦: 毎フレーム 2ライン ---
	bit	0, e			; 上
	jr	nz, .nu
	ld	a, (ship_vy)
	or	a
	jr	z, .nu
	dec	a
	ld	(ship_vy), a
.nu:	bit	1, e			; 下
	jr	nz, .nd
	ld	a, (ship_vy)
	cp	SHIP_VYMAX
	jr	nc, .nd
	inc	a
	ld	(ship_vy), a
.nd:
	ret

;=====================================================================
; 自機を現フレームの表示 POS に合わせてテキストVRAMに配置 (スクロール追従)。
;   前回位置を消去し、新位置(cur_pos + 画面オフセット)に 2x2 を書く。
	IFDEF	SPR_1WIN
; [2窓削減] 表示ページ(cur_pos bit10)のみ毎フレーム erase+draw。非表示ページは
;   そのページが表示される時(スクロールステップで cur_pos の bit10 が反転した時)に
;   初めて erase+draw される。sprite は TVRAM 層で terrain(GRAM/chunk)と別レイヤ
;   なので、非表示ページに残した stale sprite は chunk に壊されない。描画コスト半減。
overlay_ship:
	; --- soff / code 計算 ---
	ld	a, (ship_vy)
	srl	a
	srl	a			; cell行 = vy>>2
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
	ld	a, (ship_hx)
	srl	a
	srl	a			; cell列 = hx>>2
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	(ship_soff), hl
	ld	a, (ship_vy)
	and	3
	add	a, a
	add	a, a			; (vy&3)*4
	ld	b, a
	ld	a, (ship_hx)
	and	3
	add	a, b			; 版
	ld	b, a
	add	a, a
	add	a, a
	add	a, a			; 版*8
	add	a, b			; 版*9
	ld	e, a			; ws_codeptr = ship_codetab + 版*9 (dedup表)
	ld	d, 0
	ld	hl, ship_codetab
	add	hl, de
	ld	(ws_codeptr), hl
	; --- 表示ページの prev を指す ---
	ld	a, (cur_pos + 1)	; cur_pos 上位バイト
	and	0x04			; bit10 (=0x400)
	rrca				; idx*2 (0x04->0x02, 0->0)
	ld	e, a
	ld	d, 0
	ld	hl, ship_pp0
	add	hl, de
	ld	(ship_ppp), hl		; = &ship_pp[idx]
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; de = prev addr
	ld	a, d
	cp	0xFF
	jr	z, .noerase1
	ex	de, hl
	call	erase_3x3		; hl = prev
.noerase1:
	; target = (cur_pos + soff) & 0x7FF、表示ページに描く
	ld	hl, (ship_soff)
	ld	de, (cur_pos)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a			; hl = target
	ld	e, l
	ld	d, h
	ld	hl, (ship_ppp)
	ld	(hl), e
	inc	hl
	ld	(hl), d			; ship_pp[idx] = target
	ex	de, hl			; hl = target
	call	write_ship_3x3
	ret
	ELSE
overlay_ship:
	; [死亡演出] ALIVE 以外は自機を隠す: 旧位置を一度だけ消去し prev を無効化。
	ld	a, (ship_st)
	or	a
	jr	z, .alive
	ld	hl, (ship_prev)
	ld	a, h
	cp	0xFF
	ret	z			; 既に消去済 → 何もしない
	call	erase_3x3
	ld	hl, (ship_prev2)
	call	erase_3x3
	ld	hl, 0xFFFF
	ld	(ship_prev), hl
	ld	(ship_prev2), hl
	ret
.alive:
	; 前回消去 (両窓)。自機は弾の銃口貫通で欠けるため idle-skip しない(毎フレーム描画)。
	ld	hl, (ship_prev)
	ld	a, h
	cp	0xFF
	jr	z, .noerase
	call	erase_3x3
	ld	hl, (ship_prev2)
	call	erase_3x3
.noerase:
	; soff = (ship_vy>>2)*40 + (ship_hx>>2) (画面セルオフセット)
	ld	a, (ship_vy)
	srl	a
	srl	a			; cell行 = vy>>2
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
	ld	a, (ship_hx)
	srl	a
	srl	a			; cell列 = hx>>2
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	(ship_soff), hl
	; 版 = (vy&3)*4 + (hx&3) → 先頭コード = SHIP_BASE + 版*9
	ld	a, (ship_vy)
	and	3
	add	a, a
	add	a, a			; (vy&3)*4
	ld	b, a
	ld	a, (ship_hx)
	and	3
	add	a, b			; 版
	ld	b, a
	add	a, a
	add	a, a
	add	a, a			; 版*8
	add	a, b			; 版*9
	ld	e, a			; ws_codeptr = ship_codetab + 版*9 (dedup表)
	ld	d, 0
	ld	hl, ship_codetab
	add	hl, de
	ld	(ws_codeptr), hl
	; 窓1: top-left = (cur_pos + soff) & 0x7FF
	ld	hl, (ship_soff)
	ld	de, (cur_pos)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(ship_prev), hl
	call	write_ship_3x3
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
	call	write_ship_3x3
	ret
	ENDIF

; hl=top-left。選択版の 3列x3行 (9セル) を配置。コードは ship_code から連番。
;   row r (0..2): code+r*3+0,+1,+2  cell = top + r*COLS + col
; [perf] 1セル配置マクロ: de=コードptr から1つ読んで配置。PUTCELL は hl 保存なので
;   行内は inc hl で次セルへ(push/pop hl 不要)。PUTCELL が e を壊すので de のみ push/pop。
	MACRO	WSC
	ld	a, (de)
	inc	de
	push	de
	PUTCELL
	pop	de
	ENDM
write_ship_3x3:
	; [perf] アンロール(ループ/push-pop bc/push-pop hl 除去)。ws_codeptr は overlay_ship が
	;   ship_codetab+版*9 に設定済。9セルを順次参照し、窓2の呼び出しに備え ws_codeptr を進めて書き戻す。
	ld	de, (ws_codeptr)
	WSC				; (0,0)
	inc	hl
	WSC				; (0,1)
	inc	hl
	WSC				; (0,2)
	ld	bc, COLS - 2
	add	hl, bc			; 次行先頭
	WSC				; (1,0)
	inc	hl
	WSC				; (1,1)
	inc	hl
	WSC				; (1,2)
	ld	bc, COLS - 2
	add	hl, bc
	WSC				; (2,0)
	inc	hl
	WSC				; (2,1)
	inc	hl
	WSC				; (2,2)
	ld	(ws_codeptr), de	; 進めた位置を保存(現挙動を維持)
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

; hl=top-left。3列x3行 を消去(ANK=0,ATR=0)。[perf] アンロール(CLRCELL は hl 保存)。
erase_3x3:
	CLRCELL
	inc	hl
	CLRCELL
	inc	hl
	CLRCELL
	ld	bc, COLS - 2
	add	hl, bc
	CLRCELL
	inc	hl
	CLRCELL
	inc	hl
	CLRCELL
	ld	bc, COLS - 2
	add	hl, bc
	CLRCELL
	inc	hl
	CLRCELL
	inc	hl
	CLRCELL
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
ship_vy:	db	0		; 自機 縦位置 (2ライン単位, 0..SHIP_VYMAX)
ship_prev:	dw	0		; 前回の自機 top-left セル 窓1 (0xFFFF=無効)
ship_prev2:	dw	0		; 窓2
ship_soff:	dw	0		; 画面セルオフセット
ship_code:	db	0		; このフレームの先頭 PCG コード (版選択後)
	IFDEF	SPR_1WIN
ship_pp0:	dw	0xFFFF		; [2窓削減] page0(bit10=0) の前回 top-left
ship_pp1:	dw	0xFFFF		; page1(bit10=1) の前回 top-left
ship_ppp:	dw	0		; 今フレームの対象 prev へのポインタ(scratch)
	ENDIF
ws_tl:		dw	0
ws_code:	db	0		; (未使用) 旧連番コード
ws_codeptr:	dw	0		; write_ship_3x3 が参照する ship_codetab 内ポインタ
; 自機144セル(16版×9)の重複除去表: index=版*9+off(=元code-SHIP_BASE) → dedup後の実PCGコード。
;   byte完全一致セルのみ共有(ロスレス)。exact-dedup は 103 ユニークで、自機専用の空きコード
;   プール 0x10..0x66(87) ∪ 0x92..0x9F(14)=101 にほぼ収まる。超過 2 セルのみバイト差 1/2 の
;   最近傍対へマージ(ほぼ不可視)。空白(全0)は 0x12 に集約し非空白と絶対に混ぜない。
;   0x67..0x91 は Bacura 常駐なので使わない。
;   ※tools/ship_dedup.py を SHIP_CELL_DUMP ログに適用して生成。自機パターン変更時は要再生成。
ship_codetab:
	db	0x10,0x11,0x12,0x13,0x14,0x12,0x12,0x12,0x12	; 版0
	db	0x15,0x16,0x17,0x18,0x19,0x1A,0x12,0x12,0x12	; 版1
	db	0x1B,0x1C,0x1D,0x1E,0x1F,0x20,0x12,0x12,0x12	; 版2
	db	0x21,0x22,0x23,0x24,0x25,0x26,0x12,0x12,0x12	; 版3
	db	0x27,0x28,0x12,0x29,0x2A,0x12,0x2B,0x12,0x12	; 版4
	db	0x2C,0x2D,0x12,0x2E,0x2F,0x30,0x31,0x1A,0x12	; 版5
	db	0x32,0x33,0x12,0x34,0x35,0x36,0x37,0x38,0x12	; 版6
	db	0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x40,0x12	; 版7
	db	0x41,0x42,0x12,0x43,0x44,0x12,0x45,0x1A,0x12	; 版8
	db	0x46,0x47,0x12,0x48,0x49,0x4A,0x4B,0x4C,0x12	; 版9
	db	0x4D,0x4E,0x12,0x4F,0x50,0x51,0x52,0x53,0x12	; 版10
	db	0x54,0x55,0x12,0x56,0x57,0x58,0x59,0x5A,0x12	; 版11
	db	0x5B,0x12,0x12,0x5C,0x5D,0x12,0x5E,0x5F,0x12	; 版12
	db	0x60,0x42,0x12,0x61,0x62,0x63,0x64,0x65,0x12	; 版13
	db	0x66,0x92,0x12,0x93,0x94,0x95,0x96,0x97,0x12	; 版14
	db	0x98,0x99,0x12,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F	; 版15
; --- gen_ship 作業変数 ---
gs_vh:		db	0		; 横シフト版 0..3
gs_vv:		db	0		; 縦シフト版 0..3
gs_col:		db	0		; 0..2
gs_cr:		db	0		; セル行 0..2
gs_pl:		db	0		; プレーン 0..2
gs_r:		db	0		; ラスタ 0..15
gs_rr:		db	0		; r & 7
gs_croff:	db	0		; cr*8
gs_p:		db	0		; 位相 p=2*vh
gs_lsrc:	dw	0		; src 左セル先頭
gs_rsrc:	dw	0		; src 右セル先頭
gs_c1:		db	0		; シフト結果 col1
gs_c2:		db	0		; シフト結果 col2
gs_code:	db	0		; 版の先頭コード

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
; 自機ベースデータ (96バイト)。差し替えフック:
;   -DSHIP_EXTDATA 指定時は外部ファイル roms/shipdata_ext.inc を使う。
;   (抽出スプライト等の差し替え用。外部データはコミットしない方針=各自用意。
;    roms/ は gitignore 済み。png2ship.py で 16x16 PNG から生成できる。)
;   未指定(既定)は下のオリジナル矢印機体を使う。
shipdata:
	IFDEF	SHIP_EXTDATA
	INCLUDE	"roms/shipdata_ext.inc"	; 外部提供の 96バイト db ブロック (gitignore)
	ELSE
; オリジナルの矢印型(著作権配慮)。右向き。2色。パレットD(公式スロット)に合わせ、
;   各ピクセルの有効プレーン合成 tc をスロット番号に:
;   後方ボディ = B のみ → tc1 → slot1(白)。前方ノーズ(TR/BR) = B+G → tc5 → slot5(赤)。
;   R プレーンは未使用。SHIP_ATR=0x27(全プレーン有効)で per-pixel にスロット選択。
	db	0x00,0x00,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F	; TL B (body → slot1 白)
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TL R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TL G
	db	0x00,0x0C,0x3C,0xFC,0x3C,0x0C,0x00,0x00	; TR B (nose)
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TR R
	db	0x00,0x0C,0x3C,0xFC,0x3C,0x0C,0x00,0x00	; TR G (nose, B+G → slot5 赤)
	db	0x3F,0x3F,0x3F,0x3F,0x3F,0x00,0x00,0x00	; BL B (body → slot1 白)
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL G
	db	0x00,0x00,0x0C,0x3C,0xFC,0x3C,0x0C,0x00	; BR B (nose)
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BR R
	db	0x00,0x00,0x0C,0x3C,0xFC,0x3C,0x0C,0x00	; BR G (nose, B+G → slot5 赤)
	ENDIF
