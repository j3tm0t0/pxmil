; EMM / DMA テスト (xmil / pxmil 用、通常 X1 モードで動く)
;
; 1. EMM のアドレス 0x012345 に 256 バイト書き、読み戻して照合
; 2. EMM のアドレス 0 にパターンを 8000 バイト置き、
;    Z80 DMA で「EMM データポート 0x0D03 (I/O 固定)」->「GRAM 青 0x4000 (I/O 増加)」
;    へ転送。画面に青い模様が出れば EMM->DMA->GRAM 経路が動いている
; 3. テキスト左上に "EMM OK" か "EMM NG" を表示
;
; ビルド:
;   sjasmplus --raw=emmtest.bin tools/emmtest.asm
;   python3 tools/mkx1disk.py emmtest.bin -o roms/emmtest.2d -n EMMTEST

	DEVICE	NOSLOT64K
	ORG	0x0100

EMM_AL	EQU	0x0D00
EMM_DAT	EQU	0x0D03
DMA	EQU	0x1F80
TVRAM	EQU	0x3000
TATTR	EQU	0x2000
GRAM_B	EQU	0x4000
LEN	EQU	8000

start:
	di
	ld	sp, 0xF000

	; ---- 0. 画面初期化 ----
	; 疑似 IPL はブート前に I/O 0x0000-0x3FFF を 0 で埋めるため、
	; CRTC もパレットも 0 になっている。ソフト側で設定し直す。
	ld	hl, crtc_tbl
	ld	d, 0
.crtc:	ld	bc, 0x1800
	out	(c), d			; レジスタ番号
	inc	c
	ld	a, (hl)
	out	(c), a			; 値
	inc	hl
	inc	d
	ld	a, d
	cp	18
	jr	nz, .crtc
	ld	bc, 0x1000		; パレット B/R/G (標準 8 色)
	ld	a, 0xAA
	out	(c), a
	inc	b
	ld	a, 0xCC
	out	(c), a
	inc	b
	ld	a, 0xF0
	out	(c), a
	inc	b			; 0x1300 プライオリティ: テキストが前面
	xor	a
	out	(c), a

	; ---- 1. 書いて読み戻す ----
	call	set_addr_test
	ld	bc, EMM_DAT
	ld	e, 0
.w:	ld	a, e
	xor	0x5A
	out	(c), a
	inc	e
	jr	nz, .w

	call	set_addr_test
	ld	bc, EMM_DAT
	ld	e, 0
	ld	d, 0			; d = エラーフラグ
.r:	in	a, (c)
	ld	l, a
	ld	a, e
	xor	0x5A
	cp	l
	jr	z, .ok1
	ld	d, 1
.ok1:	inc	e
	jr	nz, .r

	; ---- 2. EMM アドレス 0 に模様 8000 バイト ----
	ld	bc, EMM_AL
	xor	a
	out	(c), a
	inc	c
	out	(c), a
	inc	c
	out	(c), a
	ld	bc, EMM_DAT
	ld	hl, 0
.p:	ld	a, l			; 下位バイトそのもの + 行で反転 -> 斜めの縞
	xor	h
	out	(c), a
	inc	hl
	ld	a, h
	cp	HIGH LEN
	jr	nz, .p
	ld	a, l
	cp	LOW LEN
	jr	nz, .p

	; EMM アドレスを 0 に戻してから DMA (データポート読みで自動加算される)
	ld	bc, EMM_AL
	xor	a
	out	(c), a
	inc	c
	out	(c), a
	inc	c
	out	(c), a

	ld	hl, dma_tbl
	ld	bc, DMA
	ld	e, dma_end - dma_tbl
.d:	ld	a, (hl)
	out	(c), a
	inc	hl
	dec	e
	jr	nz, .d

	; ---- 3. 結果表示 ----
	ld	hl, msg_ok
	ld	a, d
	or	a
	jr	z, .show
	ld	hl, msg_ng
.show:	ld	bc, TVRAM
.s:	ld	a, (hl)
	or	a
	jr	z, .attr
	out	(c), a
	inc	hl
	inc	bc
	jr	.s
.attr:	ld	bc, TATTR
	ld	e, 6
	ld	a, 0x07			; 白
.a:	out	(c), a
	inc	bc
	dec	e
	jr	nz, .a

.halt:	jr	.halt

set_addr_test:
	ld	bc, EMM_AL
	ld	a, 0x45
	out	(c), a
	inc	c
	ld	a, 0x23
	out	(c), a
	inc	c
	ld	a, 0x01
	out	(c), a
	ret

; Z80 DMA コマンド列: ポート A = EMM データ (I/O 固定) -> ポート B = GRAM (I/O 増加)
dma_tbl:
	db	0xC3			; WR6: リセット
	db	0x7D			; WR0: A->B 転送、A アドレス/長さが続く
	db	LOW EMM_DAT, HIGH EMM_DAT
	db	LOW (LEN - 1), HIGH (LEN - 1)
	db	0x2C			; WR1: ポート A = I/O、アドレス固定
	db	0x18			; WR2: ポート B = I/O、アドレス増加
	db	0xAD			; WR4: 連続モード、B アドレスが続く
	db	LOW GRAM_B, HIGH GRAM_B
	db	0x82			; WR5
	db	0xCF			; WR6: ロード
	db	0x87			; WR6: DMA 開始
dma_end:

crtc_tbl:			; 40 桁 x 25 行 (xmil/io/crtc.c の defreg と同じ)
	db	0x37, 0x28, 0x2d, 0x34, 0x1f, 0x02, 0x19, 0x1c, 0x00
	db	0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

msg_ok:	db	"EMM OK", 0
msg_ng:	db	"EMM NG", 0

	END
