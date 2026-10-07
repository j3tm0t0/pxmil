; tz2terr.asm - 地形を 64色 2ページ(page0=cells 0..999, page1=cells 1000..1999)に
;   同一データで置き、CRTC POS を 0 <-> 1000 で切替える検証。
;
; 目的(advisor 指摘への対応): tz2page はソリッド塗りだったため、page1 のセル対応
;   (cell = 1000 + row*40 + col)が表示側の読み出しと一致するかを「実地形」で確認する。
;   両ページで同一地形が歪み/折返しなく出れば、2ページのセルマッピングが正しい
;   (HDISP=40 / surfrx=0 前提。HDISP!=40 では stride が変わる点は別途注意)。
;
; 依存: python3 -P tools/xevi_gram64.py (terrain64_gram.bin 48000B, terrain64_pal.bin 320B)

	DEVICE	NOSLOT64K
	ORG	0x0100

PORT_SCRN	EQU	0x1FD0
PORT_EXTPAL	EQU	0x1FB0
PORT_EXTTDISP	EQU	0x1FC0
PORT_EXTGPAL	EQU	0x1FC5
PORT_PPIC	EQU	0x1A02
PORT_CRTC	EQU	0x1800
SCRN_15K_200	EQU	0x02
SCRN_15K_ACC1	EQU	0x12

start:
	di
	ld	sp, 0xFFF0
	ld	bc, PORT_PPIC
	xor	a
	out	(c), a
	ld	a, 0x40
	out	(c), a
	ld	bc, PORT_SCRN
	ld	a, SCRN_15K_200
	out	(c), a
	ld	bc, PORT_EXTTDISP
	xor	a
	out	(c), a
	ld	bc, PORT_EXTPAL
	ld	a, 0x90
	out	(c), a
	ld	bc, PORT_EXTGPAL
	ld	a, 0x80
	out	(c), a

	call	load_palette

	; ---- page0: cells 0..999 へ 6プレーン ----
	ld	a, 0
	ld	(cp_start), a
	ld	a, 0
	ld	(cp_start+1), a
	call	load_gram
	; ---- page1: cells 1000..1999 へ 同一 6プレーン ----
	ld	hl, 1000
	ld	a, l
	ld	(cp_start), a
	ld	a, h
	ld	(cp_start+1), a
	call	load_gram

	; ---- POS 切替ループ: 0 <-> 1000 ----
page_loop:
	ld	hl, 0
	call	set_pos
	call	wait_long
	ld	hl, 1000
	call	set_pos
	call	wait_long
	jr	page_loop

; load_gram: terrain64_gram.bin の6プレーンを現在の (cp_start) セルから書く。
;   bank0(B0,R0,G0) -> ACCESS=0, bank1(B1,R1,G1) -> ACCESS=1。
load_gram:
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
	ret

; set_pos: HL = 開始セル -> CRTC R12(POSH=12)/R13(POSL=13)
set_pos:
	ld	a, 13			; R13 = POSL
	ld	bc, PORT_CRTC
	out	(c), a
	ld	a, l
	inc	c
	out	(c), a
	ld	a, 12			; R12 = POSH
	ld	bc, PORT_CRTC
	out	(c), a
	ld	a, h
	and	7
	inc	c
	out	(c), a
	ret

; copy_plane: HL=データ(8000B), (planebase)=0x40/0x80/0xC0, (cp_start)=開始セル。
copy_plane:
	ld	de, (cp_start)		; DE = 開始セル
	ld	bc, 1000		; セル数
cp_cell:
	ld	a, (planebase)
	add	a, d			; + cell_hi
	ld	(cp_phi), a
	push	bc
	ld	c, e			; port low = cell_lo
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
	pop	bc
	inc	de
	dec	bc
	ld	a, b
	or	c
	jr	nz, cp_cell
	ret

wait_long:
	ld	de, 0
wl1:
	dec	de
	ld	a, d
	or	e
	jr	nz, wl1
	ret

load_palette:
	ld	ix, paldata
	ld	a, 64
	ld	(palcnt), a
lp_loop:
	ld	e, (ix+0)
	ld	d, (ix+1)
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

palcnt:		db	0
planebase:	db	0
cp_phi:		db	0
cp_scnt:	db	0
cp_start:	dw	0

paldata:
	incbin	"roms/arcade/xevious-out/terrain64_pal.bin"
gramdata:
	incbin	"roms/arcade/xevious-out/terrain64_gram.bin"

	END
