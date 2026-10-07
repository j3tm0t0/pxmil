; tz2page.asm - 64色 1画面モードで「2ページ」を CRTC 表示開始アドレスで切替える検証。
;
; 仮説(要実機確認): 64色は6プレーン(各16KB=2048セル)。320x200=1000セルなので
;   page0 = cells 0..999、page1 = cells 1000..1999 に2枚の独立画像を置ける(96KB で2画面)。
;   表示は CRTC POSL/POSH(= crtc.e.pos = vramtop)で切替。これで動作済みの1画面
;   maker(width40x25_64s)のまま2ページ切替=4pxスクロールの土台になる。
;
; このテスト: page0 を全面 index1(赤)、page1 を全面 index4(緑)で塗り、
;   1秒ごとに CRTC POS を 0 <-> 1000 で切替える。赤<->緑 が切り替われば2ページ成立。
;
; I/O は tzcube/tzterrain と同じ。CRTC: port 0x1800=regnum, 0x1801=data。
;   X1 CRTC は 6845 互換: R12=開始アドレス上位(POSH), R13=下位(POSL)。
;   xmil の CRTCREG_POSH=12 / CRTCREG_POSL=13 (crtc.h で確認済)。

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
CRTC_POSH	EQU	12		; 6845 R12 = start addr high
CRTC_POSL	EQU	13		; 6845 R13 = start addr low

start:
	di
	ld	sp, 0xFFF0
	; width40
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

	; ---- パレット: index1=赤, index4=緑 を設定(他は黒)----
	; index1 の addr = banktbl0[1]=0x008, index4 の addr=banktbl0[4]=0x800
	; 赤: R=0xF -> grph4096[0x008] の R channel。緑: G=0xF -> grph4096[0x800] の G。
	; 書込: port_lo=(addr>>4)&0xff, val上位4bit=addr&0xf, 下位=nib。
	; index1 (addr=0x008): port_lo=0x00, addr&0xf=0x08 -> val上位=0x80
	ld	bc, 0x1100		; R channel, port_lo=0x00
	ld	a, 0x8F			; val = 0x80(addr&0xf<<4) | 0x0F(R=15)
	out	(c), a
	; index4 (addr=0x800): port_lo=0x80, addr&0xf=0 -> val上位=0
	ld	bc, 0x1280		; G channel, port_lo=0x80
	ld	a, 0x0F			; val = 0 | 0x0F(G=15)
	out	(c), a

	; ---- GRAM: page0(cells 0..999)=index1(B0 plane=全0, R0 plane=全0xFF? )----
	; index1 = bit0 set -> B0 plane。index4 = bit2 set -> G0 plane。
	; つまり page0 は bank0 の B プレーンを全0xFF(他0)、page1 は bank0 の G プレーンを全0xFF。
	; 両方 bank0 のみ(bank1 は全0)。ACCESSVRAM=0。
	ld	bc, PORT_SCRN
	ld	a, SCRN_15K_200
	out	(c), a
	; page0: cells 0..999, B プレーン(0x4000)=0xFF
	ld	hl, 0			; cell
	ld	de, 1000
	ld	a, 0x40			; B base
	call	fill_solid
	; page1: cells 1000..1999, G プレーン(0xC000)=0xFF
	ld	hl, 1000
	ld	de, 1000
	ld	a, 0xC0			; G base
	call	fill_solid

	; ---- 表示ループ: POS を 0 <-> 1000 で切替 ----
	ld	hl, 0			; 現在の POS
loop:
	call	set_pos
	call	wait_long
	ld	hl, 1000
	call	set_pos
	call	wait_long
	ld	hl, 0
	jr	loop

; set_pos: HL = 開始セルアドレス(11bit) を CRTC R12/R13 へ
set_pos:
	ld	a, CRTC_POSL
	ld	bc, PORT_CRTC
	out	(c), a			; regnum=R13
	ld	a, l
	inc	c			; port 0x1801
	out	(c), a			; R13 = low
	ld	a, CRTC_POSH
	ld	bc, PORT_CRTC
	out	(c), a			; regnum=R12
	ld	a, h
	and	7
	inc	c
	out	(c), a			; R12 = high
	ret

; fill_solid: HL=開始cell, DE=セル数, A=plane base high(0x40/0x80/0xC0)。
;   そのプレーンを 0xFF(全8スキャンライン)で埋める。
fill_solid:
	ld	(fs_base), a
fs_cell:
	ld	a, (fs_base)
	add	a, h			; + cell_hi
	ld	(fs_phi), a
	ld	c, l			; port low = cell_lo
	ld	a, 8
	ld	(fs_scnt), a
fs_s:
	ld	a, (fs_phi)
	ld	b, a
	ld	a, 0xFF
	out	(c), a
	ld	a, (fs_phi)
	add	a, 8
	ld	(fs_phi), a
	ld	a, (fs_scnt)
	dec	a
	ld	(fs_scnt), a
	jr	nz, fs_s
	inc	hl
	dec	de
	ld	a, d
	or	e
	jr	nz, fs_cell
	ret

wait_long:
	ld	bc, 0
wl1:
	dec	bc
	ld	a, b
	or	c
	jr	nz, wl1
	ret

fs_base:	db	0
fs_phi:		db	0
fs_scnt:	db	0

	END
