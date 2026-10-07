; emmscroll.asm — X1turbo EMM 2ドット単位スムーズ横スクロールデモ (pxmil)
;
; 原典: X1turbo 40周年 R-TYPE 風デモ
;   https://x1turbo-agency.hatenablog.jp/entry/2024/10/05/035915
;
; 手法 (2段階スクロール):
;   粗 (8px) = CRTC 表示開始アドレス POS(reg12/13) を +1
;   細 (2px) = 0/2/4/6 px プリシフト済み 4 ページを毎フレーム切替
;   pg0/1=bank0, pg2/3=bank1。各バンク内は POS を +0 / +1024 で 2 窓。
;
;   pg0: bank0 POS=p        shift 0px
;   pg1: bank0 POS=p+1024   shift 2px
;   pg2: bank1 POS=p        shift 4px
;   pg3: bank1 POS=p+1024   shift 6px
;
; GRAM pitch=40セル(余白なし)。POS+1 で全行が 8px 左シフト、右端1列だけ
; 新データが必要。4ページそれぞれ毎ステップ右端1列(25セル, stride40)を
; 再描画する (STRIP 再描画)。縦ストライプ背景のため全セル同バイト。
;
; GRAM I/O: B=0x4000 R=0x8000 G=0xC000, port=base+cell+raster*0x800
;
; ビルド:
;   ~/.local/bin/sjasmplus --nologo --raw=emmscroll.bin tools/emmscroll.asm
;   python3 tools/mkx1disk.py emmscroll.bin -o roms/EMMSCRL.2d -n EMMSCRL
;
; === 現状 ===
;   M1/M2: 4ページ切替+CRTC POS で全画面2ドット無限スムーズ横スクロール (CPU再描画)
;   M4: 毎フレームの空き時間 (アクティブ表示中の idle ループ回数) を計測。
;       idle ループ = 約41 T-state/回。4MHz idle≈501 (≈20.5k cyc, フレーム
;       66666 の約31%が空き)、8MHz idle≈2152 (≈88k cyc, フレーム133333 の約66%)。
;       生の回数比は 2倍クロック分を含むので、実時間の空き時間比は約2.1倍。
;       4MHz では600バイトの列再描画が VBLANK に収まらずアクティブ表示に食い込む
;       (書込み先は非表示ページなので乱れない)。8MHz では VBLANK 内に収まる。
;       probe ポート 0x00FE/0x00FF (エミュ XMIL_PROBE 時に stderr 出力) で取得。
;       -DM4_DISPLAY で画面左上に16進4桁オンスクリーン表示 (任意)。
;   8MHz 切替: エミュ環境変数 XMIL_CYCMUL=128 (256=4MHz)。
;   M5: 背景をタイルID方式でエリア1全長(256列=実ゲーム周期)に。継ぎ目なし。
;       tools/xevi2x1.py が「タイルID列(256列x25x2B, ID=タイル絶対RAMアドレス)」と
;       「ユニークタイル表(304個x24B, X1 8色 hp2網点, 8x8)」を生成し両方を incbin。
;       tiletbl は 0x0103 固定(= jp 直後, xevi2x1 の --tilebase と一致)。
;       起動: fill_emm_map が ID列を EMM へ転送, build_tables が shl/shr
;       ルックアップ表(4ページ)を 0xC000〜に構築。
;       毎フレーム: 新規列 wc と wc+1 の ID列(各50B)を DMA で EMM→RAM(IDBUFA/B),
;       各タイルを TBUF[i]=shl[tileA[i]]|shr[tileB[i]] で展開時シフト合成し GRAM へ。
;       2px(偶数)シフト + 横周期2網点 で実機チラつき無し。
;       縦1列は GRAM 不連続のため CPU で展開(記事の「1列ループはCPUで転送」)。
;       データ量 約20KB(ID列12.8KB+タイル表7.3KB)で 1本の bin に埋め込み。
;
; ビルド前にタイルデータを生成すること (roms/ は非コミット):
;   python3 tools/xevi2x1.py --area 1 --emit --tilebase 0x103

	DEVICE	NOSLOT64K
	ORG	0x0100

GRAM_B		EQU	0x4000
GRAM_R		EQU	0x8000
GRAM_G		EQU	0xC000
CRTC_REG	EQU	0x1800
CRTC_VAL	EQU	0x1801
PAL_B		EQU	0x1000
PAL_R		EQU	0x1100
PAL_G		EQU	0x1200
PAL_PLY		EQU	0x1300
PORT_SCRN	EQU	0x1FD0		; bit3=表示bank bit4=accessbank bit0=24kHz
SCRN_PCG	EQU	0x20		; PCG モード (0x1FD0 bit5)
PORT_PPIB	EQU	0x1A01		; bit7=DISP (VBLANK判定)
TVRAM		EQU	0x3000
TATTR		EQU	0x2000
PCG_B		EQU	0x1500		; PCG 定義 B プレーン (|(line<<1))
PCG_R		EQU	0x1600		; R
PCG_G		EQU	0x1700		; G
PCG_DEFCELL	EQU	0x07FF		; PCG 定義に使うセル
; 自機 PCG。2px スムーズ横移動のため 4 シフト版(0/2/4/6px) を持つ。
;   各版 = 3列x2行 = 6セル。コード = SHIP_BASE + v*6 + row*3 + col。
;   版 v の画面横オフセット = +2v px。計 4*6 = 24 セル (0x80..0x97)。
SHIP_BASE	EQU	0x80		; 自機 PCG コード先頭
SHIP_VSTRIDE	EQU	6		; 1版 = 6セル
SHIP_ATR	EQU	0x20 | 0x02	; PCG + R プレーンのみ (赤, 地形に少なく視認性高)
;   ↑ 色付きスプライトに差し替える際は 0x20|0x07 (B/R/G 全プレーン) にする。
SHIP_HXMAX	EQU	151		; ship_hx 上限 = 37*4+3 (col2 <= 39, 行内に収まる)
MOVE_DELAY	EQU	3		; 縦移動の間隔(フレーム)。横は毎フレーム(2px/フレーム)
SHIPGEN		EQU	0xC8A0		; シフト版生成バッファ 72バイト (3列x24, IDBUF後)
; ジョイスティック (PSG reg 0x0e, 負論理)
PORT_PSGREG	EQU	0x1C00		; レジスタ選択
PORT_PSGDAT	EQU	0x1B00		; 読み (sndboard_psgsta)

COLS		EQU	40
ROWS		EQU	25
OFF1024		EQU	1024		; バンク内 2 窓目のオフセット

; --- EMM / タイルID方式 ---
EMM_A0		EQU	0x0D00		; アドレス 下位
EMM_A1		EQU	0x0D01		; 中位
EMM_A2		EQU	0x0D02		; 上位
EMM_DAT		EQU	0x0D03		; データ (R/W でアドレス自動+1)
W_CELLS		EQU	256		; エリア1周期 (列)。wc = ... & 0xFF
TILEBYTES	EQU	24		; 1タイル = 8x8 を 8ラスタ x 3プレーン (B8,R8,G8)
IDBYTES		EQU	2		; タイルID = タイル表の絶対RAMアドレス(LE16)
COLIDS		EQU	ROWS*IDBYTES	; 1列の ID 列 = 25*2 = 50 バイト
TILEBASE	EQU	0x0103		; tiletbl の配置(= jp の直後)。xevi2x1 と一致
; 高位 RAM のバッファ/テーブル (bin には含めない, 起動時構築/転送)
SHLTAB		EQU	0xC000		; a<<(2*np) ルックアップ 4ページ (page=0xC0+np)
SHRTAB		EQU	0xC400		; a>>(8-2*np) ルックアップ 4ページ (page=0xC4+np)
TBUF		EQU	0xC800		; 1タイル合成 24バイト
IDBUFA		EQU	0xC820		; wc 列の ID 50バイト
IDBUFB		EQU	0xC860		; (wc+1) 列の ID 50バイト

;=====================================================================
start:				; exec = 0x0100
	jp	realstart

; --- 埋め込みデータ (疑似IPLが RAM に連続ロード) ---
tiletbl:			; 0x0103 固定 (= TILEBASE, xevi2x1 の tilebase と一致)
	incbin	"roms/xtiles.bin"	; ユニークタイル表 (24B/タイル, X1 8色 hp2網点)
xmapdata:
	incbin	"roms/xtilemap.bin"	; タイルID列 256列x25x2B (ID=タイル絶対アドレス)

realstart:
	di
	ld	sp, 0xF000

	call	init_screen
	call	clear_tvram

	; --- タイルID列を EMM へ転送, シフトルックアップ表を構築 ---
	call	fill_emm_map
	call	build_tables

	IFDEF	SHIP
	; --- PCG モードを有効にして自機(オリジナル機体)を定義 [M6, 開発中] ---
	ld	bc, PORT_SCRN
	ld	a, SCRN_PCG		; bank0 + PCG
	out	(c), a
	call	gen_ship
	ld	a, 18 * 4		; ship_hx = 列18, 位相0 (2px単位)
	ld	(ship_hx), a
	ld	a, 12
	ld	(ship_row), a
	ld	hl, 0xFFFF
	ld	(ship_prev), hl
	ld	(ship_prev2), hl
	ENDIF

	IFNDEF	DBG_STATIC
	; --- 初期プリフィル: pg0..3 の表示窓(列0..39)を埋める ---
	call	prefill
	ENDIF

	IFDEF	DBG_SHIP
	; スクロール無し・page0・POS=0 で自機を ship_hx=DBG_SHIPHX に置き halt。
	;   DBG_SHIPHX 未指定なら 72(列18位相0)。xmin が hx+1 ごと +4(2px)を検証。
	IFNDEF	DBG_SHIPHX
	DEFINE	DBG_SHIPHX 72
	ENDIF
	ld	bc, PORT_SCRN
	ld	a, SCRN_PCG		; bank0 + PCG
	out	(c), a
	ld	bc, CRTC_REG
	ld	a, 13
	out	(c), a
	inc	c
	xor	a
	out	(c), a			; POSL=0
	ld	bc, CRTC_REG
	ld	a, 12
	out	(c), a
	inc	c
	xor	a
	out	(c), a			; POSH=0
	ld	a, DBG_SHIPHX
	ld	(ship_hx), a
	ld	a, 12
	ld	(ship_row), a
	ld	hl, 0
	ld	(cur_pos), hl
	ld	hl, 0xFFFF
	ld	(ship_prev), hl
	call	overlay_ship
.shiphalt:
	jr	.shiphalt
	ENDIF

	IFDEF	DBG_POS
	; prefill 済み。page0(bank0) を POS=10 で表示して halt。
	; POS が効けばバーが左に 10 cell シフトする。
	ld	bc, PORT_SCRN
	xor	a
	out	(c), a			; disp/access bank0
	ld	bc, CRTC_REG
	ld	a, 13
	out	(c), a
	inc	c
	ld	a, 10
	out	(c), a			; POSL=10
	ld	bc, CRTC_REG
	ld	a, 12
	out	(c), a
	inc	c
	xor	a
	out	(c), a			; POSH=0
.posh:	jr	.posh
	ENDIF

	IFDEF	DBG_SCROLL0
	; prefill 済み。page0(bank0) のみ、毎フレーム POS=coarse で 8px スクロール。
	ld	bc, PORT_SCRN
	xor	a
	out	(c), a			; bank0
	ld	hl, 0
	ld	(coarse), hl
.s0loop:
	IFDEF	DBG_NOWAIT
	ld	de, 0
.dly:	dec	de
	ld	a, d
	or	e
	jr	nz, .dly		; 65536 回ディレイ
	ELSE
	call	wait_vblank
	ENDIF
	ld	hl, (coarse)
	inc	hl
	ld	a, h
	and	0x07
	ld	h, a
	ld	(coarse), hl
	; POS = coarse
	ld	bc, CRTC_REG
	ld	a, 13
	out	(c), a
	inc	c
	out	(c), l
	ld	bc, CRTC_REG
	ld	a, 12
	out	(c), a
	inc	c
	out	(c), h
	jr	.s0loop
	ENDIF
	IFDEF	DBG_STATIC
	; access bank0
	ld	bc, PORT_SCRN
	xor	a
	out	(c), a
	; col0 を白で描く: colB/R/G=0xFF, base=0x07D8 (-40)
	ld	a, 0xFF
	ld	(colB), a
	ld	(colR), a
	ld	(colG), a
	ld	hl, 0x07D8
	ld	(dc_base), hl
	call	draw_column
	; col5 を青(colB=0xFF,R/G=0)で: base=0x07D8+5
	ld	a, 0xFF
	ld	(colB), a
	xor	a
	ld	(colR), a
	ld	(colG), a
	ld	hl, 0x07D8 + 5
	ld	(dc_base), hl
	call	draw_column
	ENDIF

	; scroll 状態初期化
	ld	hl, 0
	ld	(coarse), hl
	ld	(framecnt), hl
	ld	(step), hl
	ld	hl, 0xFFFF
	ld	(minspare), hl
	xor	a
	ld	(substep), a
	; 最初の次ページ(np)列を準備 (prefill 済みの表示に続く展開のため)
	call	redraw_begin

	IFDEF	DBG_STATIC
	; --- デバッグ: page0 (bank0 POS=0) を静止表示して halt ---
	ld	bc, PORT_SCRN
	xor	a			; disp bank0, access bank0
	out	(c), a
	ld	bc, CRTC_REG
	ld	a, 13
	out	(c), a
	inc	c
	xor	a
	out	(c), a			; POSL=0
	ld	bc, CRTC_REG
	ld	a, 12
	out	(c), a
	inc	c
	xor	a
	out	(c), a			; POSH=0
.dbghalt:
	jr	.dbghalt
	ENDIF

;=====================================================================
; メインループ (処理落ち対策版)
;   step 単位でスクロール。step は speed_div フレームごとに +1 (速度調整)。
;   phase=step&3, coarse=step>>2。各ページは speed_div フレーム表示。
;   次ページ(np)の新規列展開を step の speed_div フレームに分散 (redraw_chunk)
;   して 1 フレームあたりの負荷を下げ、4MHz でも取りこぼし 0 を狙う。
;   wait_vblank はエッジ検出 + 取りこぼし計測 (misscnt)。
mainloop:
	call	wait_vblank
	; --- phase=step&3, coarse=step>>2 ---
	ld	hl, (step)
	ld	a, l
	and	3
	ld	(phase), a
	srl	h
	rr	l
	srl	h
	rr	l
	ld	(coarse), hl
	; --- 0x1FD0: disp=phase>>1, access=npage>>1 (展開先バンク) ---
	ld	a, (phase)
	srl	a
	rlca
	rlca
	rlca				; (phase>>1)<<3
	ld	d, a
	ld	a, (npage)
	srl	a
	rlca
	rlca
	rlca
	rlca				; (np>>1)<<4
	or	d
	IFDEF	SHIP
	or	SCRN_PCG		; PCG 表示には PCGMODE が必要 (状態差分で確認)
	ENDIF
	ld	bc, PORT_SCRN
	out	(c), a
	; --- POS = (coarse + (phase&1?1024:0)) & 0x7FF ---
	ld	hl, (coarse)
	ld	a, (phase)
	and	1
	jr	z, .showpos
	ld	bc, OFF1024
	add	hl, bc
.showpos:
	ld	a, h
	and	0x07
	ld	h, a
	IFDEF	SHIP
	ld	(cur_pos), hl
	ENDIF
	call	setpos
	; --- 次ページ列の展開を分散 (substep ぶん) ---
	IFNDEF	DBG_NOREDRAW
	call	redraw_chunk
	ENDIF
	IFDEF	SHIP
	call	move_ship		; ジョイスティックで自機移動
	call	overlay_ship
	ENDIF
	; --- 最小フレーム余裕(minspare)をプローブ出力 (XMIL_PROBE 時) ---
	;   余裕は wait_vblank で計測済み。minspare>0 なら全フレーム1フレーム内に収まる。
	ld	hl, (minspare)
	ld	(idlecnt), hl
	IFDEF	DBG_SHIPPOS
	ld	a, (ship_hx)
	ld	l, a
	ld	a, (ship_row)
	ld	h, a			; (row<<8)|ship_hx
	ENDIF
	ld	bc, 0x00FE
	out	(c), l
	ld	bc, 0x00FF
	out	(c), h
	IFDEF	M4_DISPLAY
	call	show_idle
	ENDIF
	; --- substep++ , speed_div で step++ & 次ページ準備 ---
	ld	a, (substep)
	inc	a
	ld	hl, speed_div
	cp	(hl)
	jr	c, .samestep
	xor	a
	ld	(substep), a
	ld	hl, (step)
	inc	hl
	ld	(step), hl
	call	redraw_begin
	jr	.fcinc
.samestep:
	ld	(substep), a
.fcinc:
	ld	hl, (framecnt)
	inc	hl
	ld	(framecnt), hl
	jp	mainloop

;=====================================================================
; DISP(bit7)=1 (アクティブ開始) になるまで待つ
wait_active:
	ld	bc, PORT_PPIB
.w:	in	a, (c)
	add	a, a
	jr	nc, .w			; DISP=0 の間待つ
	ret

;=====================================================================
; idlecnt(16bit) を 16進4桁で「現フレームの表示左上」に出す。
; テキスト VRAM も CRTC POS を共有するので、表示開始セル base を基準に書く。
;   base = (coarse + (phase&1?1024:0)) & 0x7FF   (= このフレームの表示 POS)
show_idle:
	; base 計算 -> si_base
	ld	hl, (coarse)
	ld	a, (phase)
	and	1
	jr	z, .nooff
	ld	bc, OFF1024
	add	hl, bc
.nooff:
	ld	a, h
	and	0x07
	ld	h, a
	ld	(si_base), hl		; base cell
	; --- 前フレームの数字セル (prev_base+0..3) のテキストを消す ---
	ld	hl, (prev_base)
	ld	d, 4
.clrtxt:
	ld	a, h
	and	0x07
	or	(TVRAM >> 8)
	ld	b, a
	ld	c, l
	xor	a
	out	(c), a			; ANK=0 (空白)
	inc	hl
	dec	d
	jr	nz, .clrtxt
	ld	hl, (si_base)
	ld	(prev_base), hl
	; --- 数字セル base+0..3 (row0) の GRAM を黒クリア (数字を読めるように) ---
	ld	d, 4			; 4 セル
	ld	hl, (si_base)
.clr:
	ld	a, h
	and	0x07
	push	hl			; cell 保存
	; B plane
	or	(GRAM_B >> 8)
	ld	b, a
	ld	c, l
	xor	a
	call	wcell8
	pop	hl
	push	hl
	ld	a, h
	and	0x07
	or	(GRAM_R >> 8)
	ld	b, a
	ld	c, l
	xor	a
	call	wcell8
	pop	hl
	push	hl
	ld	a, h
	and	0x07
	or	(GRAM_G >> 8)
	ld	b, a
	ld	c, l
	xor	a
	call	wcell8
	pop	hl
	inc	hl			; 次セル
	dec	d
	jr	nz, .clr
	; 4 ニブル: H上,H下,L上,L下 を base+0..3 に
	ld	a, 0			; digit index 0..3
	ld	(si_idx), a
	ld	hl, (idlecnt)
	ld	a, h
	rrca
	rrca
	rrca
	rrca
	call	si_nib			; H 上位
	ld	hl, (idlecnt)
	ld	a, h
	call	si_nib			; H 下位
	ld	hl, (idlecnt)
	ld	a, l
	rrca
	rrca
	rrca
	rrca
	call	si_nib			; L 上位
	ld	hl, (idlecnt)
	ld	a, l
	call	si_nib			; L 下位
	ret
; a の下位4bit -> 16進文字 -> (base+si_idx) の ANK, ATTR=白。si_idx++。
si_nib:
	and	0x0F
	add	a, 0x90
	daa
	adc	a, 0x40
	daa				; 0..9->'0'..'9', A..F->'A'..'F'
	ld	e, a			; e = 文字
	; cell = (si_base + si_idx) & 0x7FF
	ld	hl, (si_base)
	ld	a, (si_idx)
	ld	c, a
	ld	b, 0
	add	hl, bc
	ld	a, h
	and	0x07
	ld	h, a			; cell &0x7FF
	; ANK port = 0x3000 | cell
	ld	a, h
	or	(TVRAM >> 8)		; 0x30
	ld	b, a
	ld	c, l
	out	(c), e			; ANK = 文字
	ld	a, h
	or	(TATTR >> 8)		; 0x20
	ld	b, a
	ld	a, 0x07
	out	(c), a			; ATTR = 白
	; si_idx++
	ld	a, (si_idx)
	inc	a
	ld	(si_idx), a
	ret

;=====================================================================
; CRTC POS 設定: HL = 11bit 表示開始セル
setpos:
	ld	bc, CRTC_REG
	ld	a, 13
	out	(c), a
	inc	c
	out	(c), l			; POSL
	ld	bc, CRTC_REG
	ld	a, 12
	out	(c), a
	inc	c
	out	(c), h			; POSH
	ret

;=====================================================================
; step 変化時: 次ページ np=(step+1)&3 の新規列の準備 (ID読み+base+シフト表)。
;   展開自体は redraw_chunk で speed_div フレームに分散する。
redraw_begin:
	; np=(step+1)&3, coarsen=(step+1)>>2
	ld	hl, (step)
	inc	hl
	ld	a, l
	and	3
	ld	(npage), a
	srl	h
	rr	l
	srl	h
	rr	l
	ld	(coarsen), hl
	; wc=(coarsen+COLS-1)&0xFF, ID 読み
	ld	hl, (coarsen)
	ld	bc, COLS - 1
	add	hl, bc
	ld	a, l
	ld	(wcol), a
	ld	e, a
	ld	hl, IDBUFA
	call	read_ids
	ld	a, (wcol)
	inc	a
	ld	e, a
	ld	hl, IDBUFB
	call	read_ids
	; base=(coarsen+off_np-1)&0x7FF
	ld	hl, (coarsen)
	ld	a, (npage)
	and	1
	jr	z, .nooff
	ld	bc, OFF1024
	add	hl, bc
.nooff:
	dec	hl
	ld	a, h
	and	0x07
	ld	h, a
	ld	(dc_base), hl
	call	set_shiftpg
	xor	a
	ld	(ec_row), a		; 展開カーソルリセット
	ret

;=====================================================================
; 展開チャンク: target=((substep+1)*ROWS)/speed_div 行目まで展開。
;   speed_div フレームで全 ROWS(25) 行が完了する (最終 substep で target=25)。
redraw_chunk:
	; hl = (substep+1) * ROWS
	ld	a, (substep)
	inc	a
	ld	b, a
	ld	hl, 0
	ld	de, ROWS
.m:	add	hl, de
	djnz	.m
	; hl / speed_div -> b (quotient)
	ld	a, (speed_div)
	ld	e, a
	ld	d, 0
	ld	b, 0
.dv:
	ld	a, l
	sub	e
	ld	c, a
	ld	a, h
	sbc	d
	jr	c, .dvdone		; hl < speed_div
	ld	l, c
	ld	h, a
	inc	b
	jr	.dv
.dvdone:
	ld	a, b			; target 行数 (0..25)
	ld	(exp_target), a
.el:
	ld	a, (ec_row)
	ld	hl, exp_target
	cp	(hl)
	jr	nc, .eldone		; ec_row >= target
	call	expand_one_row
	jr	.el
.eldone:
	ret

; npage からシフト表のページ(shl_hi/shr_hi)を設定
set_shiftpg:
	ld	a, (npage)
	add	a, (SHLTAB >> 8)
	ld	(shl_hi), a
	ld	a, (npage)
	add	a, (SHRTAB >> 8)
	ld	(shr_hi), a
	ret

;=====================================================================
; ID 列読み込み: e=世界列(0..255), hl=転送先(IDBUFA/B)。
;   EMM アドレス = col*COLIDS(50) を設定し DMA で 50 バイト転送。
read_ids:
	ld	(dma_id_dst), hl
	ld	h, 0
	ld	l, e			; col
	ld	b, h
	ld	c, l			; bc = col*1
	add	hl, hl			; *2
	add	hl, hl			; *4
	add	hl, hl			; *8
	ld	d, h
	ld	e, l			; de = col*8
	add	hl, hl			; *16
	add	hl, de			; *24
	add	hl, bc			; *25
	add	hl, hl			; *50
	ld	a, l
	ld	(emm_a0), a
	ld	a, h
	ld	(emm_a1), a
	xor	a
	ld	(emm_a2), a
	call	set_emm_addr
	ld	hl, dma_id
	ld	bc, 0x1F80
	ld	e, dma_id_end - dma_id
.d:	ld	a, (hl)
	out	(c), a
	inc	hl
	dec	e
	jr	nz, .d
	ret

; emm_a0/a1/a2 -> EMM アドレスポート
set_emm_addr:
	ld	bc, EMM_A0
	ld	a, (emm_a0)
	out	(c), a
	inc	c
	ld	a, (emm_a1)
	out	(c), a
	inc	c
	ld	a, (emm_a2)
	out	(c), a
	ret

;=====================================================================
; 全 25 行を展開 (prefill 用)。ec_row をリセットして expand_one_row を 25 回。
expand_col_tiles:
	xor	a
	ld	(ec_row), a
	ld	b, ROWS
.rl:
	push	bc
	call	expand_one_row
	pop	bc
	djnz	.rl
	ret

; 1 行(ec_row)を展開し ec_row++。
;   IDBUFA[ec_row]=タイルA, IDBUFB[ec_row]=タイルB。
;   cell = (dc_base + 40*(ec_row+1)) & 0x7FF。
expand_one_row:
	ld	a, (ec_row)
	inc	a			; ec_row+1
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
	ld	de, (dc_base)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(ec_cell), hl
	ld	a, (ec_row)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, IDBUFA
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	(ec_ta), de		; tileA addr
	ld	a, (ec_row)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, IDBUFB
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	(ec_tb), de		; tileB addr
	call	compose_tile
	call	scatter_tile
	ld	a, (ec_row)
	inc	a
	ld	(ec_row), a
	ret

; 1タイル(24B)を合成: TBUF[i] = shl[tileA[i]] | shr[tileB[i]]
compose_tile:
	ld	de, (ec_ta)
	ld	ix, TBUF
	ld	b, TILEBYTES
.p1:
	ld	a, (de)
	inc	de
	ld	l, a
	ld	a, (shl_hi)
	ld	h, a
	ld	a, (hl)
	ld	(ix+0), a
	inc	ix
	djnz	.p1
	ld	de, (ec_tb)
	ld	ix, TBUF
	ld	b, TILEBYTES
.p2:
	ld	a, (de)
	inc	de
	ld	l, a
	ld	a, (shr_hi)
	ld	h, a
	ld	a, (hl)
	or	(ix+0)
	ld	(ix+0), a
	inc	ix
	djnz	.p2
	ret

; TBUF(24B: B8,R8,G8) を GRAM cell(ec_cell)の 3プレーン8ラスタへ
scatter_tile:
	ld	de, TBUF
	ld	hl, (ec_cell)
	ld	a, h
	or	(GRAM_B >> 8)
	ld	b, a
	ld	c, l
	call	wr8
	ld	hl, (ec_cell)
	ld	a, h
	or	(GRAM_R >> 8)
	ld	b, a
	ld	c, l
	call	wr8
	ld	hl, (ec_cell)
	ld	a, h
	or	(GRAM_G >> 8)
	ld	b, a
	ld	c, l
	call	wr8
	ret

; bc=port, de=src。8バイトを8ラスタに書き de+=8, b(上位)+=8/ラスタ。hl破壊。
wr8:
	ld	l, 8
.w:	ld	a, (de)
	inc	de
	out	(c), a
	ld	a, b
	add	a, 0x08
	ld	b, a
	dec	l
	jr	nz, .w
	ret

; bc=port(b=high c=low), a=data を 8 ラスタに書く (bc/de/hl 保持, a 破壊)
;   show_idle の黒クリア用。
wcell8:
	push	bc
	push	de
	ld	d, a
	ld	e, 8
.w:	out	(c), d
	ld	a, b
	add	a, 0x08
	ld	b, a
	dec	e
	jr	nz, .w
	pop	de
	pop	bc
	ret

;=====================================================================
; 初期プリフィル: coarse=0。各ページ np の表示窓(POS=off_np)の列 sc=0..39 に
;   世界列 sc のタイルを置く。base = off_np + sc - 40。
prefill:
	xor	a
	ld	(pf_page), a
.pl:
	ld	a, (pf_page)
	srl	a
	ld	c, a
	rlca
	rlca
	rlca				; bank<<3 (disp)
	ld	d, a
	ld	a, c
	rlca
	rlca
	rlca
	rlca				; bank<<4 (access)
	or	d
	ld	bc, PORT_SCRN
	out	(c), a
	ld	a, (pf_page)
	add	a, (SHLTAB >> 8)
	ld	(shl_hi), a
	ld	a, (pf_page)
	add	a, (SHRTAB >> 8)
	ld	(shr_hi), a
	ld	hl, 0
	ld	a, (pf_page)
	and	1
	jr	z, .b0
	ld	hl, OFF1024
.b0:
	ld	(pf_base), hl
	xor	a
	ld	(pf_wc), a
.wl:
	ld	a, (pf_wc)
	ld	e, a
	ld	hl, IDBUFA
	call	read_ids
	ld	a, (pf_wc)
	inc	a
	ld	e, a
	ld	hl, IDBUFB
	call	read_ids
	ld	hl, (pf_base)
	ld	a, (pf_wc)
	ld	c, a
	ld	b, 0
	add	hl, bc
	ld	bc, COLS
	or	a
	sbc	hl, bc
	ld	a, h
	and	0x07
	ld	h, a
	ld	(dc_base), hl
	call	expand_col_tiles
	ld	a, (pf_wc)
	inc	a
	ld	(pf_wc), a
	cp	COLS
	jr	nz, .wl
	ld	a, (pf_page)
	inc	a
	ld	(pf_page), a
	cp	4
	jp	nz, .pl
	ret

;=====================================================================
; タイルID列(xmapdata, RAM)を EMM アドレス 0 へ転送。
fill_emm_map:
	xor	a
	ld	(emm_a0), a
	ld	(emm_a1), a
	ld	(emm_a2), a
	call	set_emm_addr
	ld	hl, xmapdata
	ld	de, W_CELLS * COLIDS	; 12800
	ld	bc, EMM_DAT
.l:	ld	a, (hl)
	out	(c), a
	inc	hl
	dec	de
	ld	a, d
	or	e
	jr	nz, .l
	ret

;=====================================================================
; シフトルックアップ表を構築 (SHLTAB/SHRTAB, 各4ページ256B)。
;   shl[np][a] = (a << 2np) & 0xFF,  shr[np][a] = a >> (8-2np)。
build_tables:
	xor	a
	ld	(bt_np), a
.nl:
	ld	a, (bt_np)
	add	a, a			; 2np
	ld	(bt_shl), a
	ld	b, a
	ld	a, 8
	sub	b			; 8-2np
	ld	(bt_shr), a
	ld	a, (bt_np)
	add	a, (SHLTAB >> 8)
	ld	(bt_shlpg), a
	ld	a, (bt_np)
	add	a, (SHRTAB >> 8)
	ld	(bt_shrpg), a
	ld	c, 0			; byte value
.bl:
	; shl result = c << bt_shl
	ld	a, (bt_shl)
	ld	b, a
	inc	b
	ld	a, c
	jr	.slt
.sll:
	add	a, a
.slt:
	dec	b
	jr	nz, .sll
	ld	e, a
	ld	a, (bt_shlpg)
	ld	h, a
	ld	l, c
	ld	(hl), e			; shl[c]
	; shr result = c >> bt_shr
	ld	a, (bt_shr)
	ld	b, a
	inc	b
	ld	a, c
	jr	.srt
.srl:
	srl	a
.srt:
	dec	b
	jr	nz, .srl
	ld	e, a
	ld	a, (bt_shrpg)
	ld	h, a
	ld	l, c
	ld	(hl), e			; shr[c]
	inc	c
	jr	nz, .bl			; 256 バイト
	ld	a, (bt_np)
	inc	a
	ld	(bt_np), a
	cp	4
	jp	nz, .nl
	ret

;=====================================================================
; VBLANK 待ち: DISP(bit7) が 1 の間待ち、0 になったら戻る
; VBLANK 待ち (エッジ検出 + フレーム余裕計測)。
;   まず DISP=1(アクティブ)まで待ち、次に立下り(1->0)を待って VBLANK 先頭に同期。
;   この間の待ちループ回数 = このフレームの CPU 余裕。最小値を minspare に記録
;   (0 なら処理落ち=1フレームに収まっていない)。前フレームの処理が1フレームを
;   超えると待ちが短くなり minspare が下がる = 取りこぼし検出。
wait_vblank:
	ld	bc, PORT_PPIB
	ld	hl, 0			; 待ちカウンタ
.a:	in	a, (c)			; DISP=1 になるまで (VBLANK 中なら残りを待つ)
	add	a, a
	jr	c, .adone
	inc	hl
	jr	.a
.adone:
.b:	in	a, (c)			; DISP=0 (立下り=次VBLANK先頭) まで
	add	a, a
	jr	nc, .bdone
	inc	hl
	jr	.b
.bdone:
	; minspare = min(minspare, hl)
	ex	de, hl			; de = 今回の余裕
	ld	hl, (minspare)
	; if de < hl: minspare = de
	ld	a, l
	sub	e
	ld	a, h
	sbc	d
	jr	c, .keep		; hl < de -> 据え置き
	ld	(minspare), de
.keep:
	ret

;=====================================================================
; 画面初期化 (emmtest.asm と同じ: 15kHz 40桁 8色)
init_screen:
	ld	hl, crtc_tbl
	ld	d, 0
.crtc:	ld	bc, CRTC_REG
	out	(c), d
	inc	c
	ld	a, (hl)
	out	(c), a
	inc	hl
	inc	d
	ld	a, d
	cp	18
	jr	nz, .crtc
	ld	bc, PAL_B
	ld	a, 0xAA
	out	(c), a
	ld	b, PAL_R >> 8
	ld	a, 0xCC
	out	(c), a
	ld	b, PAL_G >> 8
	ld	a, 0xF0
	out	(c), a
	ld	b, PAL_PLY >> 8
	xor	a
	out	(c), a
	ret

;=====================================================================
; テキスト VRAM/ATTR クリア (グラフィックのみ見せる)
clear_tvram:
	ld	hl, 0
.c:	ld	a, h
	or	(TVRAM >> 8)
	ld	b, a
	ld	c, l
	xor	a
	out	(c), a			; ANK=0 (スペース)
	ld	a, h
	or	(TATTR >> 8)
	ld	b, a
	xor	a
	out	(c), a			; ATTR=0
	inc	hl
	ld	a, h
	cp	0x08
	jr	nz, .c
	ret

	IFDEF	SHIP
;=====================================================================
; [M6] 自機(オリジナル機体)の PCG を定義。base 16x16(shipdata) から
;   4 シフト版(0/2/4/6px) を 24bit 右シフトで生成し、各版 3列x2行を定義。
;   計 24 セル (0x80..0x97)。PCG モード有効時に呼ぶ。
gen_ship:
	xor	a
	ld	(gs_v), a		; v = 0..3
.vloop:
	xor	a
	ld	(gs_row), a		; row = 0,1
.rloop:
	call	gen_one_vr		; SHIPGEN に 3列ぶん生成
	call	def_vr_cells		; 3 セル定義
	ld	a, (gs_row)
	inc	a
	ld	(gs_row), a
	cp	2
	jr	nz, .rloop
	ld	a, (gs_v)
	inc	a
	ld	(gs_v), a
	cp	4
	jr	nz, .vloop
	ret

; (gs_v,gs_row) の 1行(16px)を位相 p=2v で右シフトし 3列(24px)を SHIPGEN へ。
;   SHIPGEN レイアウト: col0=[+0..23], col1=[+24..47], col2=[+48..71]
;   各 24 バイト = B8,R8,G8 (plane*8+raster)。
gen_one_vr:
	; src 左セル = shipdata + row*48, 右セル = +24
	ld	hl, shipdata
	ld	a, (gs_row)
	or	a
	jr	z, .r0
	ld	de, 48
	add	hl, de
.r0:	ld	(gs_lsrc), hl
	ld	de, 24
	add	hl, de
	ld	(gs_rsrc), hl
	ld	a, (gs_v)		; p = 2*v
	add	a, a
	ld	(gs_p), a
	xor	a
	ld	(gs_r), a		; idx 0..23 (= plane*8+raster)
.loop:
	ld	a, (gs_r)
	ld	e, a
	ld	d, 0
	ld	hl, (gs_lsrc)
	add	hl, de
	ld	b, (hl)			; b0 = 左バイト
	ld	hl, (gs_rsrc)
	add	hl, de
	ld	c, (hl)			; b1 = 右バイト
	ld	d, 0			; b2 = 0
	ld	a, (gs_p)
	or	a
	jr	z, .noshift
.sh:	srl	b			; [b0 b1 b2] 24bit 論理右シフト = 画面右へ
	rr	c
	rr	d
	dec	a
	jr	nz, .sh
.noshift:
	ld	a, c
	ld	(gs_c1), a
	ld	a, d
	ld	(gs_c2), a
	ld	hl, SHIPGEN
	ld	a, (gs_r)
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	(hl), b			; col0
	ld	de, 24
	add	hl, de
	ld	a, (gs_c1)
	ld	(hl), a			; col1
	add	hl, de
	ld	a, (gs_c2)
	ld	(hl), a			; col2
	ld	a, (gs_r)
	inc	a
	ld	(gs_r), a
	cp	24
	jr	nz, .loop
	ret

; SHIPGEN の 3列を PCG セルに定義。code = SHIP_BASE + v*6 + row*3 + col。
def_vr_cells:
	ld	a, (gs_v)
	add	a, a			; 2v
	ld	e, a
	add	a, a			; 4v
	add	a, e			; 6v
	ld	e, a
	ld	a, (gs_row)
	ld	d, a
	add	a, a			; 2row
	add	a, d			; 3row
	add	a, e			; 6v + 3row
	add	a, SHIP_BASE
	ld	(gs_code), a
	ld	hl, SHIPGEN
	ld	b, 3			; 3 列
.cl:
	push	bc
	ld	a, (gs_code)
	call	pcg_select
	ld	d, PCG_B >> 8
	call	def_plane		; B (hl += 8)
	ld	d, PCG_R >> 8
	call	def_plane		; R
	ld	d, PCG_G >> 8
	call	def_plane		; G (hl は次列 +24 へ)
	ld	a, (gs_code)
	inc	a
	ld	(gs_code), a
	pop	bc
	djnz	.cl
	ret

; a=code を PCG 定義セル(0x7FF)に選択 (ANK=code, ATR=PCG)
pcg_select:
	ld	e, a			; code
	ld	b, (TVRAM >> 8) | (PCG_DEFCELL >> 8)	; 0x37
	ld	c, PCG_DEFCELL & 0xFF			; 0xFF
	out	(c), e			; ANK[0x7FF]=code
	ld	b, (TATTR >> 8) | (PCG_DEFCELL >> 8)	; 0x27
	ld	a, 0x20
	out	(c), a			; ATR[0x7FF]=PCG
	ret

; d=プレーンポート上位(0x15/16/17), hl->8バイト。port=(d<<8)|(line<<1)
def_plane:
	ld	e, 0
.l:	ld	a, e
	add	a, a			; line<<1
	ld	c, a
	ld	b, d
	ld	a, (hl)
	out	(c), a
	inc	hl
	inc	e
	ld	a, e
	cp	8
	jr	nz, .l
	ret

;=====================================================================
; ジョイスティック読み取り → a (PSG port A, 負論理, bit=0 で押下)。
;   OUT 0x1C00,14 でレジスタ選択 → IN 0x1B00。
read_joy:
	ld	bc, PORT_PSGREG
	ld	a, 0x0E			; PSG reg 14 (port A = joystick)
	out	(c), a
	ld	bc, PORT_PSGDAT
	in	a, (c)
	ret

;=====================================================================
; 自機移動。横は ship_hx(2px単位) を毎フレーム ±1、縦は ship_row(8px) を
;   MOVE_DELAY フレームごと ±1。いずれも境界付き。
;   --- 方向マッピング (X1 モニタを 90度回した向き前提。bit 割当で調整可) ---
;     bit0=上, bit1=下, bit2=左, bit3=右 (X1 標準 PSG port A, 負論理)。
;     既定: 上→行-1, 下→行+1, 左→hx-1(左2px), 右→hx+1(右2px)。
;     90度回転で実機の見た目に合わせる場合はここの対応を入れ替える。
move_ship:
	call	read_joy
	ld	e, a			; e = joy (負論理)
	; --- 横: 毎フレーム 2px ---
	bit	2, e			; 左
	jr	nz, .nl
	ld	a, (ship_hx)
	or	a
	jr	z, .nl
	dec	a
	ld	(ship_hx), a
.nl:	bit	3, e			; 右
	jr	nz, .nr
	ld	a, (ship_hx)
	cp	SHIP_HXMAX
	jr	nc, .nr
	inc	a
	ld	(ship_hx), a
.nr:
	; --- 縦: MOVE_DELAY フレームごと 1 行(8px) ---
	ld	a, (move_dly)
	or	a
	jr	z, .vgo
	dec	a
	ld	(move_dly), a
	ret
.vgo:
	ld	d, 0			; d = 縦移動したか
	bit	0, e			; 上
	jr	nz, .nu
	ld	a, (ship_row)
	or	a
	jr	z, .nu
	dec	a
	ld	(ship_row), a
	ld	d, 1
.nu:	bit	1, e			; 下
	jr	nz, .nd
	ld	a, (ship_row)
	cp	ROWS - 2		; 行は 0..23 (2行ぶん)
	jr	nc, .nd
	inc	a
	ld	(ship_row), a
	ld	d, 1
.nd:
	ld	a, d
	or	a
	ret	z
	ld	a, MOVE_DELAY
	ld	(move_dly), a
	ret

;=====================================================================
; 自機を現フレームの表示 POS に合わせてテキストVRAMに配置 (スクロール追従)。
;   前回位置を消去し、新位置(cur_pos + 画面オフセット)に 2x2 を書く。
overlay_ship:
	; 前回消去 (両窓)
	ld	hl, (ship_prev)
	ld	a, h
	cp	0xFF
	jr	z, .noerase
	call	erase_3x2
	ld	hl, (ship_prev2)
	call	erase_3x2
.noerase:
	; soff = ship_row*40 + (ship_hx>>2) (画面セルオフセット)
	ld	h, 0
	ld	a, (ship_row)
	ld	l, a
	add	hl, hl
	add	hl, hl
	add	hl, hl			; *8
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, hl			; *32
	add	hl, de			; *40
	ld	a, (ship_hx)
	srl	a
	srl	a			; cell列 = hx>>2
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	(ship_soff), hl
	; 版 v = ship_hx & 3 → 先頭コード = SHIP_BASE + v*6
	ld	a, (ship_hx)
	and	3
	add	a, a			; 2v
	ld	e, a
	add	a, a			; 4v
	add	a, e			; 4v + 2v = 6v
	add	a, SHIP_BASE
	ld	(ship_code), a
	; 窓1: top-left = (cur_pos + soff) & 0x7FF
	ld	hl, (ship_soff)
	ld	de, (cur_pos)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(ship_prev), hl
	call	write_ship_3x2
	; 窓2: top-left = ((cur_pos ^ 0x400) + soff) & 0x7FF  (+1024 側の窓)
	ld	hl, (cur_pos)
	ld	a, h
	xor	0x04			; 0x0400 = +1024 窓
	ld	h, a
	ld	de, (ship_soff)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(ship_prev2), hl
	call	write_ship_3x2
	ret

; hl=top-left。選択版の 3列x2行 (6セル) を配置。コードは ship_code から連番。
;   row0: code+0,+1,+2  row1: code+3,+4,+5
write_ship_3x2:
	ld	(ws_tl), hl
	ld	a, (ship_code)
	ld	(ws_code), a
	ld	hl, (ws_tl)		; row0
	ld	b, 3
.r0:	push	bc
	push	hl
	ld	a, (ws_code)
	call	put_cell
	pop	hl
	inc	hl
	ld	a, (ws_code)
	inc	a
	ld	(ws_code), a
	pop	bc
	djnz	.r0
	ld	hl, (ws_tl)		; row1 = top + COLS
	ld	de, COLS
	add	hl, de
	ld	b, 3
.r1:	push	bc
	push	hl
	ld	a, (ws_code)
	call	put_cell
	pop	hl
	inc	hl
	ld	a, (ws_code)
	inc	a
	ld	(ws_code), a
	pop	bc
	djnz	.r1
	ret

; hl=cell(要マスク), a=code。ANK=code, ATR=SHIP_ATR。
put_cell:
	ld	e, a			; code
	ld	a, h
	and	0x07
	ld	h, a
	or	(TVRAM >> 8)
	ld	b, a
	ld	c, l
	out	(c), e			; ANK=code
	ld	a, h
	or	(TATTR >> 8)
	ld	b, a
	ld	a, SHIP_ATR
	out	(c), a			; ATR
	ret

; hl=top-left。3列x2行 を消去(ANK=0,ATR=0)。
erase_3x2:
	ld	(ws_tl), hl
	ld	b, 3			; row0
.r0:	push	bc
	push	hl
	call	clr_cell
	pop	hl
	inc	hl
	pop	bc
	djnz	.r0
	ld	hl, (ws_tl)		; row1 = top + COLS
	ld	de, COLS
	add	hl, de
	ld	b, 3
.r1:	push	bc
	push	hl
	call	clr_cell
	pop	hl
	inc	hl
	pop	bc
	djnz	.r1
	ret

clr_cell:
	ld	a, h
	and	0x07
	ld	h, a
	or	(TVRAM >> 8)
	ld	b, a
	ld	c, l
	xor	a
	out	(c), a			; ANK=0
	ld	a, h
	or	(TATTR >> 8)
	ld	b, a
	xor	a
	out	(c), a			; ATR=0
	ret
	ENDIF	; SHIP

;=====================================================================
; データ
crtc_tbl:			; 40桁x25行 15kHz (defreg と同一)
	db	0x37, 0x28, 0x2d, 0x34, 0x1f, 0x02, 0x19, 0x1c, 0x00
	db	0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

; Z80 DMA コマンド列: EMM データ 0x0D03(I/O固定) -> dma_id_dst(メモリ増加), 50B
;   dma_id_dst は read_ids で IDBUFA/B に patch。WR2=0x10 でメモリ宛。
dma_id:
	db	0xC3			; WR6 リセット
	db	0x7D			; WR0 A->B, A addr/len 続く
	db	LOW EMM_DAT, HIGH EMM_DAT
	db	LOW (COLIDS - 1), HIGH (COLIDS - 1)	; 49
	db	0x2C			; WR1 port A = I/O 固定
	db	0x10			; WR2 port B = メモリ 増加
	db	0xAD			; WR4 連続, port B addr 続く
dma_id_dst:
	dw	0			; 転送先 (patch)
	db	0x82			; WR5
	db	0xCF			; WR6 ロード
	db	0x87			; WR6 開始
dma_id_end:

; RAM 変数
coarse:		dw	0		; 表示ページの粗スクロール位置 (f>>2)
coarsen:	dw	0		; 次フレームの粗位置 ((f+1)>>2)
framecnt:	dw	0		; フレームカウンタ (16bit)
step:		dw	0		; スクロールステップ (speed_div フレームごと +1)
substep:	db	0		; step 内のフレーム (0..speed_div-1)
	IFNDEF	SPEED
SPEED		EQU	4		; 1 step = 何フレーム (速度)。4≈0.5px/f(アーケード寄り), 1=2px/f(高速デモ)
	ENDIF
speed_div:	db	SPEED		; -DSPEED=n で切替可。実行時に書換えても可
exp_target:	db	0		; redraw_chunk の目標行数
minspare:	dw	0xFFFF		; 最小フレーム余裕 (0=処理落ち)
phase:		db	0
npage:		db	0		; 次フレームに見せるページ
wcol:		db	0		; 描画対象の世界列 (0..255)
idlecnt:	dw	0		; M4: フレーム空き時間カウンタ
pf_page:	db	0
pf_base:	dw	0
pf_wc:		db	0
dc_base:	dw	0
dc_row:		db	0
emm_a0:		db	0
emm_a1:		db	0
emm_a2:		db	0
shl_hi:		db	0		; shl ルックアップ表の上位アドレス (0xC0+np)
shr_hi:		db	0		; shr ルックアップ表の上位アドレス (0xC4+np)
ec_row:		db	0		; expand: 行 0..24
ec_cell:	dw	0		; expand: 現セル
ec_ta:		dw	0		; expand: タイルA アドレス
ec_tb:		dw	0		; expand: タイルB アドレス
bt_np:		db	0		; build_tables: np
bt_shl:		db	0
bt_shr:		db	0
bt_shlpg:	db	0
bt_shrpg:	db	0
si_base:	dw	0
si_idx:		db	0
prev_base:	dw	0
	IFDEF	SHIP
; --- 自機 (M6, 開発中) ---
cur_pos:	dw	0		; このフレームの表示開始セル
ship_hx:	db	0		; 自機 横位置 (2px単位, 0..SHIP_HXMAX)
ship_row:	db	0		; 自機 画面セル行 (0..23)
ship_prev:	dw	0		; 前回の自機 top-left セル 窓1 (0xFFFF=無効)
ship_prev2:	dw	0		; 窓2
ship_soff:	dw	0		; 画面セルオフセット
ship_code:	db	0		; このフレームの先頭 PCG コード (版選択後)
ws_tl:		dw	0
ws_code:	db	0		; write_ship_3x2 の連番コード作業用
move_dly:	db	0		; 縦移動の遅延カウンタ
; --- gen_ship 作業変数 ---
gs_v:		db	0
gs_row:		db	0
gs_p:		db	0		; 位相 p=2v
gs_r:		db	0		; idx 0..23 (plane*8+raster)
gs_lsrc:	dw	0		; src 左セル先頭
gs_rsrc:	dw	0		; src 右セル先頭
gs_c1:		db	0		; シフト結果 col1
gs_c2:		db	0		; シフト結果 col2
gs_code:	db	0		; def_vr_cells 連番コード

; === 自機ベースデータ (shipdata) フォーマット ===
;   16x16 を 8x8 の 2x2 セルに分割。セル順 = TL, TR, BL, BR
;     TL=(x0-7,y0-7) TR=(x8-15,y0-7) BL=(x0-7,y8-15) BR=(x8-15,y8-15)
;   各セル = プレーン B, R, G の順。各プレーン = 8 ラスタ(y昇順)の 1 バイト。
;   バイト bit7 = 左端ピクセル, bit0 = 右端 (X1 GRAM/PCG と同じ)。
;   計 4セル x 3プレーン x 8ラスタ = 96 バイト。
;   起動時に gen_ship がこのベースから 0/2/4/6px の 4 シフト版(各3x2セル)を
;   24bit 右シフトで生成する (シフト版はここには持たない)。
;   生成/差し替えは tools/png2ship.py (16x16 RGB PNG -> この db ブロック)。
;   8色版はこのビットがそのまま色。turboZ 64色版はテキストパレットで色を
;   付けるので、色は呼び出し側で選び、ここはシルエット(例:全部R)でよい。
; 現機体 = オリジナルの矢印型(著作権配慮)。R プレーンのみ(赤)。右向き。
shipdata:
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TL B
	db	0x00,0x00,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F	; TL R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TL G
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TR B
	db	0x00,0x0C,0x3C,0xFC,0x3C,0x0C,0x00,0x00	; TR R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; TR G
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL B
	db	0x3F,0x3F,0x3F,0x3F,0x3F,0x00,0x00,0x00	; BL R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BL G
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BR B
	db	0x00,0x00,0x0C,0x3C,0xFC,0x3C,0x0C,0x00	; BR R
	db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00	; BR G
	ENDIF	; SHIP

	END
