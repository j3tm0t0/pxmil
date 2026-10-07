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
;   M3 (列データの EMM→DMA 供給) は未実装: 縦1列は GRAM ポート空間で不連続
;       (cell stride 40, raster stride 0x800) のため単一 DMA で GRAM に直接描けず、
;       記事も「1列ループはCPUで転送」とする。EMM→DMA→GRAM 経路自体は
;       tools/emmtest.asm で実証済み。2D STRIP 化が今後の課題。

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
PORT_PPIB	EQU	0x1A01		; bit7=DISP (VBLANK判定)
TVRAM		EQU	0x3000
TATTR		EQU	0x2000

COLS		EQU	40
ROWS		EQU	25
OFF1024		EQU	1024		; バンク内 2 窓目のオフセット

;=====================================================================
start:
	di
	ld	sp, 0xF000

	call	init_screen
	call	clear_tvram

	IFNDEF	DBG_STATIC
	; --- 初期プリフィル: 両バンクに 0 ページ分 (全窓) 描く ---
	; coarse=0 で pg0..3 の表示窓を世界列 0..39 で埋める
	call	prefill
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
; メインループ: framecnt(16bit) 1増=1フレーム。phase=f&3, coarse=f>>2。
;   見せるページ=phase (POS=coarse+off, shift は coltab で付与)。
;   次フレームに見せるページ np=(f+1)&3 の col39 を、その表示 POS の
;   1つ手前(base=POS-1)に先行描画 (draw_column は base+40(r+1)=col39@POS)。
mainloop:
	call	wait_vblank
	; --- phase=f&3, coarse=f>>2 ---
	ld	hl, (framecnt)
	ld	a, l
	and	3
	ld	(phase), a
	srl	h
	rr	l
	srl	h
	rr	l
	ld	(coarse), hl		; coarse = f>>2
	; --- np=(f+1)&3, coarsen=(f+1)>>2 ---
	ld	hl, (framecnt)
	inc	hl
	ld	a, l
	and	3
	ld	(npage), a
	srl	h
	rr	l
	srl	h
	rr	l
	ld	(coarsen), hl		; coarse_n = (f+1)>>2
	; --- 0x1FD0: disp=phase>>1, access=np>>1 ---
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
	ld	bc, PORT_SCRN
	out	(c), a
	; --- 表示 POS = (coarse + (phase&1?1024:0)) & 0x7FF ---
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
	call	setpos
	; --- 再描画: np の col39 ---
	IFNDEF	DBG_NOREDRAW
	call	redraw_next
	ENDIF
	; --- M4: フレーム作業後の空き時間を計測して表示 ---
	;   アクティブ表示期間 (DISP=1) に idle カウンタを回し、DISP=0 に
	;   なるまでの回数 = 1フレームの CPU 空き容量。8MHz なら約2倍になる。
	call	wait_active		; DISP=1 になるまで待つ
	ld	de, 0
	ld	bc, PORT_PPIB
.spin:	in	a, (c)
	add	a, a			; DISP -> carry
	jr	nc, .spindone		; DISP=0 (アクティブ終了) で停止
	inc	de
	jr	.spin
.spindone:
	ld	(idlecnt), de
	; デバッグプローブ出力 (XMIL_PROBE 有効時のみエミュが拾う)
	ld	bc, 0x00FE
	out	(c), e			; lo
	ld	bc, 0x00FF
	out	(c), d			; hi
	IFDEF	M4_DISPLAY
	call	show_idle		; オンスクリーン表示 (任意)
	ENDIF
	; framecnt++
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
; 次ページ np の col39 を先行描画。
;   表示 POS_n = coarse_n + off_np。col39@POS_n にするため base = POS_n - 1。
;   世界列 wc = coarse_n + (COLS-1) = 右端。content = coltab[np][wc&7]。
redraw_next:
	; wc = coarse_n + COLS-1
	ld	hl, (coarsen)
	ld	bc, COLS - 1
	add	hl, bc
	; e = (wc&7)*3
	ld	a, l
	and	0x07
	ld	e, a
	add	a, a
	add	a, e
	ld	e, a
	; ptr = coltab + np*24 + e
	ld	a, (npage)
	ld	l, a
	ld	h, 0
	add	hl, hl
	add	hl, hl
	add	hl, hl			; *8
	ld	b, h
	ld	c, l
	add	hl, hl			; *16
	add	hl, bc			; *24
	ld	bc, coltab
	add	hl, bc
	ld	c, e
	ld	b, 0
	add	hl, bc
	ld	a, (hl)
	ld	(colB), a
	inc	hl
	ld	a, (hl)
	ld	(colR), a
	inc	hl
	ld	a, (hl)
	ld	(colG), a
	; base = (coarse_n + off_np) - 1, &0x7FF
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
	call	draw_column
	ret

;=====================================================================
; 右端 1 列描画: dc_base=base(cell), colB/colR/colG を使う
;   セル = (base + 40*(r+1)) & 0x7FF , r=0..24, 各 8 raster, 3 プレーン
draw_column:
	ld	a, ROWS
	ld	(dc_row), a
	ld	hl, 0			; hl = オフセット(40 の倍数)
.rloop:
	ld	bc, COLS
	add	hl, bc			; オフセット += 40 (40,80,...,1000)
	push	hl			; オフセット退避
	ld	de, (dc_base)
	add	hl, de			; base + offset
	ld	a, h
	and	0x07
	ld	h, a			; cell &0x7FF (hl=cell, 3 プレーン共通)
	; --- B プレーン ---
	ld	a, h
	or	(GRAM_B >> 8)		; 0x40 | cellhigh
	ld	b, a
	ld	c, l			; bc = port
	ld	a, (colB)
	call	wcell8
	; --- R プレーン ---
	ld	a, h
	or	(GRAM_R >> 8)		; 0x80
	ld	b, a
	ld	c, l
	ld	a, (colR)
	call	wcell8
	; --- G プレーン ---
	ld	a, h
	or	(GRAM_G >> 8)		; 0xC0
	ld	b, a
	ld	c, l
	ld	a, (colG)
	call	wcell8
	pop	hl			; オフセット復帰
	ld	a, (dc_row)
	dec	a
	ld	(dc_row), a
	jr	nz, .rloop
	ret

; 1 セルの 8 raster に同じバイトを書く
;   bc=port(b=high c=low), a=data。raster で port high(b) +0x08。
;   bc/de/hl 保持 (a 破壊)。
wcell8:
	push	bc
	push	de
	ld	d, a			; d=data
	ld	e, 8			; raster 8 本
.w:	out	(c), d
	ld	a, b
	add	a, 0x08			; raster++ (+0x800)
	ld	b, a
	dec	e
	jr	nz, .w
	pop	de
	pop	bc
	ret

;=====================================================================
; 初期プリフィル: coarse=0 で pg0..3 の表示窓 40 列を世界列 0..39 で描く
;   各ページを選び、列 0..39 を順に draw_column 相当で埋める。
;   簡易化: redraw_rp と同じ経路を、coarse を 0..39 と仮想的に進めず、
;   列ごとに base を +1 して書く。
prefill:
	ld	a, 0
	ld	(pf_page), a
.ploop:
	; このページの bank を access/disp に選ぶ (bank = page>>1)
	ld	a, (pf_page)
	srl	a			; a = bank (0/1)
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
	ld	e, a
	ld	bc, PORT_SCRN
	out	(c), e
	; base = (pf_page&1 ? 1024 : 0)
	ld	hl, 0
	ld	a, (pf_page)
	and	1
	jr	z, .b0
	ld	hl, OFF1024
.b0:
	ld	(pf_base), hl
	; 列 wc=0..39 を描く。各列の base = pf_base + wc, draw_column は
	; base+40*(r+1) に書くので、base を (wc-40) 相当にするのは面倒。
	; 代わりに「draw_column の base = pf_base + wc - 40」とすると
	; 実セル = pf_base + wc + 40*r。これで列 wc が画面列 wc に入る。
	ld	a, 0
	ld	(pf_wc), a
.wloop:
	; coltab ptr = coltab + page*24 + (wc&7)*3
	ld	a, (pf_wc)
	and	7
	ld	e, a
	add	a, a
	add	a, e
	ld	e, a			; (wc&7)*3
	ld	a, (pf_page)
	ld	l, a
	ld	h, 0
	add	hl, hl
	add	hl, hl
	add	hl, hl			; *8
	ld	b, h
	ld	c, l
	add	hl, hl			; *16
	add	hl, bc			; *24
	ld	bc, coltab
	add	hl, bc
	ld	c, e
	ld	b, 0
	add	hl, bc
	ld	a, (hl)
	ld	(colB), a
	inc	hl
	ld	a, (hl)
	ld	(colR), a
	inc	hl
	ld	a, (hl)
	ld	(colG), a
	; base = pf_base + wc - 40  (draw_column が +40 から始めるため)
	ld	hl, (pf_base)
	ld	a, (pf_wc)
	ld	c, a
	ld	b, 0
	add	hl, bc			; + wc
	ld	bc, COLS
	or	a
	sbc	hl, bc			; - 40
	ld	a, h
	and	0x07
	ld	h, a
	ld	(dc_base), hl
	call	draw_column
	; wc++
	ld	a, (pf_wc)
	inc	a
	ld	(pf_wc), a
	cp	COLS
	jr	nz, .wloop
	; page++
	ld	a, (pf_page)
	inc	a
	ld	(pf_page), a
	cp	4
	jp	nz, .ploop
	ret

;=====================================================================
; VBLANK 待ち: DISP(bit7) が 1 の間待ち、0 になったら戻る
wait_vblank:
	ld	bc, PORT_PPIB
.w:	in	a, (c)
	add	a, a			; bit7 -> carry
	jr	c, .w			; DISP=1 の間待つ
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

;=====================================================================
; データ
crtc_tbl:			; 40桁x25行 15kHz (defreg と同一)
	db	0x37, 0x28, 0x2d, 0x34, 0x1f, 0x02, 0x19, 0x1c, 0x00
	db	0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

; coltab: page0..3 (shift 0/2/4/6px) x worldcell 0..7 x {B,R,G}
;   世界: 8pxバー(色=バー番号&7) 左2px=白。周期64px(8セル)。
coltab:
	db	0xC0,0xC0,0xC0, 0xFF,0xC0,0xC0, 0xC0,0xFF,0xC0, 0xFF,0xFF,0xC0, 0xC0,0xC0,0xFF, 0xFF,0xC0,0xFF, 0xC0,0xFF,0xFF, 0xFF,0xFF,0xFF
	db	0x03,0x03,0x03, 0xFF,0x03,0x03, 0x03,0xFF,0x03, 0xFF,0xFF,0x03, 0x03,0x03,0xFF, 0xFF,0x03,0xFF, 0x03,0xFF,0xFF, 0xFF,0xFF,0xFF
	db	0x0F,0x0C,0x0C, 0xFC,0x0F,0x0C, 0x0F,0xFF,0x0C, 0xFC,0xFC,0x0F, 0x0F,0x0C,0xFF, 0xFC,0x0F,0xFF, 0x0F,0xFF,0xFF, 0xFC,0xFC,0xFC
	db	0x3F,0x30,0x30, 0xF0,0x3F,0x30, 0x3F,0xFF,0x30, 0xF0,0xF0,0x3F, 0x3F,0x30,0xFF, 0xF0,0x3F,0xFF, 0x3F,0xFF,0xFF, 0xF0,0xF0,0xF0

; RAM 変数
coarse:		dw	0		; 表示ページの粗スクロール位置 (f>>2)
coarsen:	dw	0		; 次フレームの粗位置 ((f+1)>>2)
framecnt:	dw	0		; フレームカウンタ (16bit)
phase:		db	0
npage:		db	0		; 次フレームに見せるページ
idlecnt:	dw	0		; M4: フレーム空き時間カウンタ
colB:		db	0
colR:		db	0
colG:		db	0
pf_page:	db	0
pf_base:	dw	0
pf_wc:		db	0
dc_base:	dw	0
dc_row:		db	0
si_base:	dw	0
si_idx:		db	0
prev_base:	dw	0

	END
