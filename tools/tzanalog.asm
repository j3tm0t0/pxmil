; tzanalog.asm - X1turboZ 実機プローブ: 15kHz / width40 / 320x200 / 3プレーン8色
;                グラフィックで「アナログパレット(4096中8色)」が効くかを判定する。
;
; ■ なぜこのプローブが必要か (調査結論)
;   「15kHz 320x200 の標準8色(3プレーン)グラフィックでアナログパレットが
;    使えるか」は、入手できた一次資料(oh!X/akiba/hachibitto 等)では port
;    レベルまで確定できなかった。二次資料(エミュのソース)は以下で一致するが、
;    いずれも "width40 + AEN + 8色" という同じ経路を実装して「いない」:
;     - xmil: AEN(EXTPALMODE bit7)を 15kHz/width40 で立てると dispmode が
;       SCRN64(4096/64色パック, 未実装)へ落ちる。8色+アナログは
;       24kHz+width80(PAL_HIGHRESO)経路のみ。
;     - MAME(sharp/x1_v.cpp): draw_gfxbitmap は全モードで3プレーン8色固定描画で、
;       解像度分岐なし。ただしアナログパレットはペン16+に置くだけで基本8色へは
;       適用しない(未完)。パック読みも未実装。
;   => 2つのエミュが「同じ空白」を実装していないだけで、HW可否の証拠にならない。
;      唯一決定的なのは実 X1turboZ でこのプローブを走らせること。
;
; ■ 重要: このプローブは xmil(Mac native / PPSSPP)では正しく表示されない。
;   どちらも同じ xmil モデルで、15kHz+width40+AEN は未実装 SCRN64 に落ちて
;   真っ黒/不定になる(= それ自体が「エミュが実装していない空白」の確認)。
;   判定は実 X1turboZ 実機でのみ有効。
;
; ■ 実機での読み取り方 (8本の縦カラーバー)
;   アナログパレットには index0..7 に "中間輝度を含む色"(例: 半分の青 0x0..8,
;   橙など)を設定する。デジタルパレットには別の "全輝度8色" を事前設定する。
;     - 中間色が出る         → アナログパレットが 15kHz/8色で有効 (= 実装可)
;     - 全輝度8色(デジタル)  → アナログは無視されデジタルにフォールバック (= 無効)
;     - パックされた砂嵐      → HW が VRAM を 4096パックとして読む (= 8色は不可)
;
; ■ 既知の罠: turboZ(ROM_TYPE=3)疑似IPL ではパレット既定が黒。実機では IPLROM が
;   組むが、本プローブはデジタル/アナログ両方を自前設定するので黒で隠れない。
;
; ■ CRTC について: X1 の電源/ローダ既定は 15kHz 40桁 320x200。本プローブは
;   その標準モードを前提に SCRN_BITS で 200line グラフィックを選ぶのみ(CRTC 18
;   レジスタは触らない = 既定タイミングを流用)。24kHz へ切り替えないので
;   デフォルト CRTC のままで整合する。

	DEVICE	NOSLOT64K
	ORG	0x0100

PORT_SCRN	EQU	0x1FD0		; SCRN_BITS
PORT_EXTPAL	EQU	0x1FB0		; EXTPALMODE (bit7=AEN, bit4=C64/64色)
PORT_EXTTDISP	EQU	0x1FC0		; ZPRY (64色x2 制御)
PORT_EXTGPAL	EQU	0x1FC5		; EXTGRPHPAL (bit7=APEN, bit3=APRD)
PORT_PPIC	EQU	0x1A02		; PPI port C (bit6: 1=width40 0=width80)

; SCRN_BITS: bit0=24kHz(0=15kHz), bit1=200line(1), bit3=DISPVRAM, bit4=ACCESSVRAM
SCRN_15K_200	EQU	0x02		; 15kHz + 200line + bank0

ROWCOLS		EQU	40		; 40桁
BARCOLS		EQU	5		; 8色 × 5桁 = 40桁

start:
	di

	; --- width40 を確定: PPI portC bit6 を 0->1 (width80->width40 edge) ---
	ld	bc, PORT_PPIC
	xor	a			; bit6=0 -> width80
	out	(c), a
	ld	a, 0x40			; bit6=1 -> width40
	out	(c), a

	; --- SCRN_BITS = 15kHz + 200line (bank0) ---
	ld	bc, PORT_SCRN
	ld	a, SCRN_15K_200
	out	(c), a

	; ============================================================
	; (1) まず AEN オフのままデジタルパレットを「全輝度8色」に設定
	;     (アナログ無視時のフォールバック表示を明確にするため)
	;     デジタル経路: port 0x1000/0x1100/0x1200 = rgbp[0/1/2]
	;     各レジスタ bit k = 色index k のそのチャネル成分。
	;     0xAA/0xCC/0xF0 で index0..7 が 8 種の全輝度色になる。
	; ============================================================
	ld	bc, 0x1000
	ld	a, 0xAA
	out	(c), a			; rgbp[0]
	ld	bc, 0x1100
	ld	a, 0xCC
	out	(c), a			; rgbp[1]
	ld	bc, 0x1200
	ld	a, 0xF0
	out	(c), a			; rgbp[2]

	; ============================================================
	; (2) アナログパレット有効化 + スーパーカラー系ビットは明示クリア
	;     EXTPALMODE = 0x80 : AEN(bit7)=1, C64/64色(bit4)=0
	;     ZPRY       = 0x00 : 64色x2 無効
	;     EXTGRPHPAL = 0x80 : (val&0x88)==0x80 で 8色パレット書込許可, APRD=0
	; ============================================================
	ld	bc, PORT_EXTTDISP
	xor	a
	out	(c), a			; ZPRY=0
	ld	bc, PORT_EXTPAL
	ld	a, 0x80
	out	(c), a			; AEN=1, 64色bit=0
	ld	bc, PORT_EXTGPAL
	ld	a, 0x80
	out	(c), a			; APEN=1, APRD=0

	; ============================================================
	; (3) アナログ 8色パレット (index0..7) を 4bit/ch で設定。
	;     8色モード書込の index bit: b2=(port.b7) b1=(port.b3) b0=(value.b7)
	;     チャネルは port_hi: 0x10=B 0x11=R 0x12=G。value 下位4bit=輝度(×0x11)。
	;     中間輝度(0x8)を混ぜ、通常X1(全/無のみ)では不可能な色にする。
	; ============================================================
	ld	hl, paltbl
palloop:
	ld	a, (hl)			; port high (0x10/0x11/0x12) / 0=終端
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

	; ============================================================
	; (4) GRAM(bank0, 3プレーン)に 8色 × 5桁 の縦カラーバーを描く。
	;     1行=40セル連続。plane: B=0x4000 R=0x8000 G=0xC000, 各セル 0xFF/0x00。
	; ============================================================
	ld	de, 0x0000		; de = セルアドレス
	xor	a
	ld	(kval), a
	ld	a, ROWCOLS
	ld	(colrem), a
	ld	a, BARCOLS
	ld	(barrem), a
gfill:
	ld	a, (kval)
	ld	l, a			; l = k (0..7)

	; B プレーン (0x4000|addr), k.bit0
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

	; R プレーン (0x8000|addr), k.bit1
	ld	a, d
	or	0x80
	ld	b, a
	ld	a, l
	and	2
	jr	z, rzero
	ld	a, 0xFF
rzero:
	out	(c), a

	; G プレーン (0xC000|addr), k.bit2
	ld	a, d
	or	0xC0
	ld	b, a
	ld	a, l
	and	4
	jr	z, gzero
	ld	a, 0xFF
gzero:
	out	(c), a

	; バー境界: 5桁ごとに k=(k+1)&7
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

	; 行境界: 40桁ごとに k=0
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

; --- RAM 作業変数 ---
kval:	db	0
colrem:	db	0
barrem:	db	0

; --- アナログ 8色パレットテーブル [port_hi, port_lo, value]*、port_hi=0 終端 ---
; index bit: b2=port_lo.b7, b1=port_lo.b3, b0=value.b7。value 下位4bit=輝度。
; 中間色(0x8)を混ぜる:
;   k0 半青(B=8)   k1 青F    k2 赤F    k3 緑F
;   k4 青F+赤8中間 k5 赤F+緑8中間 k6 緑F+青8中間 k7 灰(各8=中間輝度の白)
paltbl:
	; k0 (b2=0,b1=0,b0=0 -> plo=0x00, val.b7=0): B=8(中間) R=0 G=0
	db	0x10, 0x00, 0x08	; B=0x8
	db	0x11, 0x00, 0x00	; R=0
	db	0x12, 0x00, 0x00	; G=0
	; k1 (0,0,1 -> plo=0x00, val.b7=0x80): B=F
	db	0x10, 0x00, 0x8F
	db	0x11, 0x00, 0x80
	db	0x12, 0x00, 0x80
	; k2 (0,1,0 -> plo=0x08, val.b7=0): R=F
	db	0x10, 0x08, 0x00
	db	0x11, 0x08, 0x0F
	db	0x12, 0x08, 0x00
	; k3 (0,1,1 -> plo=0x08, val.b7=0x80): G=F
	db	0x10, 0x08, 0x80
	db	0x11, 0x08, 0x80
	db	0x12, 0x08, 0x8F
	; k4 (1,0,0 -> plo=0x80, val.b7=0): B=F R=8(中間)
	db	0x10, 0x80, 0x0F
	db	0x11, 0x80, 0x08
	db	0x12, 0x80, 0x00
	; k5 (1,0,1 -> plo=0x80, val.b7=0x80): R=F G=8(中間)
	db	0x10, 0x80, 0x80
	db	0x11, 0x80, 0x8F
	db	0x12, 0x80, 0x88
	; k6 (1,1,0 -> plo=0x88, val.b7=0): G=F B=8(中間)
	db	0x10, 0x88, 0x08
	db	0x11, 0x88, 0x00
	db	0x12, 0x88, 0x0F
	; k7 (1,1,1 -> plo=0x88, val.b7=0x80): 各8 = 中間輝度の灰
	db	0x10, 0x88, 0x88
	db	0x11, 0x88, 0x88
	db	0x12, 0x88, 0x88
	db	0x00			; 終端

	END
