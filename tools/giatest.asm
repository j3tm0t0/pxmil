; giatest.asm — gia_div_sub (従来 ROM 準拠の減算ループ) と gia_div_bin (6bit 2進除算) が
; floor(small*32/big) で bit 単位一致することを全入力で網羅検証する。
;
; 検証領域: big = 1..255, small = 0..big (min/max で上流が保証する領域)。
;   ペア総数 = Σ_{big=1}^{255}(big+1) = 32895。
;
; PROBE 出力 (OUT 0x00FE=lo, 0x00FF=hi -> stderr "PROBE <(hi<<8)|lo>"):
;   0xE5E5                       開始センチネル
;   mismatch 数 (期待 0)
;   total ペア数 (期待 32895)
;   fm_big<<8 | fm_small         最初の不一致の入力 (無ければ 0)
;   fm_sub<<8  | fm_bin          その sub/bin 値
;   0xDEAD                       本検証の終端
;   以降 self-check: big=255 の各 small(0..255) で small<<8 | sub_result を 256 行
;       (Python が small*32//255 と突き合わせ、ハーネス自体の健全性を確認)
;   0xDEAF                       self-check 終端
;
; 実行: sjasmplus --raw=giatest.bin tools/giatest.asm ; mkx1disk ; xmilsdl2 (XMIL_PROBE=1)

	DEVICE	NOSLOT64K
	ORG	0x0100

start:
	di
	ld	sp, 0xFE00

	; --- 変数初期化 ---
	ld	hl, 0
	ld	(mis), hl
	ld	(total), hl
	xor	a
	ld	(fm_big), a		; 0 = 不一致未捕捉

	ld	a, 1
	ld	(t_big), a
.big_loop:
	xor	a
	ld	(t_small), a
.small_loop:
	call	setup_hlde		; hl=small*32, de=big
	push	de
	push	hl
	call	gia_div_sub
	ld	(r_sub), a
	pop	hl
	pop	de
	call	gia_div_bin
	ld	(r_bin), a

	ld	hl, (total)		; total++
	inc	hl
	ld	(total), hl

	ld	a, (r_sub)		; compare
	ld	b, a
	ld	a, (r_bin)
	cp	b
	jr	z, .ok
	ld	hl, (mis)		; mismatch++
	inc	hl
	ld	(mis), hl
	ld	a, (fm_big)		; 最初の不一致だけ捕捉
	or	a
	jr	nz, .ok
	ld	a, (t_big)
	ld	(fm_big), a
	ld	a, (t_small)
	ld	(fm_small), a
	ld	a, (r_sub)
	ld	(fm_sub), a
	ld	a, (r_bin)
	ld	(fm_bin), a
.ok:
	ld	a, (t_small)		; small++ ; small<=big なら継続
	inc	a
	jr	z, .big_next		; 255->0 wrap = この big 完了
	ld	(t_small), a
	ld	b, a
	ld	a, (t_big)
	cp	b			; big - small ; big>=small で carry 無し
	jr	nc, .small_loop
.big_next:
	ld	a, (t_big)		; big++ ; 255->0 wrap で終了
	inc	a
	ld	(t_big), a
	jr	nz, .big_loop

	; --- 結果出力 ---
	ld	hl, 0xE5E5
	call	emit_hl
	ld	hl, (mis)
	call	emit_hl
	ld	hl, (total)
	call	emit_hl
	ld	a, (fm_big)
	ld	h, a
	ld	a, (fm_small)
	ld	l, a
	call	emit_hl
	ld	a, (fm_sub)
	ld	h, a
	ld	a, (fm_bin)
	ld	l, a
	call	emit_hl
	ld	hl, 0xDEAD
	call	emit_hl

	; --- self-check: big=255 の全 small で sub 値を出力 ---
	xor	a
	ld	(t_small), a
.sc_loop:
	ld	a, (t_small)
	ld	l, a
	ld	h, 0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl			; small*32
	ld	de, 255
	call	gia_div_sub		; a = small*32//255
	ld	e, a
	ld	a, (t_small)
	ld	h, a
	ld	l, e
	call	emit_hl			; small<<8 | sub
	ld	a, (t_small)
	inc	a
	ld	(t_small), a
	jr	nz, .sc_loop
	ld	hl, 0xDEAF
	call	emit_hl

.hang:	halt
	jr	.hang

; hl = small*32, de = big を作る
setup_hlde:
	ld	a, (t_small)
	ld	l, a
	ld	h, 0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl			; small*32
	ld	a, (t_big)
	ld	e, a
	ld	d, 0
	ret

; PROBE = h<<8 | l
emit_hl:
	ld	a, l
	ld	bc, 0x00FE
	out	(c), a			; lo ラッチ
	ld	a, h
	ld	bc, 0x00FF
	out	(c), a			; 出力
	ret

	INCLUDE	"gia_div.inc"

t_big:		DB	0
t_small:	DB	0
r_sub:		DB	0
r_bin:		DB	0
fm_big:		DB	0
fm_small:	DB	0
fm_sub:		DB	0
fm_bin:		DB	0
mis:		DW	0
total:		DW	0

	END
