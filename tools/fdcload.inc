; fdcload.inc - FDC -> EMM ローダ(独立モジュール, 割り込み無し・ポーリング)
;
;   mkx1disk.py の --data で置いた追加データファイル(本体直後の連続セクタ)を、
;   MB8877 互換 FDC(I/O 0x0FF8-0x0FFC)でセクタ読みし、EMM(I/O 0x0D00-0x0D03)の
;   指定アドレスへ書き込む。実機 X1 でも動く作法(モーターON→Restore→Seek→Read)。
;
;   セクタ配置(2D: 40cyl x 2side x 16sec, 256B/sec, mkx1disk と同じ線形):
;     linear S -> sector=(S&15)+1, track_combined=S>>4, cyl=tc>>1, side=tc&1
;
;   API:
;     fdc_init         : モーターON・ドライブ0・Restore(track0 へシーク)。最初に1回。
;     fdc_load         : BC=開始 linear セクタ, DE=セクタ数, EMM addr は呼び出し前に
;                        set_emm_dst で設定。DE セクタ分を FDC から EMM へ連続書込。
;     set_emm_dst      : HL=EMM addr下16bit, A=bit16..23 を 0x0D00-02 へ設定。
;
;   FDC ポート
FDC_CMD		EQU	0x0FF8		; W:command / R:status (bit0=BUSY, bit1=DRQ)
FDC_TRK		EQU	0x0FF9		; track register
FDC_SEC		EQU	0x0FFA		; sector register
FDC_DAT		EQU	0x0FFB		; data register
FDC_CTL		EQU	0x0FFC		; W: bit7=motor bit4=side bit0-1=drive
;   EMM ポート
EMM_AL		EQU	0x0D00		; addr[7:0]
EMM_AM		EQU	0x0D01		; addr[15:8]
EMM_AH		EQU	0x0D02		; addr[23:16]
EMM_DT		EQU	0x0D03		; data(R/W, 読み書きで addr 自動 +1)
DRIVE		EQU	0		; ドライブ0

; ---- EMM 宛先アドレス設定: HL=addr[15:0], A=addr[23:16] ----
set_emm_dst:
	push	bc
	ld	c, EMM_AL & 0xFF
	ld	b, EMM_AL >> 8
	out	(c), l
	inc	c
	out	(c), h
	inc	c
	out	(c), a
	pop	bc
	ret

; ---- 初期化: モーターON + Restore ----
fdc_init:
	ld	a, 0x80 | DRIVE		; motor on, side0, drive0
	ld	(cur_ctl), a
	ld	bc, FDC_CTL
	out	(c), a
	call	fdc_motordelay		; モーター回転待ち
	ld	bc, FDC_CMD
	ld	a, 0x0B			; Restore (h=1,V=1,r1r0=11: 速いステップ+verify)
	out	(c), a
	call	fdc_wait
	xor	a
	ld	(cur_cyl), a		; 現在シリンダ=0
	ret

; ---- DE セクタを BC(開始 linear sec)から EMM(事前設定)へ読み込み ----
;   破壊: AF,BC,DE,HL
fdc_load:
.next:
	ld	a, d
	or	e
	ret	z			; 残0 なら終了
	push	de			; 残セクタ数
	; --- linear sector BC -> cyl/side/sec ---
	ld	a, c
	and	0x0F
	inc	a
	ld	(fl_sec), a		; sector = (S&15)+1
	; tc = S>>4 (16bit BC >>4)
	ld	h, b
	ld	l, c
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l			; HL = S>>4 = track_combined
	ld	a, l
	and	1
	ld	(fl_side), a		; side = tc&1
	srl	l			; cyl = tc>>1 (上位は0想定: 2Dは cyl<=39)
	ld	a, l
	ld	(fl_cyl), a
	; --- 必要ならシーク ---
	ld	hl, cur_cyl
	cp	(hl)
	call	nz, fdc_seek		; cyl 変化時のみシーク
	; --- side/drive 設定 ---
	ld	a, (fl_side)
	add	a, a
	add	a, a
	add	a, a
	add	a, a			; side -> bit4
	or	0x80 | DRIVE		; motor on
	ld	(cur_ctl), a
	push	bc
	ld	bc, FDC_CTL
	out	(c), a
	pop	bc
	; --- sector register ---
	push	bc
	ld	a, (fl_sec)
	ld	bc, FDC_SEC
	out	(c), a
	; --- Read Sector (type II single, 0x88) → EMM ---
	call	fdc_read_to_emm
	pop	bc
	inc	bc			; 次 linear セクタ
	pop	de
	dec	de
	jr	.next

; ---- linear sector0 (cyl0,side0,sec1) を RAM HL へ読む(マニフェスト用, init 直後) ----
;   破壊: AF,BC,HL
fdc_read0_ram:
	ld	bc, FDC_CTL
	ld	a, 0x80 | DRIVE		; motor, side0
	out	(c), a
	ld	bc, FDC_SEC
	ld	a, 1
	out	(c), a			; sector=1
	ld	bc, FDC_CMD
	ld	a, 0x88			; Read Sector
	out	(c), a
.l:	in	a, (c)			; status
	bit	1, a
	jr	nz, .rd			; DRQ → 読む
	bit	0, a
	jr	nz, .l			; BUSY(DRQ無し) → 待つ
	ret				; not busy & no DRQ → 終了
.rd:	push	bc
	ld	bc, FDC_DAT
	in	a, (c)
	pop	bc
	ld	(hl), a
	inc	hl
	jr	.l

; ---- A=cyl へシーク ----
fdc_seek:
	ld	(cur_cyl), a
	push	bc
	ld	bc, FDC_DAT		; MB8877 Seek: data reg = 目標トラック
	out	(c), a
	ld	bc, FDC_CMD
	ld	a, 0x1B			; Seek (h=1,V=1,r1r0=11)
	out	(c), a
	call	fdc_wait
	pop	bc
	ret

; ---- 1セクタ(256B)を FDC から EMM(0x0D03)へ。Read Sector 発行 + DRQ ポーリング ----
fdc_read_to_emm:
	push	bc
	push	de
	ld	bc, FDC_CMD
	ld	a, 0x88			; Read Sector (single, type II)
	out	(c), a			; bc=FDC_CMD のまま下で status 読み
.rloop:
	in	a, (c)			; status (FDC_CMD)
	bit	1, a
	jr	nz, .rdata		; DRQ → データ読む
	bit	0, a
	jr	nz, .rloop		; BUSY(DRQ無し) → 待つ
	jr	.rdone			; not busy & no DRQ → 終了
.rdata:
	; DRQ: FDC data → EMM data
	ld	c, FDC_DAT & 0xFF	; C=0xFB (B=0x0F のまま)
	in	a, (c)			; FDC data
	ld	c, FDC_CMD & 0xFF	; C=0xF8 復帰
	push	bc
	ld	bc, EMM_DT
	out	(c), a			; EMM へ書込(addr 自動 +1)
	pop	bc
	jr	.rloop
.rdone:
	pop	de
	pop	bc
	ret

; ---- ステータス BUSY が落ちるまで待つ ----
fdc_wait:
	push	bc
	ld	bc, FDC_CMD
.w:	in	a, (c)
	bit	0, a
	jr	nz, .w
	pop	bc
	ret

; ---- モーター回転待ち(簡易ループ) ----
fdc_motordelay:
	ld	bc, 0x4000
.md:	dec	bc
	ld	a, b
	or	c
	jr	nz, .md
	ret

; ---- 作業変数 ----
cur_ctl:	db	0
cur_cyl:	db	0
fl_sec:		db	0
fl_side:	db	0
fl_cyl:		db	0
