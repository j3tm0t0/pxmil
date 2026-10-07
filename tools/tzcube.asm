; tzcube.asm - X1turboZ 15kHz 320x200 64色(1画面)描画の検証用。
;
; xmil の新規実装 (width40x25_64s / makemix_mixgrph64) が、GRAM 6プレーンから
; 正しい 6bit インデックスを生成しているかを「色キューブ」で診断する。
;
; ■ 仕組み
;   各セルの 6bit インデックス = bank0 B/R/G (bit0/1/2) | bank1 B/R/G (bit3/4/5)。
;   画面を 8x8 の繰り返しタイルにし、各セルの
;     下位3bit = col & 7  (bank0 planes)   … 横方向
;     上位3bit = row & 7  (bank1 planes)   … 縦方向
;   パレットは index k に色 {R=(k&7)*2, G=((k>>3)&7)*2, B=0} を割当てる
;   (grph4096[pal4096banktbl[0][k]] に書く)。
;   → 正しければ「赤が横方向に増え、緑が縦方向に増える」格子になる。
;     bank0/bank1 が逆なら赤緑が転置、B/R/G 取り違えなら色相がずれる、
;     真っ黒なら描画経路に到達していない。
;
; ■ 前提: ROM_TYPE=3 (turboZ)、15kHz/width40。xmil 既定 CRTC (40桁 320x200) 流用。

	DEVICE	NOSLOT64K
	ORG	0x0100

PORT_SCRN	EQU	0x1FD0
PORT_EXTPAL	EQU	0x1FB0
PORT_EXTTDISP	EQU	0x1FC0
PORT_EXTGPAL	EQU	0x1FC5
PORT_PPIC	EQU	0x1A02

SCRN_15K_200	EQU	0x02		; 15kHz 200line bank0 (access=disp=0)
SCRN_15K_ACC1	EQU	0x12		; + ACCESSVRAM=1 (bank1 書込)

start:
	di
	; width40
	ld	bc, PORT_PPIC
	xor	a
	out	(c), a
	ld	a, 0x40
	out	(c), a
	; SCRN = 15kHz 200line bank0
	ld	bc, PORT_SCRN
	ld	a, SCRN_15K_200
	out	(c), a
	; ZPRY=0 (1画面)
	ld	bc, PORT_EXTTDISP
	xor	a
	out	(c), a
	; EXTPALMODE = AEN(0x80)|64色(0x10) = 0x90
	ld	bc, PORT_EXTPAL
	ld	a, 0x90
	out	(c), a
	; EXTGRPHPAL = 0x80
	ld	bc, PORT_EXTGPAL
	ld	a, 0x80
	out	(c), a

	call	set_palette
	; bank0 面を col&7 で埋める
	ld	bc, PORT_SCRN
	ld	a, SCRN_15K_200
	out	(c), a
	call	fill_bank0
	; bank1 面を row&7 で埋める
	ld	bc, PORT_SCRN
	ld	a, SCRN_15K_ACC1
	out	(c), a
	call	fill_bank1
	; 表示 bank0
	ld	bc, PORT_SCRN
	ld	a, SCRN_15K_200
	out	(c), a
hang:
	jr	hang

; ==================================================================
; 64色パレット: k=0..63, addr=banktbl0[k],
;   grph4096[addr] = {B:0, R:(k&7)*2, G:((k>>3)&7)*2}
; 書込エンコード(crtc.c palette_o PAL_4096):
;   num = (port_lo<<4)|(value>>4) ; channel は port_hi(0x10=B,0x11=R,0x12=G)の sft。
;   addr を得るには port_lo=(addr>>4)&0xff, value の上位4bit=addr&0xf。
; ==================================================================
set_palette:
	ld	b, 0			; k
sp_loop:
	ld	a, b
	add	a, a
	ld	l, a
	ld	h, 0
	ld	de, banktbl0
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE = addr (12bit)
	; port_lo = (D<<4)|(E>>4) -> C
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
	ld	c, a			; C=port_lo
	; hi4 = (addr&0xf)<<4 -> L
	ld	a, e
	and	0x0F
	rlca
	rlca
	rlca
	rlca
	ld	l, a
	; R channel: val=(k&7)*2 | hi4
	ld	a, b
	and	7
	add	a, a
	or	l
	push	bc
	ld	b, 0x11
	out	(c), a
	pop	bc
	; G channel: val=((k>>3)&7)*2 | hi4
	ld	a, b
	rrca
	rrca
	rrca
	and	7
	add	a, a
	or	l
	push	bc
	ld	b, 0x12
	out	(c), a
	pop	bc
	; B channel: val=hi4 (Bnib=0)
	ld	a, l
	push	bc
	ld	b, 0x10
	out	(c), a
	pop	bc
	inc	b
	ld	a, b
	cp	64
	jr	nz, sp_loop
	ret

; ==================================================================
; fill_bank0: 全セル row0..24 col0..39、bank0 プレーンを col&7 の bit で。
; fill_bank1: 同じく bank1 プレーンを row&7 の bit で。
;   celladdr(HL)=row*40+col をインクリメント。
; ==================================================================
fill_bank0:
	ld	hl, 0
	ld	d, 25			; row loop
fb0r:
	xor	a
	ld	(curcol), a
	ld	e, 40			; col loop
fb0c:
	ld	a, (curcol)
	ld	(curbits), a
	call	put_cell
	ld	a, (curcol)
	inc	a
	ld	(curcol), a
	inc	hl
	dec	e
	jr	nz, fb0c
	dec	d
	jr	nz, fb0r
	ret

fill_bank1:
	ld	hl, 0
	ld	d, 25
fb1r:
	ld	a, 25
	sub	d			; row = 25-d (0..24)
	ld	(curbits), a		; bank1 bits = row (下位3bitのみ使用)
	ld	e, 40
fb1c:
	call	put_cell
	inc	hl
	dec	e
	jr	nz, fb1c
	dec	d
	jr	nz, fb1r
	ret

; put_cell: HL=cell, (curbits)下位3bit -> B/R/G プレーン(0x00/0xFF)を 8ライン出力。
;   呼び出し側のループカウンタ D(row)/E(col)/HL(cell) を保存復元する。
put_cell:
	push	de
	push	hl
	ld	(cellsave), hl
	ld	a, (curbits)
	and	1
	call	calc_byte
	ld	e, a
	ld	d, 0x40			; B base high
	call	put_plane8
	ld	a, (curbits)
	and	2
	call	calc_byte
	ld	e, a
	ld	d, 0x80			; R base high
	call	put_plane8
	ld	a, (curbits)
	and	4
	call	calc_byte
	ld	e, a
	ld	d, 0xC0			; G base high
	call	put_plane8
	pop	hl
	pop	de
	ret

; A!=0 -> 0xFF, A==0 -> 0x00
calc_byte:
	or	a
	ret	z
	ld	a, 0xFF
	ret

; put_plane8: E=plane byte, D=plane base high(0x40/0x80/0xC0), cellsave=cell。
;   port = ((D + s*8 + cell_high)<<8) | cell_low   (s=0..7)
put_plane8:
	ld	hl, (cellsave)
	ld	c, l			; C = port low = cell low (一定)
	ld	a, d
	add	a, h			; base_high + cell_high (s=0)
	ld	h, a			; H = 現在の port high
	ld	l, 8			; L = ライン数
pp8:
	ld	b, h			; B = port high
	ld	a, e			; A = plane byte
	out	(c), a			; OUT (B:C), A
	ld	a, h
	add	a, 8			; 次ライン: high += 8 (= s*0x800)
	ld	h, a
	dec	l
	jr	nz, pp8
	ret

; ---- RAM 作業変数 ----
curcol:		db	0
curbits:	db	0
cellsave:	dw	0

; ---- pal4096banktbl[0] (xmil palettes.c と一致): index -> grph4096 12bit アドレス ----
banktbl0:
	dw	0x000, 0x008, 0x080, 0x088, 0x800, 0x808, 0x880, 0x888
	dw	0x004, 0x00C, 0x084, 0x08C, 0x804, 0x80C, 0x884, 0x88C
	dw	0x040, 0x048, 0x0C0, 0x0C8, 0x840, 0x848, 0x8C0, 0x8C8
	dw	0x044, 0x04C, 0x0C4, 0x0CC, 0x844, 0x84C, 0x8C4, 0x8CC
	dw	0x400, 0x408, 0x480, 0x488, 0xC00, 0xC08, 0xC80, 0xC88
	dw	0x404, 0x40C, 0x484, 0x48C, 0xC04, 0xC0C, 0xC84, 0xC8C
	dw	0x440, 0x448, 0x4C0, 0x4C8, 0xC40, 0xC48, 0xCC0, 0xCC8
	dw	0x444, 0x44C, 0x4C4, 0x4CC, 0xC44, 0xC4C, 0xCC4, 0xCCC

	END
