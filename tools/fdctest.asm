; fdctest.asm - fdcload.inc の検証用 独立プログラム(Mac native / 実機)。
;   マニフェスト(sector0 +0x20)を読み、先頭データファイル(file0)を EMM へ FDC 展開、
;   EMM を読み戻して 16bit チェックサムを計算し、probe(OUT 0x00FE/0xFF)へ出力。
;   ホスト側(python)で file0 のバイト総和(mod 65536)と一致を確認する。
;
;   ビルド:
;     sjasmplus --raw=fdctest.bin tools/fdctest.asm -I tools
;     python3 tools/mkx1disk.py fdctest.bin -o roms/FDCTEST.2d -n FDCTEST --load 0x0100 \
;         --data roms/arcade/xevious-out/allareas/common_tiles.bin
;   実行(Mac native, probe 有効):
;     SDL_VIDEODRIVER=dummy XMIL_PROBE=1 XMIL_ROM_TYPE=3 ./xmilsdl2 roms/FDCTEST.2d
;     -> stderr の "PROBE N" が file0 の総和と一致すれば OK。

	DEVICE	NONE
	ORG	0x0100

start:
	di
	ld	sp, 0xEF00
	call	fdc_init
	; --- マニフェストを RAM へ(sector0) ---
	ld	hl, man_buf
	call	fdc_read0_ram
	; --- file0 の start_sector / length ---
	;   man_buf+0x20: count(1B), +0x21: file0 [start_sector:2B, length:4B]
	ld	hl, man_buf + 0x21
	ld	c, (hl)
	inc	hl
	ld	b, (hl)			; BC = start_sector
	inc	hl
	; 診断: start_sector を probe 出力
	push	bc
	ld	d, b
	ld	e, c
	call	probe16
	pop	bc
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE = length の下位16bit
	; nsec = (len + 255) >> 8
	ld	hl, 255
	add	hl, de
	ld	a, h			; nsec(<=255 想定, file0=common_tiles≒162)
	ld	(nsec), a
	; 診断: nsec を probe 出力
	ld	e, a
	ld	d, 0
	call	probe16
	; --- EMM dst = 0 ---
	ld	hl, 0
	xor	a
	call	set_emm_dst
	; --- FDC -> EMM 展開(DE=nsec) ---
	ld	a, (nsec)
	ld	e, a
	ld	d, 0			; DE = nsec
	; BC は start_sector のまま
	call	fdc_load
	; --- EMM 読み戻し + 16bit チェックサム ---
	ld	hl, 0
	xor	a
	call	set_emm_dst		; EMM addr = 0
	ld	de, 0			; DE = checksum
	ld	a, (nsec)
	ld	b, a			; B = 残セクタ数(外ループ)
.secloop:
	ld	c, 0			; C = 256 バイト(内ループ, 0=256)
.byteloop:
	push	bc
	ld	bc, EMM_DT
	in	a, (c)			; EMM data(addr 自動 +1)
	pop	bc
	add	a, e
	ld	e, a
	jr	nc, .nocarry
	inc	d
.nocarry:
	dec	c
	jr	nz, .byteloop
	djnz	.secloop
	; --- checksum(DE)を probe 出力 ---
	call	probe16
.halt:
	halt
	jr	.halt

; ---- DE を probe(port 0x00FE/0x00FF)へ出力(out (c) 版) ----
probe16:
	push	af
	push	bc
	ld	bc, 0x00FE
	out	(c), e			; lo
	inc	c			; bc=0x00FF
	out	(c), d			; hi -> "PROBE (d<<8)|e"
	pop	bc
	pop	af
	ret

	INCLUDE	"fdcload.inc"

nsec:		db	0
	ALIGN	256
man_buf:	ds	256
