; tzterrain.asm - xevi_gram64.py が出力した地形を 15kHz 320x200 64色で表示する。
;
; 依存(先に実行): python3 -P tools/xevi_gram64.py
;   -> roms/arcade/xevious-out/terrain64_gram.bin (48000 byte, 6 plane)
;      roms/arcade/xevious-out/terrain64_pal.bin  (320 byte, 64 entry)
;
; GRAM/パレットを本体に incbin し、RAM からコピーする(DMA 無し, 検証用)。
; ビルド(project root から):
;   sjasmplus --raw=out.bin tools/tzterrain.asm   (incbin はこのパス基準)
;   python3 -P tools/mkx1disk.py out.bin -o t.2d -n TZTERR --load 0x8000
; ※ 本体+データで ~48KB。ロードアドレスは 0x8000 (データ領域と被らないよう)…
;    ではなく、X1 の GRAM は I/O OUT 経由なので通常 RAM 0x0100 に置いてよい。

	DEVICE	NOSLOT64K
	ORG	0x0100

PORT_SCRN	EQU	0x1FD0
PORT_EXTPAL	EQU	0x1FB0
PORT_EXTTDISP	EQU	0x1FC0
PORT_EXTGPAL	EQU	0x1FC5
PORT_PPIC	EQU	0x1A02
SCRN_15K_200	EQU	0x02
SCRN_15K_ACC1	EQU	0x12

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
	; EXTPALMODE=0x90 (AEN|64色)
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
hang:
	jr	hang

; ------------------------------------------------------------------
; load_palette: paldata の 64 エントリ [addr_lo,addr_hi,Bnib,Rnib,Gnib] を
;   grph4096 に書く。port_lo=(addr>>4)&0xff, 値上位4bit=addr&0xf, 下位=nib。
; ------------------------------------------------------------------
load_palette:
	ld	ix, paldata
	ld	a, 64
	ld	(palcnt), a
lp_loop:
	ld	e, (ix+0)
	ld	d, (ix+1)		; DE=addr
	; port_lo=(D<<4)|(E>>4) -> C
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
	; hi4=(addr&0xf)<<4 -> L
	ld	a, e
	and	0x0F
	rlca
	rlca
	rlca
	rlca
	ld	l, a
	; B channel (port 0x10): val = Bnib | hi4
	ld	a, (ix+2)
	or	l
	ld	b, 0x10
	out	(c), a
	; R channel (0x11): Rnib
	ld	a, (ix+3)
	or	l
	ld	b, 0x11
	out	(c), a
	; G channel (0x12): Gnib
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
; copy_plane: HL=データ先頭(8000byte), (planebase)=0x40/0x80/0xC0。
;   1000 セル × 8 スキャンライン。port=(planebase+s*8+cell_hi):cell_lo。
;   HL は呼出後、次プレーン先頭を指す。
; ------------------------------------------------------------------
copy_plane:
	ld	de, 0			; DE = cell (0..999)
cp_cell:
	ld	a, (planebase)
	add	a, d			; + cell_hi (s=0 の port high)
	ld	(cp_phi), a
	ld	c, e			; C = port low = cell_lo
	ld	a, 8
	ld	(cp_scnt), a
cp_s:
	ld	a, (cp_phi)
	ld	b, a			; B = port high
	ld	a, (hl)			; data byte
	out	(c), a			; OUT (B:C), A
	inc	hl
	ld	a, (cp_phi)
	add	a, 8			; 次ライン: port high += 8
	ld	(cp_phi), a
	ld	a, (cp_scnt)
	dec	a
	ld	(cp_scnt), a
	jr	nz, cp_s
	inc	de			; 次セル
	ld	a, e
	cp	0xE8			; 1000 = 0x03E8 ?
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

paldata:
	incbin	"roms/arcade/xevious-out/terrain64_pal.bin"
gramdata:
	incbin	"roms/arcade/xevious-out/terrain64_gram.bin"

	END
