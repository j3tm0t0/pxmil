; sndplay.inc - X1 PSG サウンドプレイヤ(独立モジュール, VBLANKごとに snd_tick を1回)
;
;   xevi_sound.py が出力する PSG データ(.bin)を再生する。
;   データ形式: ch_count(1B) + ch_count*2B オフセット(tune先頭からの相対) +
;               各チャンネルストリーム。1 event=3B [period_lo, (vol<<4)|period_hi, dur_frames]。
;               休符: vol=0。終端: dur=0。
;   チャンネル i -> PSG A/B/C(period R(i*2)/R(i*2+1), vol R(8+i))。
;
;   PSG(AY-3-8910相当, X1): レジスタ選択=OUT 0x1C00, データ=OUT 0x1B00。
;   API: snd_init(最初に1回) / snd_start(IX=tune先頭) / snd_tick(毎フレーム) /
;        snd_active(A=残アクティブch数, 0=曲終了)。
;   破壊: AF,BC,DE,HL,IY(呼び出し側で必要なら退避)。

PSG_REG		EQU	0x1C00		; レジスタ選択(OUT)
PSG_DAT		EQU	0x1B00		; データ(OUT)

; ---- PSG レジスタ書き込み: D=reg番号, E=値 ----
psg_set:
	ld	bc, PSG_REG
	out	(c), d
	ld	bc, PSG_DAT
	out	(c), e
	ret

; ---- 初期化: 全音量0 + ミキサ(トーンABC有効/ノイズ無効) ----
snd_init:
	xor	a
	ld	(snd_nch), a
	ld	e, 0
	ld	d, 8
	call	psg_set			; R8=0
	ld	d, 9
	call	psg_set			; R9=0
	ld	d, 10
	call	psg_set			; R10=0
	ld	d, 7
	ld	e, 0x38			; R7: tone ABC on(bit0-2=0), noise off(bit3-5=1)
	call	psg_set
	ret

; ---- 曲開始: IX=tune データ先頭 ----
snd_start:
	push	ix
	pop	hl
	ld	(tune_base), hl
	ld	a, (hl)			; ch_count
	ld	(snd_nch), a
	or	a
	ret	z
	ld	(ss_cnt), a
	inc	hl
	ld	(ss_src), hl		; オフセット表
	ld	iy, snd_ch
.ss:
	ld	hl, (ss_src)
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE = offset
	inc	hl
	ld	(ss_src), hl
	ld	hl, (tune_base)
	add	hl, de			; HL = stream ptr
	ld	(iy+0), l
	ld	(iy+1), h
	ld	(iy+2), 1		; counter=1(次tickで即ロード)
	ld	(iy+3), 1		; active
	ld	de, 4
	add	iy, de
	ld	a, (ss_cnt)
	dec	a
	ld	(ss_cnt), a
	jr	nz, .ss
	ret

; ---- 毎フレーム更新 ----
snd_tick:
	ld	a, (snd_nch)
	or	a
	ret	z
	ld	(st_nch), a
	xor	a
	ld	(st_ch), a
	ld	iy, snd_ch
.chl:
	ld	a, (iy+3)		; active?
	or	a
	jp	z, .next
	dec	(iy+2)			; counter--
	jp	nz, .next
	; 次 event ロード
	ld	l, (iy+0)
	ld	h, (iy+1)
	ld	a, (hl)
	ld	(ev_plo), a
	inc	hl
	ld	a, (hl)
	ld	(ev_vph), a
	inc	hl
	ld	a, (hl)			; duration
	inc	hl
	ld	(iy+0), l
	ld	(iy+1), h
	or	a
	jr	nz, .play
	; チャンネル終了
	ld	(iy+3), a		; act=0
	ld	a, (st_ch)
	add	a, 8
	ld	d, a
	ld	e, 0
	call	psg_set			; vol off
	jr	.next
.play:
	ld	(iy+2), a		; counter=duration
	ld	a, (st_ch)		; period fine reg = ch*2
	add	a, a
	ld	d, a
	ld	a, (ev_plo)
	ld	e, a
	call	psg_set
	ld	a, (st_ch)		; period coarse reg = ch*2+1
	add	a, a
	inc	a
	ld	d, a
	ld	a, (ev_vph)
	and	0x0F
	ld	e, a
	call	psg_set
	ld	a, (st_ch)		; vol reg = 8+ch
	add	a, 8
	ld	d, a
	ld	a, (ev_vph)
	rrca
	rrca
	rrca
	rrca
	and	0x0F
	ld	e, a
	call	psg_set
.next:
	ld	a, (st_ch)
	inc	a
	ld	(st_ch), a
	ld	de, 4
	add	iy, de
	ld	a, (st_nch)
	dec	a
	ld	(st_nch), a
	jp	nz, .chl
	ret

; ---- アクティブ ch 数(A, 0=全終了) ----
snd_active:
	ld	a, (snd_nch)
	or	a
	ret	z
	ld	(st_nch), a
	ld	iy, snd_ch
	ld	c, 0
.al:
	ld	a, (iy+3)
	add	a, c
	ld	c, a
	ld	de, 4
	add	iy, de
	ld	a, (st_nch)
	dec	a
	ld	(st_nch), a
	jr	nz, .al
	ld	a, c
	ret

; ---- 状態 ----
snd_nch:	db	0		; チャンネル数(0=停止)
snd_ch:		ds	12		; 3ch × [ptrL, ptrH, cnt, act]
tune_base:	dw	0
ss_src:		dw	0
ss_cnt:		db	0
st_nch:		db	0
st_ch:		db	0
ev_plo:		db	0
ev_vph:		db	0
