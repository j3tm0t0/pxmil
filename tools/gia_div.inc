; gia_div.inc — get_index_for_angle の ratio 除算。sprite.inc と tools/giatest.asm が INCLUDE する。
; ratio = floor(small*32/big) を求める。small<=big なので ratio は 0..32。
; in : hl=small*32 (0..big*32), de=big (>=1, big=0 は呼出前にガード)
; out: a=ratio。副作用: bc/hl 破壊 (呼出側は a のみ使用)。
; GIA_DIVSUB 定義時は従来の減算ループ、既定は 6bit 復元法 2 進除算。
; 両者は floor(small*32/big) で bit 単位一致 (tools/giatest.asm で全入力検証)。
gia_divide:
  IFDEF GIA_DIVSUB
	jp	gia_div_sub
  ELSE
	jp	gia_div_bin
  ENDIF

; 減算ループ版 (従来 ROM 準拠)。ratio 回だけ big を引く。
gia_div_sub:
	ld	c, 0			; ratio
.rl:	or	a
	sbc	hl, de
	jr	c, .rdone
	inc	c
	jr	.rl
.rdone:	ld	a, c
	ret

; 6bit 復元法 2 進除算。被除数 hl を big*32,big*16,..,big*1 と順に比較-減算し
; 商ビットを MSB から 6 個求める。減算ループと同じ floor 除算で定数時間に近い。
; bc=big<<5 を前計算。各段 `add a,a` は a<64 のため必ず carry を落とし、直後の
; `sbc hl,bc` は借り無しの hl-bc になる。
gia_div_bin:
	ld	b, d			; bc = big (d=0 既定, b=0)
	ld	c, e
	sla	c : rl b		; big*2
	sla	c : rl b		; big*4
	sla	c : rl b		; big*8
	sla	c : rl b		; big*16
	sla	c : rl b		; big*32
	xor	a			; ratio=0
	add	a, a			; bit5: a<<=1 (a<64 なので carry=0)
	sbc	hl, bc			; hl-=big*32
	jr	c, .r5			; 借り → 復元 (bit=0)
	inc	a			; 借り無し → bit=1
	jr	.s5
.r5:	add	hl, bc			; 復元
.s5:	srl	b : rr c		; divisor>>=1
	add	a, a			; bit4
	sbc	hl, bc
	jr	c, .r4
	inc	a
	jr	.s4
.r4:	add	hl, bc
.s4:	srl	b : rr c
	add	a, a			; bit3
	sbc	hl, bc
	jr	c, .r3
	inc	a
	jr	.s3
.r3:	add	hl, bc
.s3:	srl	b : rr c
	add	a, a			; bit2
	sbc	hl, bc
	jr	c, .r2
	inc	a
	jr	.s2
.r2:	add	hl, bc
.s2:	srl	b : rr c
	add	a, a			; bit1
	sbc	hl, bc
	jr	c, .r1
	inc	a
	jr	.s1
.r1:	add	hl, bc
.s1:	srl	b : rr c
	add	a, a			; bit0
	sbc	hl, bc
	jr	c, .r0
	inc	a
	ret
.r0:	add	hl, bc
	ret
