; emmscroll64.asm - X1turboZ 15kHz 320x200 64色 2ページ 4px スムーズ横スクロール。
;   emmscroll.asm (8色4ページ2px) の 64色・2ページ版 (xevi-extract 単独作成)。
;
; ■ 64色 = 6プレーン (bank0 B/R/G + bank1 B/R/G)。320x200=1000セル、プレーン16KB=
;   2048セルなので、1プレーン内に窓を2つ (POS=coarse / POS=coarse+1024) 取れる。
;   2窓 = 2ページ = shift 0px / 4px。CRTC POS(R12/R13) で表示窓を切替。
;     page0: POS=coarse        shift 0px
;     page1: POS=coarse+1024    shift 4px
;   粗 (8px) = coarse を +1。 => 4px 単位スクロール。
;   表示は常に両バンク(DISPVRAM=0 固定, banktbl[0])。書込は ACCESSVRAM で
;   bank0/bank1 を切替 (非表示窓の cell へ書くので表示は乱れない)。
;
; ■ 速度: アーケード準拠 0.5px/frame (解析: 0x8010 を毎フレーム-16 = 1タイル/16frame
;   = 0.5px/frame)。4px 単位なので adv = framecnt>>3 (8フレームに1回 4px 進む)。
;   新規列の展開(1200B)は adv 変化時のみ (8フレームに1回) 行う。
;
; ■ タイルID方式 (xevi2x1_64.py): ID=タイル絶対RAMアドレス(tilebase+idx*48)。
;   ユニークタイル=6プレーン48バイト。ID列を EMM へ置き、毎 adv で新規列 wc/wc+1 の
;   ID列(各50B)を DMA で RAM へ読み、shl[A]|shr[B] で展開時シフト合成して GRAM へ。
;
; ビルド:
;   python3 -P tools/xevi2x1_64.py --area 1 --emit
;   sjasmplus --raw=emmscroll64.bin tools/emmscroll64.asm
;   python3 tools/mkx1disk.py emmscroll64.bin -o roms/XEV64.2d -n XEV64 --load 0x0100
;   (RT=3: xmil.cfg に [Xmillennium]/IPL_TYPE=3)
;
; M4: アクティブ表示中の idle ループ回数 (= 空き容量) を 0x00FE/0x00FF へ probe 出力。
;     VBLANK 取りこぼし検出: 作業が VBLANK を超えた回数を dropped に数える。

GRAM_B		EQU	0x4000
GRAM_R		EQU	0x8000
GRAM_G		EQU	0xC000
CRTC_REG	EQU	0x1800
CRTC_VAL	EQU	0x1801
PORT_SCRN	EQU	0x1FD0
PORT_PPIB	EQU	0x1A01		; bit7=DISP
PORT_PPIC	EQU	0x1A02		; bit6: 1=width40
PORT_EXTPAL	EQU	0x1FB0
PORT_EXTTDISP	EQU	0x1FC0
PORT_EXTGPAL	EQU	0x1FC5
TVRAM		EQU	0x3000
TATTR		EQU	0x2000

SCRN_15K	EQU	0x02		; 15kHz 200line, DISPVRAM=0 ACCESS=0
SCRN_ACC1	EQU	0x12		; + ACCESSVRAM=1

COLS		EQU	40
ROWS		EQU	25
OFF1024		EQU	1024
W_CELLS		EQU	256
TILEBYTES	EQU	48		; 6プレーン x 8ラスタ
IDBYTES		EQU	2
COLIDS		EQU	ROWS * IDBYTES	; 50
TILEBASE	EQU	0x0103
SPEED		EQU	3		; adv = framecnt >> SPEED (8frame=4px => 0.5px/frame)

; EMM
EMM_A0		EQU	0x0D00
EMM_DAT		EQU	0x0D03

; 作業領域 (0xC000〜)
SHLTAB		EQU	0xC000		; 2ページ: 0xC000(np0) 0xC100(np1)
SHRTAB		EQU	0xC200		; 0xC200 0xC300
TBUF		EQU	0xC400		; compose 48B 作業 (未使用: COLBUF へ直接)
IDBUFA		EQU	0xC500		; 50B
IDBUFB		EQU	0xC560		; 50B
COLBUF		EQU	0xC600		; 25タイル x 48B = 1200B (〜0xCAB0)

; === 自機(M6) ship.inc 用の設定 (IFDEF SHIP。64色デフォルトビルドは不変) ===
	IFDEF	SHIP
SCRN_PCG	EQU	0x20		; 0x1FD0 PCGMODE
PCG_B		EQU	0x1500		; PCG 定義ポート
PCG_R		EQU	0x1600
PCG_G		EQU	0x1700
PCG_DEFCELL	EQU	0x07FF
PORT_PSGREG	EQU	0x1C00		; ジョイスティック
PORT_PSGDAT	EQU	0x1B00
PORT_TPAL	EQU	0x1FB9		; テキストパレット先頭 (tc1..tc7 = 0x1FB9..0x1FBF)
; PCG の色は per-pixel: 有効プレーン(atr&7 のマスク)のビット合成 tc(0..7) が
; テキストパレットのスロット番号になる(xevi-extract 説明)。現機体は 2色:
;   後方ボディ=R+G → tc6 → slot6(白)、前方ノーズ=R のみ → tc2 → slot2(赤)。
; 全プレーン有効にして各ピクセルの 3bit をスロット番号に使う。色は A案 7色
; (tpal_a)をテキストパレット slot1..7 に設定。
SHIP_PLANES	EQU	0x07		; 全プレーン有効 (per-pixel でスロット選択=多色)
SHIP_ATR	EQU	0x20 | SHIP_PLANES	; PCG + プレーン有効化マスク (=0x27)
SHIP_HX0	EQU	18 * 4
SHIP_VY0	EQU	12 * 4
SHIPGEN		EQU	0xCB00		; COLBUF(〜0xCAB0)の後の空き RAM (216B 〜0xCBD8)
SHIP_SCRN_BASE	EQU	SCRN_15K	; ship_init の PCGMODE 書込は 15kHz を保つ
SPRGEN		EQU	0xCBE0		; スプライト生成バッファ 8バイト (SHIPGEN後)
	ENDIF

; PORT_SCRN 書込マクロ: SHIP 時は PCGMODE 付き(scrn_out)、非SHIP は従来通り
; (非SHIP 展開は元コードとバイト一致)。
	MACRO	SCRNSET val
	IFDEF	SHIP
	ld	a, val
	call	scrn_out
	ELSE
	ld	bc, PORT_SCRN
	ld	a, val
	out	(c), a
	ENDIF
	ENDM

	DEVICE	NOSLOT64K
	ORG	0x0100

start:				; exec=0x0100
	jp	realstart
tiletbl:			; 0x0103
	incbin	"roms/xtiles64.bin"
xmapdata:
	incbin	"roms/xtilemap64.bin"
xpaldata:
	incbin	"roms/xpal64.bin"

realstart:
	di
	ld	sp, 0xF000
	call	init_screen
	call	clear_tvram
	call	setup_turboz64
	call	load_palette64
	; (以前はここで blackctrl(0x1FE0)を叩いて palandply を立てる回避が必要だった。
	;  xmil 本体の修正 = アナログパレット(grph4096)書込で crtc.e.palandply=1 を立てる
	;  により不要になった。実機でも書込んだ色は即反映されるのでこれが正しい挙動。)
	call	fill_emm_map
	call	build_tables
	call	prefill

	IFDEF	SHIP
	; --- 自機(オリジナル機体)を定義・初期化 + テキストパレット A案 7色 ---
	call	ship_init
	ld	hl, tpal_a		; slot1..7 → port 0x1FB9..0x1FBF
	ld	bc, PORT_TPAL
	ld	d, 7
.setpal:
	ld	a, (hl)
	out	(c), a
	inc	hl
	inc	c			; 次スロットの port (下位+1)
	dec	d
	jr	nz, .setpal
	call	sprite_init		; M7: 弾/敵/爆発の PCG 生成 + テーブル初期化
	ENDIF

	ld	hl, 0
	ld	(framecnt), hl
	ld	(vbl_seen), hl
	ld	hl, 0xFFFF
	ld	(last_adv), hl		; 強制再描画
	xor	a
	ld	(prev_disp), a

	IFDEF	MEAS
; [MEAS] ルーチンの Z80 サイクル数を計測。IN 0x00FC/0x00FD = サイクルカウンタ
;   (エミュの CPU_CLOCKCOUNT)。前後で読み差分を var に格納。差分には呼び出し
;   オーバヘッド(in/push/call/ret)も含むので meas_base を別途測り減算する。
	MACRO	MEASCALL rout, var
	ld	bc, 0x00FC
	in	l, (c)			; before lo (hi をラッチ)
	ld	bc, 0x00FD
	in	h, (c)			; before hi
	push	hl
	call	rout
	ld	bc, 0x00FC
	in	e, (c)			; after lo
	ld	bc, 0x00FD
	in	d, (c)			; after hi
	pop	hl
	ex	de, hl			; hl=after, de=before
	or	a
	sbc	hl, de			; hl = after - before (16bit mod)
	ld	(var), hl
	ENDM
	ENDIF

mainloop:
	call	wait_vblank
	; adv = framecnt >> SPEED
	ld	hl, (framecnt)
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l			; hl = f>>3  (SPEED=3)
	; adv 変化?
	ld	de, (last_adv)
	ld	a, l
	cp	e
	jr	nz, .changed
	ld	a, h
	cp	d
	jr	z, .nowork
.changed:
	ld	(last_adv), hl
	; phase=adv&1, coarse=adv>>1
	ld	a, l
	and	1
	ld	(phase), a
	srl	h
	rr	l
	ld	(coarse), hl
	; np=(adv+1)&1, coarsen=(adv+1)>>1
	ld	hl, (last_adv)
	inc	hl
	ld	a, l
	and	1
	ld	(npage), a
	srl	h
	rr	l
	ld	(coarsen), hl
	; 表示: SCRN=15kHz(DISPVRAM0,ACCESS0)
	SCRNSET	SCRN_15K
	; POS = (coarse + (phase?1024:0)) & 0x7FF
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
	ld	(cur_pos), hl		; 自機オーバーレイ用に表示 POS を記録
	ENDIF
	call	setpos
	IFDEF	SHIP
	call	ship_update		; ジョイスティック移動 + 自機描画 (vblank 中)
	call	sprite_update		; M7: 弾の発射/移動/描画
	ENDIF
	IFDEF	SPR_FRAMESEP
	xor	a			; [案4] サイクル内フレーム番号を 0(step)にリセット
	ld	(cyc_f), a
	ENDIF
	; 4MHz 取りこぼし対策: スクロールステップフレームでは重いエンジン処理
	; (start_redraw の DMA + do_redraw_chunk の列展開) を走らせない。
	; sr_pending を立てて次の非ステップフレームへ回し、chunk は非ステップ
	; 7 フレームに配分する (CHUNK=4 * 7 = 28 >= 25)。自機/弾/敵の描画と
	; 重ならないようにして最悪フレームの余裕を確保する。
	ld	a, 1
	ld	(sr_pending), a
	IFDEF	MEAS
	jp	.framesync		; MEAS 時は .nowork 肥大で jr 範囲外のため jp
	ELSE
	IFDEF	SPR_FRAMESEP
	jp	.framesync		; 案4 も .nowork 肥大のため jp
	ELSE
	jr	.framesync
	ENDIF
	ENDIF
.nowork:
	IFDEF	SPR_FRAMESEP
	; [案4] 非ステップ7フレームを chunk 専用(5)と sprite 専用(2)に分離。
	;   cyc_f=3,6 を sprite フレーム、残り(1,2,4,5,7)を chunk フレームに。
	;   1フレームに chunk か sprite の片方だけ載せ 4MHz で両方とも予算内に。
	;   sprite は step(0)+3+6 = 8フレーム中3回 ≒ 22.5Hz。CHUNK=5 必須。
	ld	a, (cyc_f)
	inc	a
	ld	(cyc_f), a
	cp	3
	jp	z, .spr_frame
	cp	6
	jp	z, .spr_frame
	ENDIF
	IFDEF	SHIP
	IFDEF	SPR_SMOOTH
	; [M8] -DSPR_SMOOTH 時のみ: スプライトを毎フレーム更新 (ステップ=.changed
	; 側で済、非ステップ=ここでも更新)。自機/弾が 8フレーム刻みでなく毎フレーム
	; 動き滑らかになる。ただし非ステップは chunk(列展開)が走るため、毎フレーム
	; 描画を足すと 4MHz では予算超過する(8MHz 専用)。既定(ガード無し)は 8フレーム
	; 刻み=4MHz クリーン。start_redraw/chunk より前=VBLANK 直後に表示窓へ書く。
	IFDEF	MEAS
	MEASCALL meas_empty, meas_base
	MEASCALL ship_update, meas_ship
	MEASCALL sprite_update, meas_spr
	ELSE
	call	ship_update
	call	sprite_update
	ENDIF
	ENDIF
	ENDIF
	ld	a, (sr_pending)		; 非ステップ: 保留の start_redraw を1回だけ
	or	a
	IFDEF	MEAS
	jp	z, .dochunk		; MEAS 時は MEASCALL 展開で jr が範囲外になるため jp
	ELSE
	jr	z, .dochunk
	ENDIF
	xor	a
	ld	(sr_pending), a
	IFDEF	MEAS
	MEASCALL start_redraw, meas_sr
	ELSE
	call	start_redraw		; np col39 の再描画を開始 (DMA+合成準備)
	ENDIF
.dochunk:
	IFDEF	MEAS
	MEASCALL do_redraw_chunk, meas_chunk
	ELSE
	call	do_redraw_chunk		; K 行ずつ展開 (非ステップ7フレームに配分)
	ENDIF
	IFDEF	SPR_FRAMESEP
	jp	.framesync
.spr_frame:				; [案4] sprite 専用フレーム(chunk を載せない)
	call	ship_update
	call	sprite_update
	ENDIF
.framesync:
	; --- フレーム同期 + 取りこぼし(スリップ)検出 ---
	; VBLANK は極小で再描画がアクティブ表示に食い込むのは正常(非表示窓へ書く)。
	; 真の取りこぼし = 1反復の作業が1フレームを超え VBLANK を跨ぐこと。
	; VBLANK(DISP 1->0)エッジ総数 vbl_seen を数え、反復数 framecnt と比較。
	; 1反復=1エッジ(spin の end-active)が正常。chunk 内で余計なエッジを拾えば slip。
	call	wait_active		; DISP=1 (アクティブ開始) まで
	ld	de, 0
	ld	bc, PORT_PPIB
.spin:	in	a, (c)
	add	a, a
	jr	nc, .spindone
	inc	de
	jr	.spin
.spindone:
	ld	(idlecnt), de
	; spin 脱出 = この表示フレーム末尾の VBLANK(1->0)
	ld	hl, (vbl_seen)
	inc	hl
	ld	(vbl_seen), hl
	xor	a
	ld	(prev_disp), a		; DISP=0
	; framecnt++
	ld	hl, (framecnt)
	inc	hl
	ld	(framecnt), hl
	IFDEF	MEAS
	; [MEAS] 各ルーチンのサイクル数を OUT (base, ship, spr, chunk, sr の順)。
	;   実コストは (値 - meas_base)。走らなかったフレームは 0 付近。
	ld	hl, (meas_base)
	ld	bc, 0x00FE
	out	(c), l
	ld	bc, 0x00FF
	out	(c), h
	ld	hl, (meas_ship)
	ld	bc, 0x00FE
	out	(c), l
	ld	bc, 0x00FF
	out	(c), h
	ld	hl, (meas_spr)
	ld	bc, 0x00FE
	out	(c), l
	ld	bc, 0x00FF
	out	(c), h
	ld	hl, (meas_chunk)
	ld	bc, 0x00FE
	out	(c), l
	ld	bc, 0x00FF
	out	(c), h
	ld	hl, (meas_sr)
	ld	bc, 0x00FE
	out	(c), l
	ld	bc, 0x00FF
	out	(c), h
	ELSE
	; dropped = vbl_seen - framecnt (= 跨いだ余分な VBLANK 数)
	ld	de, (vbl_seen)
	ex	de, hl
	or	a
	sbc	hl, de			; hl = vbl_seen - framecnt
	ld	bc, 0x00FE
	out	(c), l
	ld	bc, 0x00FF
	out	(c), h
	ENDIF
	jp	mainloop

	IFDEF	MEAS
meas_empty:
	ret				; 呼び出しオーバヘッド基準用
meas_base:	dw	0
meas_ship:	dw	0
meas_spr:	dw	0
meas_chunk:	dw	0
meas_sr:	dw	0
	ENDIF

;=====================================================================
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
; 次ページ np の col39 再描画を「開始」: wc/wc+1 の ID を DMA 読み、
; dc_base/シフト表を設定し、分散展開の状態をリセットする(展開は do_redraw_chunk)。
start_redraw:
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
	; dc_base = (coarsen + (np?1024:0) - 1) & 0x7FF
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
	ld	a, (npage)
	add	a, (SHLTAB >> 8)
	ld	(shl_hi), a
	ld	a, (npage)
	add	a, (SHRTAB >> 8)
	ld	(shr_hi), a
	; 分散展開を開始
	xor	a
	ld	(ec_row), a
	ld	a, 1
	ld	(rd_active), a
	ret

;=====================================================================
; do_redraw_chunk: rd_active 中、ec_row から最大 CHUNK 行を合成+散布する。
;   (負荷分散: 1列1200Bを数フレームに分けて展開 -> VBLANK 取りこぼし 0)
	IFNDEF	CHUNK
CHUNK		EQU	4		; -DCHUNK=n で上書き可(既定4で byte-identical)
	ENDIF
do_redraw_chunk:
	ld	a, (rd_active)
	or	a
	ret	z
	; chunk_start = ec_row
	ld	a, (ec_row)
	ld	(chunk_start), a
	; 合成: 最大 CHUNK 行を COLBUF へ
	ld	hl, COLBUF
	ld	(ec_dst), hl
	xor	a
	ld	(chunk_cnt), a
	ld	b, CHUNK
.cl:
	ld	a, (ec_row)
	cp	ROWS
	jr	z, .composed
	push	bc
	; tileA = IDBUFA[ec_row*2], tileB = IDBUFB[ec_row*2]
	ld	a, (ec_row)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, IDBUFA
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	(ec_ta), hl
	ld	a, (ec_row)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, IDBUFB
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	(ec_tb), hl
	call	compose48
	ld	a, (ec_row)
	inc	a
	ld	(ec_row), a
	ld	a, (chunk_cnt)
	inc	a
	ld	(chunk_cnt), a
	pop	bc
	; VBLANK エッジ監視 (重い展開中に VBLANK を跨いだら slip): DISP 1->0 で vbl_seen++
	push	bc
	ld	bc, PORT_PPIB
	in	a, (c)
	and	0x80			; now DISP
	pop	bc
	ld	e, a			; now
	ld	a, (prev_disp)
	ld	d, a			; prev
	ld	a, e
	ld	(prev_disp), a
	ld	a, e
	or	a
	jr	nz, .noedge		; now!=0
	ld	a, d
	or	a
	jr	z, .noedge		; prev=0
	ld	hl, (vbl_seen)
	inc	hl
	ld	(vbl_seen), hl
.noedge:
	djnz	.cl
.composed:
	; 散布: chunk_cnt 行 (chunk_start から) を bank0/bank1 へ
	call	scatter_chunk
	; 全行完了なら rd_active=0
	ld	a, (ec_row)
	cp	ROWS
	ret	nz
	xor	a
	ld	(rd_active), a
	ret

; scatter_chunk: COLBUF[0..chunk_cnt*48) を、行 chunk_start.. の cell へ。
;   cell = (dc_base + 40*(row+1)) & 0x7FF。bank0=COLBUF+0, bank1=COLBUF+24。
scatter_chunk:
	; bank0
	SCRNSET	SCRN_15K
	ld	a, 0
	ld	(sc_half), a
	call	sc_pass
	; bank1
	SCRNSET	SCRN_ACC1
	ld	a, 24
	ld	(sc_half), a
	call	sc_pass
	SCRNSET	SCRN_15K
	ret

; sc_pass: (sc_half)=0/24。chunk_cnt 行。src=COLBUF+row_in_chunk*48+sc_half。
sc_pass:
	ld	a, (chunk_cnt)
	or	a
	ret	z
	ld	(sc_row), a		; 残り行数
	; sc_src = COLBUF + sc_half
	ld	hl, COLBUF
	ld	a, (sc_half)
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	(sc_src), hl
	; cell_off = (chunk_start+1) * 40
	ld	a, (chunk_start)
	inc	a
	ld	l, a
	ld	h, 0
	call	mul_hl_40
	ld	(sc_off), hl
.sr:
	; cell = (dc_base + sc_off) & 0x7FF
	ld	hl, (sc_off)
	ld	de, (dc_base)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(sc_cell), hl
	; B
	ld	hl, (sc_src)
	ex	de, hl
	ld	hl, (sc_cell)
	ld	a, h
	or	(GRAM_B >> 8)
	ld	b, a
	ld	c, l
	call	wr8
	; R
	ld	hl, (sc_cell)
	ld	a, h
	or	(GRAM_R >> 8)
	ld	b, a
	ld	c, l
	call	wr8
	; G
	ld	hl, (sc_cell)
	ld	a, h
	or	(GRAM_G >> 8)
	ld	b, a
	ld	c, l
	call	wr8
	; 次行: sc_src += 48, sc_off += 40
	ld	hl, (sc_src)
	ld	de, TILEBYTES
	add	hl, de
	ld	(sc_src), hl
	ld	hl, (sc_off)
	ld	de, COLS
	add	hl, de
	ld	(sc_off), hl
	ld	a, (sc_row)
	dec	a
	ld	(sc_row), a
	jr	nz, .sr
	ret

; hl = hl * 40 (bc破壊)。小さい値用。
mul_hl_40:
	ld	d, h
	ld	e, l
	add	hl, hl			; *2
	add	hl, hl			; *4
	add	hl, de			; *5
	add	hl, hl			; *10
	add	hl, hl			; *20
	add	hl, hl			; *40
	ret

;=====================================================================
; ID列読み込み: e=col(0..255), hl=dst(IDBUFA/B)。EMM addr=col*50 で 50B DMA。
read_ids:
	ld	(dma_id_dst), hl
	ld	h, 0
	ld	l, e
	ld	b, h
	ld	c, l			; bc=col
	add	hl, hl
	add	hl, hl
	add	hl, hl			; *8
	ld	d, h
	ld	e, l
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
; 列展開: 25タイルを COLBUF へ合成し, bank0/bank1 の2パスで GRAM へ scatter。
expand_col:
	; --- pass compose: COLBUF[row*48..] = shl[A]|shr[B] ---
	ld	a, 0
	ld	(ec_row), a
	ld	hl, COLBUF
	ld	(ec_dst), hl
.crow:
	; tileA addr
	ld	a, (ec_row)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, IDBUFA
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	(ec_ta), hl
	ld	a, (ec_row)
	add	a, a
	ld	e, a
	ld	d, 0
	ld	hl, IDBUFB
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	(ec_tb), hl
	call	compose48
	ld	a, (ec_row)
	inc	a
	ld	(ec_row), a
	cp	ROWS
	jr	nz, .crow
	; --- pass scatter bank0 (ACCESS=0): COLBUF+0..23 ---
	SCRNSET	SCRN_15K
	ld	a, 0
	ld	(sc_half), a
	call	scatter_half
	; --- pass scatter bank1 (ACCESS=1): COLBUF+24..47 ---
	SCRNSET	SCRN_ACC1
	ld	a, 24
	ld	(sc_half), a
	call	scatter_half
	; ACCESS=0 戻す
	SCRNSET	SCRN_15K
	ret

; compose48: ec_ta/ec_tb のタイル(各48B)を shl/shr 合成し (ec_dst) へ48B, ec_dst+=48。
compose48:
	ld	de, (ec_ta)
	ld	hl, (ec_dst)
	ld	b, TILEBYTES
.p1:
	ld	a, (de)
	inc	de
	push	hl
	ld	l, a
	ld	a, (shl_hi)
	ld	h, a
	ld	a, (hl)			; shl[A[i]]
	pop	hl
	ld	(hl), a
	inc	hl
	djnz	.p1
	; OR shr[B[i]]
	ld	de, (ec_tb)
	ld	hl, (ec_dst)
	ld	b, TILEBYTES
.p2:
	ld	a, (de)
	inc	de
	push	hl
	ld	l, a
	ld	a, (shr_hi)
	ld	h, a
	ld	a, (hl)			; shr[B[i]]
	pop	hl
	or	(hl)
	ld	(hl), a
	inc	hl
	djnz	.p2
	ld	hl, (ec_dst)
	ld	de, TILEBYTES
	add	hl, de
	ld	(ec_dst), hl
	ret

; scatter_half: (sc_half)=0(bank0) or 24(bank1)。25行、各 B/R/G 8ラスタ。
;   cell = (dc_base + 40*(row+1)) & 0x7FF。src = COLBUF + row*48 + sc_half (+0/8/16)。
scatter_half:
	ld	a, 0
	ld	(sc_row), a
	ld	hl, 0			; offset = 40*(row+1) 累積 (初期0, 毎行+40)
	ld	(sc_off), hl
	ld	hl, COLBUF
	ld	a, (sc_half)
	ld	e, a
	ld	d, 0
	add	hl, de
	ld	(sc_src), hl		; COLBUF + sc_half
.sr:
	; cell = (dc_base + sc_off + 40) & 0x7FF
	ld	hl, (sc_off)
	ld	de, COLS
	add	hl, de
	ld	(sc_off), hl
	ld	de, (dc_base)
	add	hl, de
	ld	a, h
	and	0x07
	ld	h, a
	ld	(sc_cell), hl
	; B plane
	ld	hl, (sc_src)
	ex	de, hl			; de=src
	ld	hl, (sc_cell)
	ld	a, h
	or	(GRAM_B >> 8)
	ld	b, a
	ld	c, l
	call	wr8			; de+=8
	; R plane (src = sc_src+8)
	ld	hl, (sc_cell)
	ld	a, h
	or	(GRAM_R >> 8)
	ld	b, a
	ld	c, l
	call	wr8
	; G plane (src = sc_src+16)
	ld	hl, (sc_cell)
	ld	a, h
	or	(GRAM_G >> 8)
	ld	b, a
	ld	c, l
	call	wr8
	; sc_src += 48 (次行)
	ld	hl, (sc_src)
	ld	de, TILEBYTES
	add	hl, de
	ld	(sc_src), hl
	ld	a, (sc_row)
	inc	a
	ld	(sc_row), a
	cp	ROWS
	jr	nz, .sr
	ret

; wr8: bc=port(b=high,c=low), de=src 8バイト。de+=8, b+=8/raster。
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

;=====================================================================
; シフト表 2ページ: shl[np][a]=(a<<4np)&0xFF, shr[np][a]=a>>(8-4np)。
;   np=0: shl=a, shr=0 (shift 0px)。 np=1: shl=a<<4, shr=a>>4 (shift 4px)。
build_tables:
	xor	a
	ld	(bt_np), a
.nl:
	ld	a, (bt_np)
	add	a, a
	add	a, a			; 4np
	ld	(bt_shl), a
	ld	b, a
	ld	a, 8
	sub	b
	ld	(bt_shr), a
	ld	a, (bt_np)
	add	a, (SHLTAB >> 8)
	ld	(bt_shlpg), a
	ld	a, (bt_np)
	add	a, (SHRTAB >> 8)
	ld	(bt_shrpg), a
	ld	c, 0
.bl:
	ld	a, (bt_shl)
	ld	b, a
	inc	b
	ld	a, c
	jr	.slt
.sll:	add	a, a
.slt:	dec	b
	jr	nz, .sll
	ld	e, a
	ld	a, (bt_shlpg)
	ld	h, a
	ld	l, c
	ld	(hl), e
	ld	a, (bt_shr)
	ld	b, a
	inc	b
	ld	a, c
	jr	.srt
.srl:	srl	a
.srt:	dec	b
	jr	nz, .srl
	ld	e, a
	ld	a, (bt_shrpg)
	ld	h, a
	ld	l, c
	ld	(hl), e
	inc	c
	jr	nz, .bl
	ld	a, (bt_np)
	inc	a
	ld	(bt_np), a
	cp	2
	jp	nz, .nl
	ret

;=====================================================================
; タイルID列を EMM addr 0 へ転送。
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
; 初期プリフィル: 2窓(page0,page1)の col0..39 を埋める。
prefill:
	xor	a
	ld	(pf_page), a
.pl:
	ld	a, (pf_page)
	ld	(npage), a		; redraw系の shift/offset に流用
	add	a, (SHLTAB >> 8)
	ld	(shl_hi), a
	ld	a, (pf_page)
	add	a, (SHRTAB >> 8)
	ld	(shr_hi), a
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
	; base = (pf_wc + (page?1024:0) - 40) & 0x7FF
	ld	hl, 0
	ld	a, (pf_page)
	and	1
	jr	z, .b0
	ld	hl, OFF1024
.b0:
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
	call	expand_col
	ld	a, (pf_wc)
	inc	a
	ld	(pf_wc), a
	cp	COLS
	jr	nz, .wl
	ld	a, (pf_page)
	inc	a
	ld	(pf_page), a
	cp	2
	jp	nz, .pl
	ret

;=====================================================================
wait_vblank:
	ld	bc, PORT_PPIB
.w:	in	a, (c)
	add	a, a
	jr	c, .w			; DISP=1 の間待つ
	ret

wait_active:
	ld	bc, PORT_PPIB
.w:	in	a, (c)
	add	a, a
	jr	nc, .w			; DISP=0 の間待つ
	ret

;=====================================================================
; turboZ 64色モード設定 (width40, 15kHz, AEN|64色, EXTGRPHPAL)
setup_turboz64:
	ld	bc, PORT_PPIC
	xor	a
	out	(c), a
	ld	a, 0x40
	out	(c), a			; width40
	SCRNSET	SCRN_15K
	ld	bc, PORT_EXTTDISP
	xor	a
	out	(c), a			; ZPRY=0
	ld	bc, PORT_EXTPAL
	ld	a, 0x90
	out	(c), a			; AEN|64色
	ld	bc, PORT_EXTGPAL
	ld	a, 0x80
	out	(c), a
	ret

; 64色パレット設定: xpaldata の 64エントリ [addr_lo,addr_hi,Bnib,Rnib,Gnib]。
load_palette64:
	ld	ix, xpaldata
	ld	a, 64
	ld	(palcnt), a
.lp:
	ld	e, (ix+0)
	ld	d, (ix+1)
	ld	a, e
	rrca
	rrca
	rrca
	rrca
	and	0x0F
	ld	c, a
	ld	a, d
	rlca
	rlca
	rlca
	rlca
	and	0xF0
	or	c
	ld	c, a
	ld	a, e
	and	0x0F
	rlca
	rlca
	rlca
	rlca
	ld	l, a
	ld	a, (ix+2)
	or	l
	ld	b, 0x10
	out	(c), a
	ld	a, (ix+3)
	or	l
	ld	b, 0x11
	out	(c), a
	ld	a, (ix+4)
	or	l
	ld	b, 0x12
	out	(c), a
	ld	de, 5
	add	ix, de
	ld	a, (palcnt)
	dec	a
	ld	(palcnt), a
	jr	nz, .lp
	ret

;=====================================================================
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
	ret

clear_tvram:
	ld	hl, 0
.c:	ld	a, h
	or	(TVRAM >> 8)
	ld	b, a
	ld	c, l
	xor	a
	out	(c), a
	ld	a, h
	or	(TATTR >> 8)
	ld	b, a
	xor	a
	out	(c), a
	inc	hl
	ld	a, h
	cp	0x08
	jr	nz, .c
	ret

;=====================================================================
crtc_tbl:
	db	0x37, 0x28, 0x2d, 0x34, 0x1f, 0x02, 0x19, 0x1c, 0x00
	db	0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

dma_id:
	db	0xC3
	db	0x7D
	db	LOW EMM_DAT, HIGH EMM_DAT
	db	LOW (COLIDS - 1), HIGH (COLIDS - 1)
	db	0x2C
	db	0x10
	db	0xAD
dma_id_dst:
	dw	0
	db	0x82
	db	0xCF
	db	0x87
dma_id_end:

; --- RAM 変数 ---
framecnt:	dw	0
last_adv:	dw	0
coarse:		dw	0
coarsen:	dw	0
phase:		db	0
npage:		db	0
wcol:		db	0
idlecnt:	dw	0
dropped:	dw	0
pf_page:	db	0
pf_wc:		db	0
dc_base:	dw	0
emm_a0:		db	0
emm_a1:		db	0
emm_a2:		db	0
shl_hi:		db	0
shr_hi:		db	0
ec_row:		db	0
ec_dst:		dw	0
ec_ta:		dw	0
ec_tb:		dw	0
bt_np:		db	0
bt_shl:		db	0
bt_shr:		db	0
bt_shlpg:	db	0
bt_shrpg:	db	0
sc_half:	db	0
sc_row:		db	0
sc_off:		dw	0
sc_src:		dw	0
sc_cell:	dw	0
palcnt:		db	0
rd_active:	db	0
chunk_start:	db	0
chunk_cnt:	db	0
vbl_seen:	dw	0		; 観測した VBLANK(DISP 1->0)エッジ総数
prev_disp:	db	0		; 前回ポーリング時の DISP(0x80/0)
sr_pending:	db	0		; start_redraw 保留フラグ(ステップフレームで立て翌フレーム実行)
	IFDEF	SPR_FRAMESEP
cyc_f:		db	0		; [案4] スクロール周期内フレーム番号(0=step,1..7)
	ENDIF

	IFDEF	SHIP
; テキストパレット A案 7色 (slot1..7 = port 0x1FB9..0x1FBF)。値=(G<<4)|(R<<2)|B。
; 淡黄白/赤/緑/橙/暗灰/白/淡青。自機は slot6(白,R+G) と slot2(赤,R)を使用。
; テキストパレット D案 7色 (slot1..7 = port 0x1FB9..0x1FBF)。値=(G<<4)|(R<<2)|B。
; 公式スロット割当(xevi-extract/team-lead 確定):
;   slot1=白0x3F slot2=明灰0x2A slot3=暗灰0x15 slot4=黒0x00 slot5=赤0x0C
;   slot6=青0x27 slot7=橙0x2D
; 自機: ボディ=slot1(白), ノーズ=slot5(赤)。敵=明灰/暗灰+赤, 弾=青/白。
tpal_a:	db	0x3F, 0x2A, 0x15, 0x00, 0x0C, 0x27, 0x2D
	INCLUDE	"ship.inc"		; 自機(M6)共通モジュール。cur_pos/ship_* 等を定義
	INCLUDE	"sprite.inc"		; M7 弾/敵/爆発 (ship.inc の後=read_joy等を使うため)
	ENDIF

	END
