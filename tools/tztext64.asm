; tztext64.asm - turboZ テキストパレット(I/O 0x1FB9-0x1FBF, 2bit/ch, AEN必要)が
;   15kHz 320x200 64色 GRAM モードと併用できるかの検証。
;   64色地形を描いた上に、テキストで 7 色の色見本(横バー)を重ねて表示する。
;
; 依存(先に実行): python3 -P tools/xevi_gram64.py
;   -> roms/arcade/xevious-out/terrain64_gram.bin (48000 byte, 6 plane)
;      roms/arcade/xevious-out/terrain64_pal.bin  (320 byte, 64 entry)
;
; ビルド(project root から):
;   sjasmplus --raw=out.bin tools/tztext64.asm
;   python3 -P tools/mkx1disk.py out.bin -o t.2d -n TZTEXT64 --load 0x0100
;
; 期待: 地形の上に 7 本の横バーが各色(青/赤/紫/緑/水/黄/白)で出れば、
;   アナログテキストパレット + 64色 GRAM の併用が xmil で成立。

	DEVICE	NOSLOT64K
	ORG	0x0100

PORT_SCRN	EQU	0x1FD0
PORT_EXTPAL	EQU	0x1FB0
PORT_EXTTDISP	EQU	0x1FC0
PORT_EXTGPAL	EQU	0x1FC5
PORT_PPIC	EQU	0x1A02
SCRN_15K_200	EQU	0x02
SCRN_15K_ACC1	EQU	0x12
TVRAM		EQU	0x3000
TATTR		EQU	0x2000

start:
	di
	ld	sp, 0xFFF0
	; width40
	ld	bc, PORT_PPIC
	xor	a
	out	(c), a
	ld	a, 0x40
	out	(c), a
	; 15kHz 200line bank0
	ld	bc, PORT_SCRN
	ld	a, SCRN_15K_200
	out	(c), a
	; ZPRY=0
	ld	bc, PORT_EXTTDISP
	xor	a
	out	(c), a
	; EXTPALMODE=0x90 (AEN|64色) -- AEN はアナログテキストパレットにも必須
	ld	bc, PORT_EXTPAL
	ld	a, 0x90
	out	(c), a
	; EXTGRPHPAL=0x80
	ld	bc, PORT_EXTGPAL
	ld	a, 0x80
	out	(c), a

	call	load_palette
	; bank0 面 (B0,R0,G0)
	ld	bc, PORT_SCRN
	ld	a, SCRN_15K_200
	out	(c), a
	ld	hl, gramdata
	ld	a, 0x40
	ld	(planebase), a
	call	copy_plane		; B0
	ld	a, 0x80
	ld	(planebase), a
	call	copy_plane		; R0
	ld	a, 0xC0
	ld	(planebase), a
	call	copy_plane		; G0
	; bank1 面 (B1,R1,G1)
	ld	bc, PORT_SCRN
	ld	a, SCRN_15K_ACC1
	out	(c), a
	ld	a, 0x40
	ld	(planebase), a
	call	copy_plane		; B1
	ld	a, 0x80
	ld	(planebase), a
	call	copy_plane		; R1
	ld	a, 0xC0
	ld	(planebase), a
	call	copy_plane		; G1
	; 表示 bank0
	ld	bc, PORT_SCRN
	ld	a, SCRN_15K_200
	out	(c), a

	call	clr_tram		; テキスト面を消去(attr=0=透明)
	call	set_textpal		; アナログテキストパレット(7色)
	call	draw_samples		; 7色の色見本バー
hang:
	jr	hang

; ------------------------------------------------------------------
; set_textpal: 0x1FB9-0x1FBF にアナログテキスト色(2bit/ch)を書く。
;   値 = (G<<4)|(R<<2)|B, 各0..3。色 1..7 を順に。
; ------------------------------------------------------------------
set_textpal:
	ld	hl, textpaldata
	ld	bc, 0x1FB9
stp_loop:
	ld	a, (hl)
	out	(c), a
	inc	hl
	inc	c
	ld	a, c
	cp	0xC0
	jr	nz, stp_loop
	ret

; ------------------------------------------------------------------
; clr_tram: TVRAM(char=space) と TATTR(attr=0) を全セル消去。
; ------------------------------------------------------------------
clr_tram:
	ld	hl, 0
clt_loop:
	ld	a, h
	and	0x07
	or	(TVRAM >> 8)
	ld	b, a
	ld	c, l
	ld	a, 0x20			; space
	out	(c), a
	ld	a, h
	and	0x07
	or	(TATTR >> 8)
	ld	b, a
	xor	a
	out	(c), a			; attr = 0 (透明)
	inc	hl
	ld	a, h
	cp	0x08
	jr	nz, clt_loop
	ret

; ------------------------------------------------------------------
; draw_samples: 色 1..7 の横バーを描く。
;   色 c: row = 2 + c*2, cell = row*40 + 4, char=0xFF を 8 セル, attr=c。
; ------------------------------------------------------------------
draw_samples:
	ld	a, 1
	ld	(ds_col), a
ds_outer:
	; row = 2 + col*2
	ld	a, (ds_col)
	add	a, a
	add	a, 2			; a = row
	; hl = row*40
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
	ld	de, 4
	add	hl, de			; + 4 (左マージン)
	ld	b, 8			; 8 セル
ds_inner:
	push	bc
	push	hl
	; ANK = 0xFF
	ld	a, h
	and	0x07
	or	(TVRAM >> 8)
	ld	b, a
	ld	c, l
	ld	a, 0xFF
	out	(c), a
	; ATR = ds_col (CHR, 色 = 下位3bit)
	ld	a, h
	and	0x07
	or	(TATTR >> 8)
	ld	b, a
	ld	a, (ds_col)
	out	(c), a
	pop	hl
	inc	hl
	pop	bc
	djnz	ds_inner
	ld	a, (ds_col)
	inc	a
	ld	(ds_col), a
	cp	8
	jr	nz, ds_outer
	ret

; ------------------------------------------------------------------
; load_palette (tzterrain.asm と同一): paldata の 64 エントリを grph4096 へ。
; ------------------------------------------------------------------
load_palette:
	ld	ix, paldata
	ld	a, 64
	ld	(palcnt), a
lp_loop:
	ld	e, (ix+0)
	ld	d, (ix+1)		; DE=addr
	ld	a, e
	rrca
	rrca
	rrca
	rrca
	and	0x0F
	ld	c, a
	ld	a, d
	rlca
	rlca
	rlca
	rlca
	and	0xF0
	or	c
	ld	c, a
	ld	a, e
	and	0x0F
	rlca
	rlca
	rlca
	rlca
	ld	l, a
	ld	a, (ix+2)
	or	l
	ld	b, 0x10
	out	(c), a
	ld	a, (ix+3)
	or	l
	ld	b, 0x11
	out	(c), a
	ld	a, (ix+4)
	or	l
	ld	b, 0x12
	out	(c), a
	ld	de, 5
	add	ix, de
	ld	a, (palcnt)
	dec	a
	ld	(palcnt), a
	jr	nz, lp_loop
	ret

; ------------------------------------------------------------------
; copy_plane (tzterrain.asm と同一)
; ------------------------------------------------------------------
copy_plane:
	ld	de, 0
cp_cell:
	ld	a, (planebase)
	add	a, d
	ld	(cp_phi), a
	ld	c, e
	ld	a, 8
	ld	(cp_scnt), a
cp_s:
	ld	a, (cp_phi)
	ld	b, a
	ld	a, (hl)
	out	(c), a
	inc	hl
	ld	a, (cp_phi)
	add	a, 8
	ld	(cp_phi), a
	ld	a, (cp_scnt)
	dec	a
	ld	(cp_scnt), a
	jr	nz, cp_s
	inc	de
	ld	a, e
	cp	0xE8
	jr	nz, cp_cell
	ld	a, d
	cp	0x03
	jr	nz, cp_cell
	ret

; ---- 作業変数 ----
palcnt:		db	0
planebase:	db	0
cp_phi:		db	0
cp_scnt:	db	0
ds_col:		db	0

; 色 1..7 (値 = (G<<4)|(R<<2)|B, 各0..3)
;   既定(配布版): 青/赤/紫/緑/水/黄/白。2bit/ch 確認時は下行に差し替え。
textpaldata:
	db	0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F
; 2bit/ch 4階調確認用: B=1/2/3, R=1/2/3, 灰(各1)
;	db	0x01, 0x02, 0x03, 0x04, 0x08, 0x0C, 0x15

paldata:
	incbin	"roms/arcade/xevious-out/terrain64_pal.bin"
gramdata:
	incbin	"roms/arcade/xevious-out/terrain64_gram.bin"
