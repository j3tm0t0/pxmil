; sndtest.asm - sndplay.inc 試聴・サイクル計測(独立, emmscroll64.asm 非依存)。
;   BGM を再生しつつ 10種の効果音を順に鳴らす。snd_tick 最大サイクルを probe 出力。
;   ヘッドレスは連続実行でないとスロットルが効かず音声検証不可のため無限ループ。
;   ビルド: python3 tools/xevi_sound.py (先にデータ生成)
;           sjasmplus --raw=sndtest.bin tools/sndtest.asm -I tools -I .
;           python3 tools/mkx1disk.py sndtest.bin -o SNDTEST.2d -n SNDTEST --load 0x0100
;   実行:   XMIL_PROBE=1 XMIL_ROM_TYPE=3 ./xmilsdl2 SNDTEST.2d  (XMIL_WAVOUTで録音可)

	DEVICE	NONE
	ORG	0x0100

start:
	di
	ld	sp, 0xF000
	ld	hl, 0
	ld	(max_cyc), hl
	call	snd_init
	call	start_bgm
	xor	a
	ld	(se_id), a
	ld	a, 90
	ld	(se_timer), a
.loop:
	call	frame_delay
	; snd_tick サイクル計測
	call	cyc_read
	ld	(cyc_start), hl
	call	snd_tick
	call	cyc_read
	ld	de, (cyc_start)
	or	a
	sbc	hl, de
	ld	(cur_delta), hl
	ld	de, (max_cyc)
	ld	hl, (cur_delta)
	or	a
	sbc	hl, de
	jr	c, .nomax
	ld	hl, (cur_delta)
	ld	(max_cyc), hl
.nomax:
	; 一定間隔で効果音トリガ(10種を巡回)
	ld	a, (se_timer)
	dec	a
	ld	(se_timer), a
	jr	nz, .nose
	ld	a, 90
	ld	(se_timer), a
	ld	a, (se_id)		; id -> SEデータポインタ
	add	a, a
	ld	l, a
	ld	h, 0
	ld	de, se_table
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ex	de, hl			; HL = SEデータ
	call	snd_play_se
	ld	a, (se_id)
	inc	a
	cp	10
	jr	c, .idok
	xor	a
.idok:
	ld	(se_id), a
	ld	de, (max_cyc)		; SEトリガ毎に最大 snd_tick サイクル出力
	call	probe16
.nose:
	; BGM 終了で再生
	call	snd_bgm_active
	or	a
	jr	nz, .loop
	call	start_bgm
	jr	.loop

start_bgm:
	ld	ix, bgm_data
	call	snd_play_bgm
	ret

cyc_read:
	ld	bc, 0x00FC
	in	a, (c)
	ld	l, a
	ld	bc, 0x00FD
	in	a, (c)
	ld	h, a
	ret

frame_delay:
	ld	hl, 0x0A00
.fd:
	dec	hl
	ld	a, h
	or	l
	jr	nz, .fd
	ret

probe16:
	ld	bc, 0x00FE
	out	(c), e
	inc	c
	out	(c), d
	ret

	INCLUDE	"sndplay.inc"

se_table:
	dw	se00, se01, se02, se03, se04, se05, se06, se07, se08, se09

bgm_data:	incbin	"roms/arcade/xevious-out/sound/xevi_bgm.bin"
se00:	incbin	"roms/arcade/xevious-out/sound/se_00_zapper.bin"
se01:	incbin	"roms/arcade/xevious-out/sound/se_01_blaster.bin"
se02:	incbin	"roms/arcade/xevious-out/sound/se_02_flyhit.bin"
se03:	incbin	"roms/arcade/xevious-out/sound/se_03_teleport.bin"
se04:	incbin	"roms/arcade/xevious-out/sound/se_04_oneup.bin"
se05:	incbin	"roms/arcade/xevious-out/sound/se_05_bonus.bin"
	IFDEF	SFX2CH
se06:	incbin	"roms/arcade/xevious-out/sound/se_06_bacura_2ch.bin"
	ELSE
se06:	incbin	"roms/arcade/xevious-out/sound/se_06_bacura.bin"
	ENDIF
se07:	incbin	"roms/arcade/xevious-out/sound/se_07_exp_aerial.bin"
se08:	incbin	"roms/arcade/xevious-out/sound/se_08_exp_ground.bin"
se09:	incbin	"roms/arcade/xevious-out/sound/se_09_exp_solvalou.bin"

cyc_start:	dw 0
cur_delta:	dw 0
max_cyc:	dw 0
se_id:		db 0
se_timer:	db 0
