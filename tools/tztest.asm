; X1turboZ 画面モードテスター (xmil / pxmil 用)
;
; 目的: このエミュレータ上で turboZ のアナログ (4096色) パレットが
;       実際に表示されるかを確認する。
;
; 本エミュの事実 (ソース確認済み):
;   * ROM_TYPE=3 (turboZ) では電源投入時のパレットが全て黒
;     (crtc_initialize 未呼出 + ROM_TYPE>=3 で resetpal スキップ)。
;     => ソフト側でパレットを必ずプログラムする必要がある。
;   * SCRN64 専用ビットマップモード (64色/4096色) は未実装 (width_dummy)。
;     => アナログパレットを「描画される通常経路」で見せるしかない。
;   * アナログ ON (EXTPALMODE bit7) を 15kHz/width40 で行うと SCRN64 (未実装)
;     に落ちて真っ黒になる。24kHz+width80 のときだけ dispmode が SCRN64 を
;     経由せず (PAL_HIGHRESO)、グラフィック 8 色が pals.grph[1] (4bit/ch=4096色)
;     から引かれて描画される。これが「4096色パレットが効く」唯一の経路。
;
; 本テスターは 24kHz+width80 で 8 本のカラーバーを描く。各バーは
; グラフィックパレット index 0..7 に割り当てた 4096色の中間色。
; 中間レベル (0x8=0x88 等) が出れば通常 X1 では不可能な色 = turboZ 表示成功。

	DEVICE	NOSLOT64K
	ORG	0x0100

PORT_SCRN	EQU	0x1FD0		; SCRN_BITS
PORT_EXTPAL	EQU	0x1FB0		; EXTPALMODE
PORT_EXTGPAL	EQU	0x1FC5		; EXTGRPHPAL
PORT_PPIC	EQU	0x1A02		; PPI port C (bit6: 1=width40 0=width80)

SCRN_24KHZ	EQU	0x01

ROWCOLS		EQU	40		; CRTC HDISP=0x28=40 桁
BARCOLS		EQU	5		; 8色 × 5桁 = 40桁

start:
	di

	; --- 幅 80 桁へ: PPI portC bit6 を 1->0 と変化させて crtc_setwidth を発火 ---
	ld	bc, PORT_PPIC
	ld	a, 0x40			; bit6=1 -> width40
	out	(c), a
	xor	a			; bit6=0 -> width80
	out	(c), a

	; --- SCRN_BITS = 24kHz (200line/width40bit等は 0、UNDERLINE/TEXTYx2 無し) ---
	ld	bc, PORT_SCRN
	ld	a, SCRN_24KHZ
	out	(c), a

	; --- アナログパレット有効化 ---
	ld	bc, PORT_EXTPAL
	ld	a, 0x80
	out	(c), a

	; --- EXTGRPHPAL: (val&0x88)==0x80 でグラフィックパレット書込みを許可 ---
	ld	bc, PORT_EXTGPAL
	ld	a, 0x80
	out	(c), a

	; --- グラフィックパレット (pals.grph[1], index 0..7) を 4096色でプログラム ---
	;     テーブル: [port_hi, port_lo, value] * N, port_hi=0 で終端
	ld	hl, paltbl
palloop:
	ld	a, (hl)			; port high (0x10=B 0x11=R 0x12=G) / 0=終端
	or	a
	jr	z, palette_done
	ld	b, a
	inc	hl
	ld	c, (hl)			; port low
	inc	hl
	ld	a, (hl)			; value
	inc	hl
	out	(c), a
	jr	palloop
palette_done:

	; --- グラフィック VRAM (bank0) を塗る: 縦カラーバー (8色 × 5桁) ---
	;     表示桁数は CRTC reg HDISP=0x28=40 桁 (width80 bit は clock 用で
	;     表示は 40 桁)。VRAM は 1 行 40 セル連続なので col = addr mod 40。
	;     8 色 × 5 桁 = 40 桁ちょうどで綺麗な縦縞になる。
	;     1 セル = 8px, B/R/G 各プレーンを 0xFF/0x00 で単色 index に。
	ld	de, 0x0000		; de = セルアドレス
	xor	a
	ld	(kval), a		; k = 0
	ld	a, ROWCOLS
	ld	(colrem), a
	ld	a, BARCOLS
	ld	(barrem), a
gfill:
	ld	a, (kval)
	ld	l, a			; l = k (0..7)

	; --- B プレーン (port 0x4000|addr), k.bit0 ---
	ld	a, d
	or	0x40
	ld	b, a
	ld	c, e
	ld	a, l
	and	1
	jr	z, bzero
	ld	a, 0xFF
bzero:
	out	(c), a

	; --- R プレーン (port 0x8000|addr), k.bit1 ---
	ld	a, d
	or	0x80
	ld	b, a
	ld	a, l
	and	2
	jr	z, rzero
	ld	a, 0xFF
rzero:
	out	(c), a

	; --- G プレーン (port 0xC000|addr), k.bit2 ---
	ld	a, d
	or	0xC0
	ld	b, a
	ld	a, l
	and	4
	jr	z, gzero
	ld	a, 0xFF
gzero:
	out	(c), a

	; --- バー境界: 5 桁ごとに k=(k+1)&7 ---
	ld	a, (barrem)
	dec	a
	jr	nz, barkeep
	ld	a, (kval)
	inc	a
	and	7
	ld	(kval), a
	ld	a, BARCOLS
barkeep:
	ld	(barrem), a

	; --- 行境界: 40 桁ごとに k=0, バー幅リセット ---
	ld	a, (colrem)
	dec	a
	jr	nz, colkeep
	xor	a
	ld	(kval), a
	ld	a, BARCOLS
	ld	(barrem), a
	ld	a, ROWCOLS
colkeep:
	ld	(colrem), a

	inc	de
	ld	a, d
	cp	0x08			; addr < 0x800 ?
	jr	nz, gfill

hang:
	jr	hang

; --- RAM 作業変数 (0x0100+ にロードされ書換可) ---
kval:	db	0
colrem:	db	0
barrem:	db	0

; --- グラフィックパレットテーブル ---
; index k の 3 bit = [port.b7, port.b3, value.b7]。チャネルは port_hi (0x10/0x11/0x12)。
; value 下位ニブル = 輝度レベル (0..15, *0x11 で 0x00..0xFF)。
; k1=青F  k2=赤F  k3=緑F  k4=青F+赤8(中間) k5=赤F+緑8(中間) k6=緑F+青8(中間) k7=白F
paltbl:
	; k1 (b2=0,b1=0,b0=1 -> plo=0x00, val.b7=0x80): B=F R=0 G=0
	db	0x10, 0x00, 0x8F	; B
	db	0x11, 0x00, 0x80	; R
	db	0x12, 0x00, 0x80	; G
	; k2 (0,1,0 -> plo=0x08, val.b7=0): B=0 R=F G=0
	db	0x10, 0x08, 0x00
	db	0x11, 0x08, 0x0F
	db	0x12, 0x08, 0x00
	; k3 (0,1,1 -> plo=0x08, val.b7=0x80): B=0 R=0 G=F
	db	0x10, 0x08, 0x80
	db	0x11, 0x08, 0x80
	db	0x12, 0x08, 0x8F
	; k4 (1,0,0 -> plo=0x80, val.b7=0): B=F R=8 G=0
	db	0x10, 0x80, 0x0F
	db	0x11, 0x80, 0x08
	db	0x12, 0x80, 0x00
	; k5 (1,0,1 -> plo=0x80, val.b7=0x80): B=0 R=F G=8
	db	0x10, 0x80, 0x80
	db	0x11, 0x80, 0x8F
	db	0x12, 0x80, 0x88
	; k6 (1,1,0 -> plo=0x88, val.b7=0): B=8 R=0 G=F
	db	0x10, 0x88, 0x08
	db	0x11, 0x88, 0x00
	db	0x12, 0x88, 0x0F
	; k7 (1,1,1 -> plo=0x88, val.b7=0x80): B=F R=F G=F
	db	0x10, 0x88, 0x8F
	db	0x11, 0x88, 0x8F
	db	0x12, 0x88, 0x8F
	db	0x00			; 終端

	END
