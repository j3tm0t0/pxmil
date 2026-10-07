; sndtest.asm - sndplay.inc 試聴・サイクル計測用(独立, emmscroll64.asm 非依存)。
;   fanfare -> bgm -> zapper -> blaster を順に再生。各 snd_tick のサイクルを
;   IN 0x00FC/0x00FD(CPU_CLOCKCOUNT)で計測し、最大値を probe(0x00FE/0xFF)へ出力。
;
;   ビルド:
;     python3 tools/xevi_sound.py           # 先に PSGデータ生成
;     sjasmplus --raw=sndtest.bin tools/sndtest.asm -I tools -I .
;     python3 tools/mkx1disk.py sndtest.bin -o SNDTEST.2d -n SNDTEST --load 0x0100
;   実行(Mac native, 音は SDL audio。XMIL_PROBEで最大サイクルが stderr に出る):
;     XMIL_PROBE=1 XMIL_ROM_TYPE=3 ./xmilsdl2 SNDTEST.2d   (afplay相当の鳴動)

	DEVICE	NONE
	ORG	0x0100

start:
	di
	ld	sp, 0xF000
	ld	hl, 0
	ld	(max_cyc), hl
	call	snd_init
	; ※ ヘッドレスはスロットルが短時間で効かず、連続実行で初めて音声バッファが
	;    詰まってリアルタイム同期される。全曲を無限ループ再生して鳴動させる。
.seq:
	ld	ix, tune_fanfare
	call	play_tune
	ld	ix, tune_bgm
	call	play_tune
	ld	ix, tune_zapper
	call	play_tune
	ld	ix, tune_blaster
	call	play_tune
	; 1周ごとに最大 snd_tick サイクルを probe 出力(計測は初回で確定)
	ld	de, (max_cyc)
	call	probe16
	jr	.seq

; ---- 1曲を全ch終了まで再生 ----
play_tune:
	call	snd_start
.ploop:
	call	frame_delay
	call	cyc_read		; HL = 開始カウント
	ld	(cyc_start), hl
	call	snd_tick
	call	cyc_read		; HL = 終了カウント
	ld	de, (cyc_start)
	or	a
	sbc	hl, de			; HL = delta(16bit modular)
	ld	(cur_delta), hl
	; max 更新
	ld	de, (max_cyc)
	ld	hl, (cur_delta)
	or	a
	sbc	hl, de
	jr	c, .nomax
	ld	hl, (cur_delta)
	ld	(max_cyc), hl
.nomax:
	call	snd_active
	or	a
	jr	nz, .ploop
	ret

; ---- 16bit サイクルカウンタ読み: HL ----
cyc_read:
	ld	bc, 0x00FC
	in	a, (c)			; low(hi をラッチ)
	ld	l, a
	ld	bc, 0x00FD
	in	a, (c)			; hi
	ld	h, a
	ret

; ---- 約1/60秒のディレイ(~66000T @4MHz) ----
frame_delay:
	ld	hl, 0x0A00
.fd:
	dec	hl
	ld	a, h
	or	l
	jr	nz, .fd
	ret

; ---- DE を probe(0x00FE/0x00FF)出力 ----
probe16:
	ld	bc, 0x00FE
	out	(c), e
	inc	c
	out	(c), d
	ret

	INCLUDE	"sndplay.inc"

cyc_start:	dw	0
cur_delta:	dw	0
max_cyc:	dw	0

tune_fanfare:	incbin	"roms/arcade/xevious-out/sound/xevi_fanfare.bin"
tune_bgm:	incbin	"roms/arcade/xevious-out/sound/xevi_bgm.bin"
tune_zapper:	incbin	"roms/arcade/xevious-out/sound/xevi_zapper.bin"
tune_blaster:	incbin	"roms/arcade/xevious-out/sound/xevi_blaster.bin"
