; X1turboZ テックデモ (pxmil) — PCG タイル地形 + turboZ 色 + VBLANK 同期
;
; 目的: 8MHz でも背景が汚れず・滑らかで・turboZ のリッチな色が出せることを、
;       新規設計で実証する。移植版 Xevious が 8MHz で PCG を beam-racing して
;       汚れた問題を、PCG 更新を VBLANK 期間に集約することで回避する。
;
; マイルストーン M1 (本ファイル現状): PCG タイル地形の「静止表示」。
;   - turboZ アナログ描画が効く唯一の経路 24kHz+width80 (PAL_HIGHRESO)
;   - PCG でタイルパターンを定義、テキスト VRAM をタイルマップにして地形を描く
;   - turboZ テキストパレット (exttextpal, 64色中間色) でタイルを着色
;   - メインループは VBLANK エッジ待ち (PPI port B 0x1A01 bit7=DISP)
;
; 検証済みのエミュ事実 ([[turboz-state]]):
;   - RT=3 は自前でパレットを組む必要がある (既定パレット黒)
;   - SCRN64 専用ビットマップは未実装。アナログ色は 24kHz+width80 のみ描画される

	DEVICE	NOSLOT64K
	ORG	0x0100

; --- I/O ポート ---
PORT_SCRN	EQU	0x1FD0		; SCRN_BITS
PORT_EXTPAL	EQU	0x1FB0		; EXTPALMODE (bit7=アナログ有効)
PORT_EXTGPAL	EQU	0x1FC5		; EXTGRPHPAL
PORT_TXTPAL	EQU	0x1FB9		; テキストパレット slot1 (0x1FB9..0x1FBF = slot1..7)
PORT_PPIC	EQU	0x1A02		; PPI port C (bit6: 1=width40 0=width80)
PORT_PPIB	EQU	0x1A01		; PPI port B (bit7=DISP, bit2=VSYNC)
PORT_ANK	EQU	0x3000		; テキスト ANK (0x3000|addr, addr 0..0x7FF)
PORT_ATR	EQU	0x2000		; テキスト ATR (0x2000|addr)
PORT_PCG_B	EQU	0x1500		; PCG B プレーン (port|(line<<1))
PORT_PCG_R	EQU	0x1600		; PCG R プレーン
PORT_PCG_G	EQU	0x1700		; PCG G プレーン

; --- SCRN_BITS ビット ---
SCRN_24KHZ	EQU	0x01
SCRN_PCGMODE	EQU	0x20

; --- 画面諸元 (CRTC HDISP=0x28=40桁, 200line/8=25行) ---
COLS		EQU	40
ROWS		EQU	25
SKYROWS		EQU	16		; 上 16 行は空
PCG_DEFCELL	EQU	0x07FF		; SCRN_PCGMODE 時の PCG 定義セル

; --- タイルコード ---
TILE_GRASS	EQU	0x80
TILE_ROCK	EQU	0x81
TILE_DIRT	EQU	0x82

; --- タイル属性 (PCG=0x20 + 使用プレーン enable bits: B=1 R=2 G=4) ---
; 色は使用プレーンの組合せ (index 0..7) で pals.text[index] を引く
ATR_GRASS	EQU	0x20 | 0x05	; B+G 使用 (index4=G, index5=G+B)
ATR_ROCK	EQU	0x20 | 0x07	; B+R+G 使用
ATR_DIRT	EQU	0x20 | 0x06	; R+G 使用 (index6=R+G)

;=====================================================================
start:
	di

	; --- width 80 桁: PPI portC bit6 を 1->0 ---
	ld	bc, PORT_PPIC
	ld	a, 0x40
	out	(c), a
	xor	a
	out	(c), a

	; --- SCRN_BITS = 24kHz + PCGMODE ---
	ld	bc, PORT_SCRN
	ld	a, SCRN_24KHZ | SCRN_PCGMODE
	out	(c), a

	; --- アナログパレット有効 + extgrphpal ---
	ld	bc, PORT_EXTPAL
	ld	a, 0x80
	out	(c), a
	ld	bc, PORT_EXTGPAL
	ld	a, 0x80
	out	(c), a

	; --- テキストパレット (slot1..7) を turboZ 色でプログラム ---
	;     値 6bit: bit0-1=B, bit2-3=R, bit4-5=G (各 *0x55 → 0/0x55/0xAA/0xFF)
	ld	hl, txtpal
	ld	bc, PORT_TXTPAL		; 0x1FB9 (slot1)
tploop:
	ld	a, (hl)
	cp	0xFF			; 終端
	jr	z, tpdone
	out	(c), a
	inc	hl
	inc	c			; slot2..7 (0x1FBA..0x1FBF)
	jr	tploop
tpdone:

	; --- PCG タイルを定義 ---
	ld	hl, pcgdata
pcgloop:
	ld	a, (hl)			; タイルコード / 0 で終端
	or	a
	jr	z, pcgdone
	inc	hl
	call	define_pcg		; hl=24バイト(B8,R8,G8) を消費, a=code
	jr	pcgloop
pcgdone:

	; --- タイルマップを描く (全VRAMに2D地形, スクロール用) ---
	call	draw_terrain

	; scroll pos 初期化
	ld	hl, 0
	ld	(scrollpos), hl

;=====================================================================
; メインループ: 毎フレーム VBLANK 期間に CRTC 表示開始(POS)を 1 行進めて
; 縦スクロール。更新を VBLANK に集約することで active display 中の
; 書き換え(=汚れ)を避ける。
mainloop:
	call	wait_vblank
	; --- ここから VBLANK 期間 (active display 外) ---
	ld	hl, (scrollpos)
	ld	de, COLS		; 1 行 = 40 セル進める (縦に 8px スクロール)
	add	hl, de
	; ラップは 51 行 = 2040 (0x7F8) で。40 の倍数を保ち行境界のずれを防ぐ
	ld	de, 2040
	or	a			; clear carry
	sbc	hl, de			; hl -= 2040
	jr	nc, sp_store		; >=0 ならラップ後の値
	add	hl, de			; <2040 なら戻す
sp_store:
	ld	(scrollpos), hl
	; CRTC POSL(reg13)=L, POSH(reg12)=H へ
	ld	bc, 0x1800
	ld	a, 13
	out	(c), a			; regnum = POSL
	inc	c			; 0x1801
	ld	a, l
	out	(c), a			; POSL = scrollpos & 0xFF
	ld	bc, 0x1800
	ld	a, 12
	out	(c), a			; regnum = POSH
	inc	c
	ld	a, h
	out	(c), a			; POSH = (scrollpos>>8)&7
	jr	mainloop

;=====================================================================
; VBLANK 待ち: DISP(bit7) が 1->0 になるエッジまで待つ
; まず DISP=1 の間ループ、次に DISP=0 を確認して戻る
wait_vblank:
	ld	bc, PORT_PPIB
wv_active:
	in	a, (c)			; A = port B
	add	a, a			; bit7 -> carry
	jr	c, wv_active		; DISP=1 の間待つ
	ret				; DISP=0 (VBLANK 突入)

;=====================================================================
; PCG 定義: a=タイルコード, hl -> 24バイト (B×8, R×8, G×8)
; SCRN_PCGMODE 中なので定義セル 0x7FF の ANK にコードを置く
define_pcg:
	push	af
	; 定義セル 0x7FF に コードを書き、PCG 属性を立てる (pcg_offset が 0x7FF を選ぶ)
	ld	bc, PORT_ANK | (PCG_DEFCELL & 0xFF)	; 0x37FF
	ld	b, (PORT_ANK >> 8) | (PCG_DEFCELL >> 8)	; B=0x37
	out	(c), a			; ANK[0x7FF] = code
	ld	bc, PORT_ATR | (PCG_DEFCELL & 0xFF)
	ld	b, (PORT_ATR >> 8) | (PCG_DEFCELL >> 8)	; 0x27FF
	ld	a, 0x20
	out	(c), a			; ATR[0x7FF] = PCG bit
	; B プレーン 8 ライン
	ld	d, PORT_PCG_B >> 8
	call	def_plane
	; R プレーン
	ld	d, PORT_PCG_R >> 8
	call	def_plane
	; G プレーン
	ld	d, PORT_PCG_G >> 8
	call	def_plane
	pop	af
	ret

; d = プレーンポート上位, hl -> 8バイト。port = (d<<8) | (line<<1)
def_plane:
	ld	e, 0			; line counter 0..7
dp_loop:
	ld	a, e
	add	a, a			; line<<1
	ld	c, a
	ld	b, d
	ld	a, (hl)
	out	(c), a
	inc	hl
	inc	e
	ld	a, e
	cp	8
	jr	nz, dp_loop
	ret

;=====================================================================
; 地形タイルマップを描く (全 VRAM 2048 セル, 縦スクロール用の 2D 地形)
;   base=土、4行ごとに草の横ライン、(row*3+col)&7<2 に岩を散らす
; 行/列カウンタで addr を進める (除算なし)。hl=addr, 行=d, 列カウンタ=別途
draw_terrain:
	ld	hl, 0			; addr
	xor	a
	ld	(dt_row), a		; row 0..51
dt_rowloop:
	ld	b, COLS			; col カウンタ (40)
	xor	a
	ld	(dt_col), a
dt_cell:
	; --- このセルのタイルを決める ---
	ld	a, (dt_row)
	and	0x03
	jr	nz, dt_notgrass
	; 草の横ライン
	ld	d, TILE_GRASS
	ld	e, ATR_GRASS
	jr	dt_put
dt_notgrass:
	; 岩判定: ((row*3 + col) & 7) < 2
	ld	a, (dt_row)
	add	a, a			; row*2
	ld	c, a
	ld	a, (dt_row)
	add	a, c			; row*3
	ld	c, a
	ld	a, (dt_col)
	add	a, c			; row*3 + col
	and	0x07
	cp	2
	jr	nc, dt_dirt
	ld	d, TILE_ROCK
	ld	e, ATR_ROCK
	jr	dt_put
dt_dirt:
	ld	d, TILE_DIRT
	ld	e, ATR_DIRT
dt_put:
	; ANK[addr]=d, ATR[addr]=e  (port = 0x3000|addr / 0x2000|addr)
	push	bc
	ld	a, h
	or	0x30
	ld	b, a
	ld	c, l
	out	(c), d			; ANK
	ld	a, h
	or	0x20
	ld	b, a
	out	(c), e			; ATR
	pop	bc
	inc	hl
	; col++
	ld	a, (dt_col)
	inc	a
	ld	(dt_col), a
	djnz	dt_cell
	; row++
	ld	a, (dt_row)
	inc	a
	ld	(dt_row), a
	; addr < 0x800 の間続ける (hl の bit11 で判定)
	ld	a, h
	cp	0x08
	jr	nz, dt_rowloop
	ret

;=====================================================================
; データ
dt_row:		db	0
dt_col:		db	0
scrollpos:	dw	0

; テキストパレット slot1..7 (6bit: B=bit0-1, R=bit2-3, G=bit4-5)
txtpal:
	db	0x01			; slot1: B=1 暗い青 (0,0,0x55)
	db	0x30			; slot2: G=3 明緑 (0,0xFF,0) ... G=bit4-5=3→0xFF
	db	0x2C			; slot3: R=3,G=2 → 橙(0xFF,0xAA,0) 中間
	db	0x14			; slot4: G=1,R=1 → (0,0x55,0x55)暗い黄緑...
	db	0x3F			; slot5: 白
	db	0x18			; slot6: R=2,G=1 → 茶(0,0x55,0xAA)? R=bit2-3=2→0xAA
	db	0x2A			; slot7: B=2,R=2,G=2 → 灰 (0xAA,0xAA,0xAA)
	db	0xFF			; 終端

; PCG タイル定義: code, B×8, R×8, G×8
pcgdata:
	; --- TILE_GRASS (0x80): 草地。G=本体, B=上端ディザ ---
	db	TILE_GRASS
	db	0xAA,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; B: 上端にディザ
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; R: なし
	db	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF	; G: 全面緑
	; --- TILE_ROCK (0x81): 岩。市松+縁 ---
	db	TILE_ROCK
	db	0xFF,0x81,0xBD,0xA5,0xA5,0xBD,0x81,0xFF	; B
	db	0xFF,0x81,0xBD,0xA5,0xA5,0xBD,0x81,0xFF	; R
	db	0xFF,0x81,0xBD,0xA5,0xA5,0xBD,0x81,0xFF	; G
	; --- TILE_DIRT (0x82): 土。R+G でレンガ模様 (目地=黒) ---
	db	TILE_DIRT
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; B
	db	0xFF,0xFF,0xEF,0xFF,0xFF,0xFE,0xFF,0xFF	; R: 横目地
	db	0xFF,0xFF,0xEF,0xFF,0xFF,0xFE,0xFF,0xFF	; G
	db	0x00				; 終端

	END
