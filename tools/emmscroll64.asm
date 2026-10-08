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
; [⑦ 全エリア化] EMM 常駐レイアウト(xevi-extract 案)。-DALLAREAS 時に FDC で展開。
EMM_PAL		EQU	0x000000	; common_pal 320B
EMM_TILES	EQU	0x001000	; common_tiles 44928B(48B/tile)
EMM_USED	EQU	0x010000	; +a*0x0800 : areaNN_used(先頭2B=n, +index×n)
EMM_USED_STR	EQU	0x0800
EMM_MAPS	EQU	0x020000	; +a*0x3200 : areaNN_map 12800B
EMM_MAPS_STR	EQU	0x3200
EMM_GOBJ	EQU	0x060000	; +a*0x0400 : areaNN_gobj
EMM_GOBJ_STR	EQU	0x0400
SPEED		EQU	3		; adv = framecnt >> SPEED (8frame=4px => 0.5px/frame)

; EMM
EMM_A0		EQU	0x0D00
EMM_DAT		EQU	0x0D03

; 作業領域 (0xC000〜)
; === 作業バッファを 0xCC00〜 へまとめて退避(旧 0xC000〜0xCBE8)。プログラムが肥大して
;     これらと重なるとスクロール書込がコード/データを破壊しハングするため、program が
;     0xCC00 まで伸ばせるよう上げた(敵32方向 step(d) で 0xC428 到達→旧0xC000台と衝突しハングした)。===
SHLTAB		EQU	0xCE00		; 2ページ: 0xCE00(np0) 0xCF00(np1) (旧0xC000)
SHRTAB		EQU	0xD000		; 0xD000 0xD100 (旧0xC200)
; TBUF(=CG_IDBUF 200B)/IDBUFA/IDBUFB は SPRGEN後の空き(0xCC00〜)へ移動。
;   以前は 0xC400-0xC5B0 にあり、プログラム肥大で 0xC400 を越えると CG_IDBUF(使用中)と
;   重なりコードを破壊→ハングした(敵32方向 step(d) で 0xC428 到達)。これで program は
;   次の使用中バッファ COLBUF(0xC600) まで伸ばせる。
TBUF		EQU	0xCC00		; CG_IDBUF 200B (〜0xCCC8) (旧0xC400)
IDBUFA		EQU	0xCD00		; 50B (旧0xC500)
IDBUFB		EQU	0xCD60		; 50B (旧0xC560)
COLBUF		EQU	0xD200		; 25タイル x 48B = 1200B (〜0xD6B0) (旧0xC600)
WORKBUF_BASE	EQU	0xCC00		; 作業バッファ最下位(=TBUF)。program 終端はここ未満であること(末尾ASSERT)
WORKBUF_TOP	EQU	0xD808		; 作業バッファ最上位(=SPRGEN+8)。スタック(0xF000)まで余裕

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
SHIPGEN		EQU	0xD700		; COLBUF(〜0xD6B0)の後の空き RAM (216B 〜0xD7D8) (旧0xCB00)
SHIP_SCRN_BASE	EQU	SCRN_15K	; ship_init の PCGMODE 書込は 15kHz を保つ
SPRGEN		EQU	0xD800		; スプライト生成バッファ 8バイト (SHIPGEN後) (旧0xCBE0)
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
	; -DGOBJ_TILEMAP で地上物を焼き込んだタイル表/マップ(xevi の roms/ データ)を
	;   使う。データは gitignore の roms/ 配下(当方は ID を読んで展開するのみ)。
	IFDEF	GOBJ_TILEMAP
	incbin	"roms/arcade/xevious-out/xtiles64_obj.bin"
	ELSE
	incbin	"roms/xtiles64.bin"
	ENDIF
xmapdata:
	IFDEF	GOBJ_TILEMAP
	incbin	"roms/arcade/xevious-out/xtilemap64_obj.bin"
	ELSE
	incbin	"roms/xtilemap64.bin"
	ENDIF
xpaldata:
	; GOBJ_TILEMAP(焼込地上物/Sol)時は地上物の赤/灰を含む 22色拡張パレット。
	; 先頭17色は xpal64.bin と同一で地形互換(xevi-extract)。
	IFDEF	GOBJ_TILEMAP
	incbin	"roms/arcade/xevious-out/xpal64_obj.bin"
	ELSE
	incbin	"roms/xpal64.bin"
	ENDIF

realstart:
	di
	ld	sp, 0xF000
	call	init_screen
	call	clear_tvram
	call	setup_turboz64
	IFNDEF	ALLAREAS
	ld	ix, xpaldata		; 単一エリア: incbin パレット
	call	load_palette64
	ENDIF
	; (以前はここで blackctrl(0x1FE0)を叩いて palandply を立てる回避が必要だった。
	;  xmil 本体の修正 = アナログパレット(grph4096)書込で crtc.e.palandply=1 を立てる
	;  により不要になった。実機でも書込んだ色は即反映されるのでこれが正しい挙動。)
	IFDEF	ALLAREAS
	call	allarea_load		; [⑦] FDC で全エリアデータを EMM へ展開
	IFDEF	DMPMAPS
	call	dump_all_maps		; [検証] 展開後の全16マップを OUT 0xFB で生ダンプ(ゲームは起動しない)
	ENDIF
	xor	a			; [⑦(2)] area1(index0)の タイルを RAM タイル表へ集約
	call	area_switch
	xor	a			; area1 の gobj/sol/grobda を RAM へ(以降 sprite_init の grobda_init が使う)
	call	gobj_load
	IFDEF	ENEMY_EXTDATA
	call	fly_load		; [出現②] area1 の flying 出現コマンド列をパース
	ENDIF
	; [⑦(3)] common_pal(EMM_PAL)を pal_buf へ読み HW パレット適用
	xor	a
	ld	(emm_a0), a
	ld	(emm_a1), a
	ld	(emm_a2), a
	call	set_emm_addr
	ld	hl, pal_buf
	ld	bc, EMM_DAT
	ld	de, 320
.cp_rd:	in	a, (c)
	ld	(hl), a
	inc	hl
	dec	de
	ld	a, d
	or	e
	jr	nz, .cp_rd
	ld	ix, pal_buf
	call	load_palette64
	; [⑦(4)] blackpal 生成: addr 2B を残し色 3B(B,R,G)を 0 に(切替暗転用)
	ld	hl, pal_buf
	ld	de, blackpal
	ld	b, 64
.bpgen:	ld	a, (hl)
	ld	(de), a
	inc	hl
	inc	de			; addr lo
	ld	a, (hl)
	ld	(de), a
	inc	hl
	inc	de			; addr hi
	xor	a
	ld	(de), a
	inc	de
	inc	hl			; B=0
	ld	(de), a
	inc	de
	inc	hl			; R=0
	ld	(de), a
	inc	de
	inc	hl			; G=0
	djnz	.bpgen
	IFDEF	ALLAREAS_DBG
	; 検証(2): RAM タイル表先頭(TILEBASE)の2バイトを PROBE(0xC8:byte)。
	ld	a, (TILEBASE + 0)
	ld	e, a
	ld	bc, 0x00FE
	out	(c), e
	ld	a, 0xC8
	ld	bc, 0x00FF
	out	(c), a
	ld	a, (TILEBASE + 1)
	ld	e, a
	ld	bc, 0x00FE
	out	(c), e
	ld	a, 0xC9
	ld	bc, 0x00FF
	out	(c), a
	; 検証: EMM_MAPS(area01_map)先頭2バイトを読み PROBE(0xC0:byte)で出力。
	ld	a, EMM_MAPS & 0xFF
	ld	(emm_a0), a
	ld	a, (EMM_MAPS >> 8) & 0xFF
	ld	(emm_a1), a
	ld	a, EMM_MAPS >> 16
	ld	(emm_a2), a
	call	set_emm_addr
	ld	bc, EMM_DAT
	in	a, (c)			; EMM_MAPS[0] (=0x03 期待)
	ld	e, a
	ld	bc, 0x00FE
	out	(c), e
	ld	a, 0xC0
	ld	bc, 0x00FF
	out	(c), a			; PROBE 0xC0xx
	ld	bc, EMM_DAT
	in	a, (c)			; EMM_MAPS[1] (=0x01 期待)
	ld	e, a
	ld	bc, 0x00FE
	out	(c), e
	ld	a, 0xC1
	ld	bc, 0x00FF
	out	(c), a			; PROBE 0xC1xx
	ENDIF
	ENDIF
	IFNDEF	ALLAREAS
	call	fill_emm_map		; 単一エリア: map を EMM addr0 へ。全エリアは EMM_MAPS 常駐。
	ENDIF
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
	IFDEF	SOUND
	call	snd_init		; [SND] PSG 無音化 + サウンド初期化
	; BGM は snd_bgm_mgr が駆動(bgm_silent=1 起動 → 1フレーム目で opening 開始)。
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

; [既定昇格] スプライトは既定で「毎フレーム更新(SPR_SMOOTH)+表示窓のみ描画
;   (SPR_1WIN)」= 60Hz 滑らか・4MHz dropped=0(chunk 高速化後)。
;   -DSPR_EVERY8: 旧方式(8フレーム刻み・2窓, 6f25b3f 相当。guard 残置)。
;   -DSPR_FRAMESEP: 案4(chunk/sprite フレーム分離, 約22Hz)。
	IFDEF	SHIP
	IFNDEF	SPR_EVERY8
	IFNDEF	SPR_FRAMESEP
	IFNDEF	SPR_SMOOTH
	DEFINE	SPR_SMOOTH
	ENDIF
	ENDIF
	IFNDEF	SPR_1WIN
	DEFINE	SPR_1WIN
	ENDIF
	ENDIF
	ENDIF

mainloop:
	call	wait_vblank
	IFDEF	SOUND
	call	snd_bgm_mgr		; [SND] BGM 終了検出→次曲(fanfare→本BGM→ループ)
	IFDEF	MEAS
	MEASCALL snd_tick, meas_snd
	ELSE
	call	snd_tick		; [SND] 毎フレーム更新(VBLANK 直後, 最悪~4048T)
	ENDIF
	ENDIF
	; adv = framecnt >> SPEED
.adv_recalc:				; [⑦(4)] エリア切替後の adv 再計算入口
	ld	hl, (framecnt)
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l			; hl = f>>3  (SPEED=3)
	IFDEF	ALLAREAS
	ld	de, (adv_off)		; [⑦(4)] エリア先頭からの相対 adv
	or	a
	sbc	hl, de
	ld	a, h
	and	0x1F			; & 0x1FFF (framecnt wrap 許容を維持)
	ld	h, a
	ENDIF
	; adv 変化?
	ld	de, (last_adv)
	ld	a, l
	cp	e
	jr	nz, .changed
	ld	a, h
	cp	d
	jp	z, .nowork
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
	IFDEF	ALLAREAS_DBG
	ld	a, (coarse)		; PROBE 0xB0<coarse_lo>
	ld	e, a
	ld	bc, 0x00FE
	out	(c), e
	ld	a, 0xB0
	ld	bc, 0x00FF
	out	(c), a
	ENDIF
	IFDEF	ALLAREAS
	; [⑦(4)] coarsen >= W_CELLS-COLS+1(=217) でエリア境界 → 切替
	ld	a, h
	or	a
	jr	nz, .area_adv
	ld	a, l
	cp	W_CELLS - COLS + 1
	jr	c, .no_area_adv
.area_adv:
	call	area_advance		; 暗転+タイル/マップ/gobj切替+adv_off reset+prefill+トランジェント取消
	jp	.adv_recalc		; 新 adv_off(=0 相対)で coarse/coarsen 再計算
.no_area_adv:
	ENDIF
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
	IFDEF	ALLAREAS
	; [⑦(4)] 切替暗転の復帰: blank_ctr を減算し 0 でパレット復帰(setpos 後なので新窓が見える)
	ld	a, (blank_ctr)
	or	a
	jr	z, .nblank
	dec	a
	ld	(blank_ctr), a
	jr	nz, .nblank
	ld	ix, pal_buf
	call	load_palette64
	IFDEF	ALLAREAS_DBG
	ld	a, 0xFF			; PROBE 0xA3FF: pal_buf 復帰
	ld	e, a
	ld	bc, 0x00FE
	out	(c), e
	ld	a, 0xA3
	ld	bc, 0x00FF
	out	(c), a
	ENDIF
.nblank:
	ENDIF
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
	IFDEF	SHIP
	IFDEF	GOBJ_TILEMAP
	call	sol_update		; [#3] Sol せり上がりアニメ(発動中のみ)
	ENDIF
	IFDEF	MEAS
	MEASCALL crater_pump, meas_crp
	ELSE
	call	crater_pump		; [#2] 保留クレーターを1ユニット/フレームで分散描画
	ENDIF
	ENDIF
	; framecnt++
	ld	hl, (framecnt)
	inc	hl
	ld	(framecnt), hl
	IFDEF	CRATER_SELFTEST
	; 決定論テスト: framecnt==CST_FRAME で gobj_list[0](col211,row14)に crater を1回強制。
	IFNDEF	CST_FRAME
CST_FRAME	EQU	3040		; coarse=190 → col211 は画面 col21 に表示
	ENDIF
	ld	hl, (framecnt)
	ld	de, CST_FRAME
	or	a
	sbc	hl, de
	jr	nz, .cst_skip
	; gobj_list[0] に crater を強制(crater_gram の回帰テスト用)。
	ld	hl, gobj_list
	ld	(gc_ptr), hl
	call	crater_write_obj	; gc_col/gc_row を設定
	; col_ex = gc_col - coarse_lo (物体の画面列; crater_gram の dc_base が使う)
	ld	a, (gc_col)
	ld	hl, coarse
	sub	(hl)
	ld	(col_ex), a
	IFDEF	CST_ENQUEUE
	call	crater_enqueue		; 分散経路テスト(pump が数フレームで描く)
	ELSE
	call	crater_gram		; 同期経路テスト
	ENDIF
.cst_skip:
	ld	hl, (framecnt)		; dropped 計算が依存する HL=framecnt を復元
	ENDIF
	IFDEF	SOL_SELFTEST
	; 決定論テスト: framecnt==SOL_FRAME で Sol(col102,row2)命中を強制発動。
	IFNDEF	SOL_FRAME
SOL_FRAME	EQU	1280		; coarse≈79 → Sol は画面 col≈23 に表示
	ENDIF
	ld	hl, (framecnt)
	ld	de, SOL_FRAME
	or	a
	sbc	hl, de
	jr	nz, .sol_skip
	ld	a, SOL_COL
	ld	(hit_tcol), a
	ld	a, SOL_ROW
	ld	(col_ey), a
	call	sol_hit_check		; Sol 発動(以後 sol_update が f0→f3)
.sol_skip:
	ld	hl, (framecnt)		; HL 復元
	ENDIF
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
	ld	hl, (meas_bac)		; [MEAS] Bacura 描画 T 数(6番目)
	ld	bc, 0x00FE
	out	(c), l
	ld	bc, 0x00FF
	out	(c), h
	; [MEAS] 追加 6: snd, crp, enm, col, tur, ebul
	ld	hl, (meas_snd)  : ld bc,0x00FE : out (c),l : ld bc,0x00FF : out (c),h
	ld	hl, (meas_crp)  : ld bc,0x00FE : out (c),l : ld bc,0x00FF : out (c),h
	ld	hl, (meas_enm)  : ld bc,0x00FE : out (c),l : ld bc,0x00FF : out (c),h
	ld	hl, (meas_col)  : ld bc,0x00FE : out (c),l : ld bc,0x00FF : out (c),h
	ld	hl, (meas_tur)  : ld bc,0x00FE : out (c),l : ld bc,0x00FF : out (c),h
	ld	hl, (meas_ebul) : ld bc,0x00FE : out (c),l : ld bc,0x00FF : out (c),h
	; フレーム区切りセンチネル 0xDEAD(Python で整列)
	ld	a,0xAD : ld bc,0x00FE : out (c),a : ld a,0xDE : ld bc,0x00FF : out (c),a
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
meas_bac:	dw	0
meas_snd:	dw	0		; [MEAS] snd_tick(BGM+SFX)
meas_crp:	dw	0		; [MEAS] crater_pump
meas_enm:	dw	0		; [MEAS] enemies_move(敵移動+射撃AI)
meas_col:	dw	0		; [MEAS] collision_check
meas_tur:	dw	0		; [MEAS] turret_fire(地上砲台 walker+spawn)
meas_ebul:	dw	0		; [MEAS] ebul_update(敵+砲台弾)
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
	add	hl, hl			; *50 (hl = col*50)
	IFDEF	ALLAREAS
	ld	de, (amb_lo16)		; [⑦(3)] + area_map_base
	add	hl, de
	ld	a, l
	ld	(emm_a0), a
	ld	a, h
	ld	(emm_a1), a
	ld	a, (amb_hi8)
	adc	a, 0			; +carry
	ld	(emm_a2), a
	ELSE
	ld	a, l
	ld	(emm_a0), a
	ld	a, h
	ld	(emm_a1), a
	xor	a
	ld	(emm_a2), a
	ENDIF
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
	; 高速版: テーブルはページ境界(SHLTAB/SHRTAB)なので BC=(ページ:値) で
	;   `ld a,(bc)` 1命令参照。HL=dst, DE=src, B=ページ(固定)。REPT で完全展開。
	;   旧版の push/pop hl と `ld a,(shl_hi);ld h,a` のループ内反復を排除。
	; p1: COLBUF[i] = shl[A[i]]
	ld	hl, (ec_dst)
	ld	de, (ec_ta)
	ld	a, (shl_hi)
	ld	b, a			; b = shl テーブルページ(固定)
	REPT	TILEBYTES
	ld	a, (de)
	inc	de
	ld	c, a
	ld	a, (bc)			; shl[A[i]]
	ld	(hl), a
	inc	hl
	ENDR
	; p2: COLBUF[i] |= shr[B[i]]
	ld	hl, (ec_dst)		; dst 先頭へ戻す
	ld	de, (ec_tb)
	ld	a, (shr_hi)
	ld	b, a			; b = shr テーブルページ(固定)
	REPT	TILEBYTES
	ld	a, (de)
	inc	de
	ld	c, a
	ld	a, (bc)			; shr[B[i]]
	or	(hl)
	ld	(hl), a
	inc	hl
	ENDR
	ld	(ec_dst), hl		; hl は既に dst+48
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
;   8ラスタをアンロールし dec/jr のループオーバヘッドを除去。
wr8:
	REPT	8
	ld	a, (de)
	inc	de
	out	(c), a
	ld	a, b
	add	a, 0x08
	ld	b, a
	ENDR
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

	IFDEF	ALLAREAS
;=====================================================================
; [⑦] 全エリア版 FDC ブートローダ: sector0 マニフェスト(count + 1件[start_sec:2,len:4])
;   を読み、各データを EMM レイアウト位置へ fdc_load で展開する。
;   file index→EMM addr: 0=PAL, 1=TILES, 2+=(area=(i-2)/3, sub=(i-2)%3: used/map/gobj)。
allarea_load:
	call	fdc_init
	ld	hl, man_buf
	call	fdc_read0_ram		; sector0 → man_buf(256B)
	ld	a, (man_buf + 0x20)
	ld	(al_count), a
	or	a
	ret	z
	xor	a
	ld	(al_i), a
	ld	ix, man_buf + 0x21	; 先頭エントリ
.loop:
	ld	a, (al_i)
	ld	hl, al_count
	cp	(hl)
	ret	nc			; 全件完了
	call	al_emm_addr		; al_i → EMM dst 設定(set_emm_dst 済)
	ld	c, (ix + 0)
	ld	b, (ix + 1)		; BC = start linear sector
	ld	l, (ix + 2)
	ld	h, (ix + 3)		; HL = len(2B, 全ファイル<64KB)
	ld	de, 255			; sector数 = ceil(len/256) = (len+255)>>8
	add	hl, de
	ld	e, h
	ld	d, 0			; DE = sector数
	push	ix
	call	fdc_load		; BC..DE を現 EMM dst へ読む
	pop	ix
	; [③disk] map(圧縮)は EMM_LZTEMP にロード済 → lz_mapdst へ展開
	ld	a, (al_sub)
	cp	1
	jr	nz, .notmap
	ld	a, (lz_mapdst + 0)
	ld	(emm_a0), a
	ld	a, (lz_mapdst + 1)
	ld	(emm_a1), a
	ld	a, (lz_mapdst + 2)
	ld	(emm_a2), a
	push	ix
	IFDEF	BOOTMEAS
	ld	a,(bm_first) : or a : jr z,.decnm	; 最初の1マップのみ計測
	xor a : ld (bm_first),a
	ld	hl,bm_t0 : call bm_rd32		; decode 前 32bit
	call	lz_decode_emm
	ld	hl,bm_t1 : call bm_rd32		; decode 後 32bit
	ld	hl,bm_t0 : ld d,0xA0 : ld e,8 : call bm_emit	; t0=0xA0..3, t1=0xA4..7
	call	bm_cksum			; 展開結果の16bit合計(0xAA/0xAB)で正しさ検証
	jr	.decdn
.decnm:
	ENDIF
	IFNDEF	DECODE_SKIP
	call	lz_decode_emm
	ENDIF
	IFDEF	BOOTMEAS
.decdn:
	ENDIF
	pop	ix
.notmap:
	ld	de, 4			; 次エントリ(4B: start_sec2 + len2)
	add	ix, de
	ld	a, (al_i)
	inc	a
	ld	(al_i), a
	jp	.loop

; al_emm_addr: al_i から EMM dst を計算し set_emm_dst。
al_emm_addr:
	ld	a, (al_i)
	or	a
	jr	nz, .n0
	ld	hl, EMM_PAL & 0xFFFF
	ld	a, EMM_PAL >> 16
	jp	set_emm_dst
.n0:	cp	1
	jr	nz, .area
	ld	hl, EMM_TILES & 0xFFFF
	ld	a, EMM_TILES >> 16
	jp	set_emm_dst
.area:
	sub	2			; j = i-2
	ld	c, 0			; area = j/3
.dl:	cp	3
	jr	c, .dok
	sub	3
	inc	c
	jr	.dl
.dok:	; a=sub(0/1/2), c=area。base24 + area*stride を計算。
	ld	(al_sub), a		; [③disk] sub 保存(map=1 はロード後 decode)
	or	a
	jr	nz, .s1
	ld	hl, EMM_USED & 0xFFFF
	ld	a, EMM_USED >> 16
	ld	de, EMM_USED_STR
	jr	.acc
.s1:	cp	1
	jr	nz, .s2
	ld	hl, EMM_MAPS & 0xFFFF
	ld	a, EMM_MAPS >> 16
	ld	de, EMM_MAPS_STR
	jr	.acc
.s2:	ld	hl, EMM_GOBJ & 0xFFFF
	ld	a, EMM_GOBJ >> 16
	ld	de, EMM_GOBJ_STR
.acc:	; (al_hi:hl) = base + area*de。a=high8, hl=low16, de=stride, c=area。
	ld	(al_hi), a
	ld	a, c
	or	a
	jr	z, .accdone
	ld	b, c
.accl:	add	hl, de
	jr	nc, .nc
	push	hl
	ld	hl, al_hi
	inc	(hl)
	pop	hl
.nc:	djnz	.accl
.accdone:
	; [③disk] map(sub=1)は圧縮。算出した出力先を lz_mapdst に保存し、
	;   fdc ロード先は EMM_LZTEMP(固定)に差し替える(ロード後にループで decode)。
	ld	a, (al_sub)
	cp	1
	jr	nz, .accset
	ld	(lz_mapdst + 0), hl	; low16
	ld	a, (al_hi)
	ld	(lz_mapdst + 2), a
	ld	hl, EMM_LZTEMP & 0xFFFF
	ld	a, EMM_LZTEMP >> 16
	jp	set_emm_dst
.accset:
	ld	a, (al_hi)
	jp	set_emm_dst

; [③disk] map LZSS 展開(高速版): EMM_LZTEMP(圧縮入力)→ 出力EMM(emm_a0/a1/a2, 12800B)。
;   256B窓LZSS(tools/lzmap.py)。レジスタ常駐で高速化:
;     BC=EMM_DT(常駐/出力ポート), DE=outcnt(E=ring index/D=上位で境界判定),
;     H=RINGHI(ring 書込/読出のページ), 裏HL'=入力 lz_inbuf ポインタ(256境界で自動 wrap)。
;   ring/inbuf は 256境界(0xCC00/0xCD00=TBUF域, boot 中は未使用)。
;   補充(lz_refill)時のみ EMM アドレスを入力↔出力で切替。境界判定はシンボル単位
;   (圧縮器はマップ境界を跨ぐマッチを出さないので outcnt は 0x3200 丁度で終わる)。
LZ_MINMATCH	EQU	3
MAP_BYTES	EQU	0x3200		; 12800
EMM_LZTEMP	EQU	0x064000	; gobj(0x060000..0x064000)直後の空き
lz_ring		EQU	0xCC00		; 後方参照リング(256境界, boot中は TBUF 域で未使用)
lz_inbuf	EQU	0xCD00		; 圧縮入力256Bチャンク(256境界, 同上)
RINGHI		EQU	lz_ring >> 8
INBUFHI		EQU	lz_inbuf >> 8
; COPYB: マッチ1バイトをコピー。src=(outcnt_lo-距離)&0xFF、ring[src]を EMM 出力+ring[E]更新+outcnt++。
	MACRO	COPYB
	ld	a, e			; src = (outcnt_lo - lz_dist) & 0xFF
	ld	hl, lz_dist
	sub	(hl)
	ld	l, a
	ld	h, RINGHI
	ld	a, (hl)			; ring[src]
	out	(c), a			; EMM 書込(BC=EMM_DT, 自動+1)
	ld	l, e
	ld	(hl), a			; ring[outcnt_lo] = byte
	inc	de			; outcnt++
	ENDM
lz_decode_emm:
	ld	a, (emm_a0)		; 出力ベース = emm_a0/a1/a2
	ld	(lz_outbase + 0), a
	ld	a, (emm_a1)
	ld	(lz_outbase + 1), a
	ld	a, (emm_a2)
	ld	(lz_outbase + 2), a
	xor	a			; 入力 ptr = EMM_LZTEMP
	ld	(lz_inptr + 0), a
	ld	a, (EMM_LZTEMP >> 8) & 0xFF
	ld	(lz_inptr + 1), a
	ld	a, EMM_LZTEMP >> 16
	ld	(lz_inptr + 2), a
	ld	de, 0			; outcnt=0
	call	lz_refill		; inbuf 充填 + 出力アドレス設定 + BC=EMM_DT/H=RINGHI/HL'=inbuf
	xor	a
	ld	(lz_bitcnt), a		; 最初のシンボルでフラグ再ロードさせる
.sym:
	ld	a, d			; 境界: outcnt(DE) >= 0x3200 で完了
	cp	MAP_BYTES >> 8
	ret	nc
	ld	a, (lz_bitcnt)		; フラグビットが残っているか
	or	a
	jr	nz, .havebit
	call	lz_getbyte		; 新しいフラグバイト
	ld	(lz_flag), a
	ld	a, 8
.havebit:
	dec	a
	ld	(lz_bitcnt), a
	ld	a, (lz_flag)
	rrca				; bit0 → carry
	ld	(lz_flag), a
	jr	c, .match
	; --- リテラル ---
	call	lz_getbyte
	out	(c), a			; EMM 書込(BC=EMM_DT, 自動+1)
	ld	h, RINGHI
	ld	l, e
	ld	(hl), a			; ring[outcnt_lo] = byte
	inc	de			; outcnt++
	jr	.sym
.match:
	call	lz_getbyte		; 距離-1
	inc	a			; 距離
	ld	(lz_dist), a
	call	lz_getbyte		; lenbyte(0..255)。L = lenbyte + MINMATCH(=3)、最大258。
	ld	(lz_len), a		; ★ add MINMATCH で8bit化すると L>255(256..258)が溢れる(旧バグ)。
	COPYB				;   なので MINMATCH 分(3)を無条件コピーし、残り lenbyte(0..255)をループ。
	COPYB				; MINMATCH 2/3
	COPYB				; MINMATCH 3/3
	ld	a, (lz_len)
	or	a
	jr	z, .sym			; lenbyte==0 → L=3 で完了
.mcl:
	COPYB
	ld	hl, lz_len
	dec	(hl)
	jr	nz, .mcl
	jr	.sym

; lz_getbyte: a = 次入力バイト(lz_inbuf から, 裏 HL'=入力ptr)。256消費で lz_refill。
;   BC/DE/H(=RINGHI)を保持し、a と flags のみ変更。
lz_getbyte:
	exx				; 裏: HL'=入力 ptr(INBUFHI:inidx)
	ld	a, (hl)			; a = inbuf[inidx]
	inc	l			; inidx++(0xFF→0x00 で Z, 256境界 wrap)
	exx				; 表に戻る(Z フラグは保持)
	ret	nz			; 通常: wrap なし
	push	af			; wrap: 次チャンクを補充(a=今読んだバイトを保持)
	call	lz_refill
	pop	af
	ret

; lz_refill: lz_inbuf へ EMM(lz_inptr)から256B、lz_inptr+=256、出力アドレス復帰、HL'=inbuf。
;   DE(outcnt)を保持。終了時 BC=EMM_DT, H=RINGHI, HL'=lz_inbuf。
lz_refill:
	ld	a, (lz_inptr + 0)	; EMM 読出アドレス = lz_inptr
	ld	bc, EMM_AL
	out	(c), a
	ld	a, (lz_inptr + 1)
	ld	bc, EMM_AM
	out	(c), a
	ld	a, (lz_inptr + 2)
	ld	bc, EMM_AH
	out	(c), a
	ld	hl, lz_inbuf		; 256B を lz_inbuf へ
	ld	bc, EMM_DT
	push	de			; outcnt 退避(D を 256 カウンタに使う)
	ld	d, 0
.rl:	in	a, (c)
	ld	(hl), a
	inc	hl
	dec	d
	jr	nz, .rl			; 256 回
	pop	de			; outcnt 復帰
	ld	hl, lz_inptr + 1	; lz_inptr += 256
	inc	(hl)
	jr	nz, .nc
	inc	hl
	inc	(hl)
.nc:	ld	a, (lz_outbase + 0)	; 出力 EMM アドレス = lz_outbase + outcnt(DE) を復帰
	add	a, e
	ld	l, a
	ld	a, (lz_outbase + 1)
	adc	a, d
	ld	h, a
	ld	a, (lz_outbase + 2)
	adc	a, 0
	ld	bc, EMM_AL
	out	(c), l
	ld	bc, EMM_AM
	out	(c), h
	ld	bc, EMM_AH
	out	(c), a
	ld	bc, EMM_DT		; ホットループ用レジスタ復帰
	ld	h, RINGHI
	exx
	ld	hl, lz_inbuf		; HL'=入力 ptr リセット
	exx
	ret

	IFDEF	DMPMAPS
; dump_all_maps: 展開後の EMM_MAPS[0..15](各12800B)を順に OUT 0xFB で生ダンプし、以降停止。
;   ホスト側(XMIL_EMMDUMP=file)で 204800B のファイルを得て area*_map.bin と cmp する。
dump_all_maps:
	ld	a, EMM_MAPS & 0xFF
	ld	(dm_a0), a
	ld	a, (EMM_MAPS >> 8) & 0xFF
	ld	(dm_a1), a
	ld	a, EMM_MAPS >> 16
	ld	(dm_a2), a
	ld	b, 16
.area:
	push	bc
	ld	a,(dm_a0) : ld bc,EMM_AL : out (c),a
	ld	a,(dm_a1) : ld bc,EMM_AM : out (c),a
	ld	a,(dm_a2) : ld bc,EMM_AH : out (c),a
	ld	de, MAP_BYTES
.byte:
	ld	bc,EMM_DT  : in  a,(c)
	ld	bc,0x00FB  : out (c),a
	dec	de : ld a,d : or e : jr nz,.byte
	ld	a,(dm_a1) : add a,(MAP_BYTES >> 8) & 0xFF : ld (dm_a1),a	; addr += 0x3200
	ld	a,(dm_a2) : adc a,MAP_BYTES >> 16 : ld (dm_a2),a
	pop	bc
	djnz	.area
.halt:	jr	.halt
dm_a0:	db	0
dm_a1:	db	0
dm_a2:	db	0
	ENDIF

	IFDEF	BOOTMEAS
; bm_rd32: HL=4byteバッファに 32bit サイクルカウンタを格納(0xFC=b0, 0xFD×3=b1..3)。
bm_rd32:
	ld	bc,0x00FC : in a,(c) : ld (hl),a : inc hl
	ld	bc,0x00FD
	in	a,(c) : ld (hl),a : inc hl
	in	a,(c) : ld (hl),a : inc hl
	in	a,(c) : ld (hl),a
	ret
; bm_emit: HL=buf, D=開始marker, E=バイト数。各バイトを PROBE(marker++)で出力。
bm_emit:
.el:	ld	a,(hl)
	ld	bc,0x00FE : out (c),a
	ld	bc,0x00FF : out (c),d
	inc	hl : inc d : dec e : jr nz,.el
	ret
; bm_cksum: lz_mapdst の 12800B を EMM から読み戻し 16bit 合計を PROBE(0xAA=lo,0xAB=hi)。
bm_cksum:
	ld	a,(lz_mapdst+0) : ld bc,EMM_AL : out (c),a
	ld	a,(lz_mapdst+1) : ld bc,EMM_AM : out (c),a
	ld	a,(lz_mapdst+2) : ld bc,EMM_AH : out (c),a
	ld	hl,0
	ld	de,MAP_BYTES
	ld	bc,EMM_DT
.cl:	in	a,(c)
	add	a,l : ld l,a : jr nc,.nc4 : inc h
.nc4:	dec	de : ld a,d : or e : jr nz,.cl
	ld	a,l : ld bc,0x00FE : out (c),a : ld bc,0x00FF : ld a,0xAA : out (c),a
	ld	a,h : ld bc,0x00FE : out (c),a : ld bc,0x00FF : ld a,0xAB : out (c),a
	ret
	ENDIF

;=====================================================================
; [⑦(2)] area_switch: a=area(0..15)。areaNN_used(EMM_USED+a*0x800)を読み、
;   common_tiles[idx](EMM_TILES+idx*48)を RAM タイル表(TILEBASE+i*48)へ 48B ずつ集約。
;   map/gobj は EMM 常駐のまま(engine が area base を加算して読む=(3))。
area_switch:
	; --- [⑦(3)] area_map_base = EMM_MAPS + area*0x3200 を保存(read_ids/crater が使う) ---
	ld	c, a			; area
	ld	hl, EMM_MAPS & 0xFFFF
	ld	a, EMM_MAPS >> 16
	ld	(amb_hi8), a
	ld	a, c
	or	a
	jr	z, .mbdone
	ld	de, EMM_MAPS_STR
	ld	b, c
.mbl:	add	hl, de
	jr	nc, .mbnc
	push	hl
	ld	hl, amb_hi8
	inc	(hl)
	pop	hl
.mbnc:	djnz	.mbl
.mbdone:
	ld	(amb_lo16), hl
	ld	a, c			; area 復帰
	; --- used list header(n): EMM addr = EMM_USED + area*0x800 ---
	add	a, a
	add	a, a
	add	a, a			; area*8 (=(area*0x800)>>8)
	ld	(emm_a1), a
	xor	a
	ld	(emm_a0), a
	ld	a, EMM_USED >> 16	; 0x01
	ld	(emm_a2), a
	call	set_emm_addr
	ld	bc, EMM_DAT
	in	a, (c)			; n lo
	ld	l, a
	in	a, (c)			; n hi
	ld	h, a
	ld	(as_n), hl
	; --- n*2B(共通index)を used_buf へ(EMM 自動+1で連続読み) ---
	add	hl, hl			; hl = n*2 (バイト数)
	ld	de, used_buf
.rdl:	ld	a, h
	or	l
	jr	z, .rddone
	in	a, (c)			; EMM byte
	ld	(de), a
	inc	de
	dec	hl
	jr	.rdl
.rddone:
	; --- 集約: for i=0..n-1: idx=used_buf[i], EMM_TILES+idx*48 → TILEBASE+i*48 ---
	ld	hl, (as_n)
	ld	(as_cnt), hl
	ld	ix, used_buf
	ld	hl, TILEBASE
	ld	(as_dst), hl
	IFDEF	SOUND
	ld	a, AS_TICK_EVERY
	ld	(as_tick), a
	ENDIF
.tl:	ld	hl, (as_cnt)
	ld	a, h
	or	l
	ret	z			; 全タイル完了
	dec	hl
	ld	(as_cnt), hl
	IFDEF	SOUND
	; [⑦(4)] 切替中も BGM 継続: AS_TICK_EVERY タイル毎に snd_tick(実機 ~0.12s の集約中)
	ld	a, (as_tick)
	dec	a
	ld	(as_tick), a
	jr	nz, .notick
	ld	a, AS_TICK_EVERY
	ld	(as_tick), a
	push	ix
	call	snd_tick
	pop	ix
.notick:
	ENDIF
	; EMM src = EMM_TILES + idx*48 (16bit, <0xC000)
	ld	l, (ix + 0)
	ld	h, (ix + 1)		; hl = idx
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl			; *16
	ld	d, h
	ld	e, l
	add	hl, hl			; *32
	add	hl, de			; *48
	ld	de, EMM_TILES
	add	hl, de			; EMM_TILES + idx*48
	ld	a, l
	ld	(emm_a0), a
	ld	a, h
	ld	(emm_a1), a
	xor	a
	ld	(emm_a2), a
	call	set_emm_addr
	; 48B を (as_dst) へ
	ld	hl, (as_dst)
	ld	bc, EMM_DAT
	ld	d, 48
.cpl:	in	a, (c)
	ld	(hl), a
	inc	hl
	dec	d
	jr	nz, .cpl
	ld	(as_dst), hl		; +48 済
	ld	de, 2
	add	ix, de			; 次 index
	jr	.tl

;=====================================================================
; [⑦(4)] area_advance: 次エリアへ切替(暗転→タイル/マップ/gobj→adv_off reset→prefill→取消)。
area_advance:
	; next = (area_cur==15) ? 6 : area_cur+1  (エリア16→7ループ)
	ld	a, (area_cur)
	cp	15
	jr	nz, .aa_inc
	ld	a, 5			; +1 で 6
.aa_inc:
	inc	a
	ld	(area_cur), a
	IFDEF	SOUND
	ld	a, 1			; [SND] 次エリアは opening から。切替中は無音(mgr が復帰で opening)
	ld	(bgm_silent), a
	call	snd_bgm_stop
	ENDIF
	IFDEF	ALLAREAS_DBG
	ld	e, a			; PROBE 0xA0<area>: エリア切替を通知
	ld	bc, 0x00FE
	out	(c), e
	ld	a, 0xA0
	ld	bc, 0x00FF
	out	(c), a
	ld	a, (area_cur)
	ENDIF
	; タイル表 + map base 切替
	ld	a, (area_cur)
	call	area_switch
	; 地上物切替
	ld	a, (area_cur)
	call	gobj_load
	IFDEF	ENEMY_EXTDATA
	call	fly_load		; [出現②] 新エリアの flying 出現コマンド列をパース
	ENDIF
	; adv_off = framecnt>>3 (エリア先頭で coarse=0)
	ld	hl, (framecnt)
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	ld	(adv_off), hl
	; 再 prefill(新エリア cols 0..39 を GRAM へ)
	call	prefill
	; トランジェント取消(旧座標の GRAM 書込を止める)
	xor	a
	ld	(pc_active), a		; クレーター待ち行列
	ld	(rd_active), a		; 進行中の列再描画
	ld	(sr_pending), a
	IFDEF	GROBDA_EXTDATA
	call	grobda_init
	ENDIF
	call	bacura_init		; [敵] 切替で Bacura クリア
	; 暗転: 黒パレットを最後にロード(EMM/GRAM I/O の後で palandply を確定させる)
	ld	ix, blackpal
	call	load_palette64
	ld	a, 2
	ld	(blank_ctr), a
	IFDEF	ALLAREAS_DBG
	ld	a, 0xFF			; PROBE 0xA2FF: blackpal ロード完了
	ld	e, a
	ld	bc, 0x00FE
	out	(c), e
	ld	a, 0xA2
	ld	bc, 0x00FF
	out	(c), a
	ENDIF
	ret

;=====================================================================
; [⑦(4)] gobj_load: a=area。EMM_GOBJ+area*0x400 の新形式地上物を engine の 6B RAM
;   リスト(gobj_list/gobj_n)へ変換。size=1 は addr0=crater_base。addr0==0xFFFF はスキップ。
;   (sol セクションは今は未使用=読み飛ばさず無視: Sol は area0 の EQU 版のまま=要注意)
gobj_load:
	add	a, a
	add	a, a			; area*4 (=(area*0x400)>>8)
	ld	(emm_a1), a
	xor	a
	ld	(emm_a0), a
	ld	a, EMM_GOBJ >> 16	; 0x06
	ld	(emm_a2), a
	call	set_emm_addr
	ld	bc, EMM_DAT
	in	a, (c)			; gobj_count
	ld	(gl_cnt), a
	xor	a
	ld	(gl_wr), a
	IFDEF	ENEMY_EXTDATA
	ld	(turret_n), a		; [③(A)] 地上砲台リストをエリア毎にリセット
	ENDIF
	ld	de, gobj_list
.gl:
	ld	a, (gl_cnt)
	or	a
	jp	z, .gdone
	dec	a
	ld	(gl_cnt), a
	; col(2),row,type,size,fire_mask_id,addr0(2) を gl_buf(8B)へ
	ld	hl, gl_buf
	ld	a, 8
	ld	(gl_dc), a
.rdhdr:	in	a, (c)
	ld	(hl), a
	inc	hl
	ld	a, (gl_dc)
	dec	a
	ld	(gl_dc), a
	jr	nz, .rdhdr
	; 残り discard: (2*size)^2-1 word = size1→6B, size2→30B
	ld	a, (gl_buf + 4)		; size
	dec	a
	jr	z, .dsz1
	ld	a, 30
	jr	.dset
.dsz1:	ld	a, 6
.dset:	or	a
	jr	z, .wr
	ld	(gl_dc), a
.dloop:	in	a, (c)
	ld	a, (gl_dc)
	dec	a
	ld	(gl_dc), a
	jr	nz, .dloop
.wr:
	; addr0==0xFFFF ならスキップ(off-map。engine の +48/96/144 導出が暴走するため)
	ld	a, (gl_buf + 6)		; addr0_lo(新フォーマットで +5=mask_id の後)
	cp	0xFF
	jr	nz, .dowrite
	ld	a, (gl_buf + 7)		; addr0_hi
	cp	0xFF
	jp	z, .gl			; 両 FF → スキップ
.dowrite:
	; 6B 書込: col_lo, col_hi, row, size(+3=未使用typeの位置), crater_lo, crater_hi
	;   (type は未使用なので +3 に size を格納。crater_write_obj が 2x2/4x4 を判定)
	ld	a, (gl_buf + 0)
	ld	(de), a
	inc	de
	ld	a, (gl_buf + 1)
	ld	(de), a
	inc	de
	ld	a, (gl_buf + 2)
	ld	(de), a
	inc	de
	ld	a, (gl_buf + 4)		; size(1 or 2)
	ld	(de), a
	inc	de
	ld	a, (gl_buf + 6)		; crater_lo(addr0)
	ld	(de), a
	inc	de
	ld	a, (gl_buf + 7)		; crater_hi
	ld	(de), a
	inc	de
	IFDEF	ENEMY_EXTDATA
	; [③(A)] 射撃砲台(fire_mask_id≠0)を turret_list へ。mask_id は gobj データが直接保持。
	;   ※bc=EMM_DAT(次レコードの EMM 読込に必須)なので push/pop で保護する。
	push	bc
	ld	a, (gl_buf + 5)		; fire_mask_id(0=非射撃 / 0x08 derota / 0x09 logram / 0x10 boza)
	or	a
	jr	z, .tur_no
	ld	c, a			; c=mask_id
	ld	a, (turret_n)
	cp	MAX_TURRETS
	jr	nc, .tur_no		; 満杯
	ld	b, a
	add	a, a
	add	a, a
	add	a, b			; turret_n*5
	push	de			; de は gobj_list 用に保持
	ld	e, a
	ld	d, 0
	ld	hl, turret_list
	add	hl, de
	ld	a, (gl_buf + 0)
	ld	(hl), a			; col_lo
	inc	hl
	ld	a, (gl_buf + 1)
	ld	(hl), a			; col_hi
	inc	hl
	ld	a, (gl_buf + 2)
	ld	(hl), a			; row
	inc	hl
	ld	(hl), c			; mask_id
	inc	hl
	ld	(hl), 1			; timer=1(初回はすぐ発火→適切マスクで再装填)
	pop	de
	ld	a, (turret_n)
	inc	a
	ld	(turret_n), a
.tur_no:
	pop	bc			; bc(=EMM_DAT)復帰(次レコードの EMM 読込用)
	ENDIF
	ld	a, (gl_wr)
	inc	a
	ld	(gl_wr), a
	jp	.gl
.gdone:
	ld	a, (gl_wr)
	ld	(gobj_n), a
	; --- sol セクション: sol_count + 各 sol[col2,row,frame4x4addr] を sol_list へ ---
	;   (EMM ポインタは gobj セクション直後。各フレームは base(=addr0)のみ保持し残3 addr 破棄)
	ld	bc, EMM_DAT
	in	a, (c)			; sol_count
	ld	(sol_n), a
	or	a
	jr	z, .read_grob		; sol 無しでも grob は読む
	ld	(gl_cnt), a
	ld	de, sol_list
.sol_rd:
	in	a, (c)			; col lo
	ld	(de), a
	inc	de
	in	a, (c)			; col hi
	ld	(de), a
	inc	de
	in	a, (c)			; row
	ld	(de), a
	inc	de
	; 4 フレーム: 各 base(2B)保持 + 残 3 addr(6B)破棄
	REPT 4
	in	a, (c)			; base lo
	ld	(de), a
	inc	de
	in	a, (c)			; base hi
	ld	(de), a
	inc	de
	in	a, (c)			; 破棄 TR,BL,BR (各2B=6B)
	in	a, (c)
	in	a, (c)
	in	a, (c)
	in	a, (c)
	in	a, (c)
	ENDR
	ld	a, (gl_cnt)
	dec	a
	ld	(gl_cnt), a
	jp	nz, .sol_rd
.read_grob:
	IFDEF	GROBDA_EXTDATA
	; --- grobda セクション: grob_count + 各[col2,row] を grob_wtab/grob_rtab へ(最大 MAX_GROBDA) ---
	ld	bc, EMM_DAT
	in	a, (c)			; grob_count
	cp	MAX_GROBDA + 1
	jr	c, .grc_ok
	ld	a, MAX_GROBDA		; クランプ
.grc_ok:
	ld	(grob_n), a
	or	a
	ret	z
	ld	(gl_cnt), a
	ld	hl, grob_wtab
	ld	ix, grob_rtab
.grob_rd:
	in	a, (c)			; col lo
	ld	(hl), a			; world列(下位8bit)
	inc	hl
	in	a, (c)			; col hi(無視)
	in	a, (c)			; row
	ld	(ix + 0), a
	inc	ix
	ld	a, (gl_cnt)
	dec	a
	ld	(gl_cnt), a
	jr	nz, .grob_rd
	ENDIF
	ret

AS_TICK_EVERY	EQU	44		; 切替タイル集約中 snd_tick 間隔(~353/8)
as_n:		dw	0
as_cnt:		dw	0
as_dst:		dw	0
as_tick:	db	0		; snd_tick カウンタ
amb_lo16:	dw	0		; [⑦(3)] area_map_base(EMM_MAPS+area*0x3200) 低16
amb_hi8:	db	0		; 高8
pal_buf:	ds	320		; [⑦(3)] common_pal(64×5B)
;   used_buf(ds 1024) は program 節約で 0xD808+ (fly データ領域) へ移動。
man_buf:	ds	256		; sector0(マニフェスト)
al_count:	db	0
al_i:		db	0
al_hi:		db	0
al_sub:		db	0		; [③disk] 現ファイルの sub(0=used/1=map/2=gobj)
lz_mapdst:	ds	3		; [③disk] 圧縮map の展開先 EMM アドレス(24bit)
bm_t0:		ds	4		; [BOOTMEAS] decode 前の 32bit サイクルカウンタ
bm_t1:		ds	4		; [BOOTMEAS] decode 後の 32bit サイクルカウンタ
bm_first:	db	1		; [BOOTMEAS] 最初の1マップ decode のみ計測するフラグ
; [⑦(4)] エリア進行
area_cur:	db	0		; 現在エリア(0-based, 0=area1)
adv_off:	dw	0		; adv のエリア先頭オフセット(= framecnt>>3 at switch)
blank_ctr:	db	0		; 切替暗転の残フレーム
blackpal:	ds	320		; 黒パレット(pal_buf の色を 0 に。boot で生成)
gl_cnt:		db	0		; gobj_load: 残エントリ
gl_wr:		db	0		; gobj_load: 書込済み件数
gl_dc:		db	0		; gobj_load: discard/読みカウンタ
gl_buf:		ds	8		; gobj_load: ヘッダ一時(col2,row,type,size,fire_mask_id,addr0_2)
	ENDIF

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
; load_palette64: ix=パレットデータ(64エントリ×5B [addr:2B, B4,R4,G4])。呼出側が ix 設定。
load_palette64:
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
	IFDEF	SOUND
;=====================================================================
; [SND] サウンド: xevi-extract の PSG プレイヤ(sndplay.inc) + データ。
;   データは xevi_sound.py 出力(非コミット, roms/)。差し替えは incbin パスのみ。
	INCLUDE	"tools/sndplay.inc"
snd_bgm_opening:
	incbin	"roms/arcade/xevious-out/sound/xevi_opening.bin"	; tune1(3ch) 開始/復活/次エリアで1回
snd_bgm_arpeggio:
	incbin	"roms/arcade/xevious-out/sound/xevi_arpeggio.bin"	; tune E(1ch) 飛行中ループ
se_zapper:
	incbin	"roms/arcade/xevious-out/sound/se_00_zapper.bin"
se_blaster:
	incbin	"roms/arcade/xevious-out/sound/se_01_blaster.bin"
se_flyhit:
	incbin	"roms/arcade/xevious-out/sound/se_02_flyhit.bin"
se_teleport:
	incbin	"roms/arcade/xevious-out/sound/se_03_teleport.bin"
se_exp_aerial:
	incbin	"roms/arcade/xevious-out/sound/se_07_exp_aerial.bin"
se_exp_ground:
	incbin	"roms/arcade/xevious-out/sound/se_08_exp_ground.bin"
se_bacura:
	; [敵] ザッパーがBacura被弾=カンカン(tune A)。
	;   ※ROM の tune A は 2ch(ch21: C5→C#5 / ch22: C#5 重ねで厚み)だが、本実装は 1ch。
	;     理由: snd_play_se は PSG C 1本を奪取する単チャンネル SFX 機構で、2ch SFX は
	;     BGM からもう1本奪う必要があり、特に opening(3ch) との競合・復帰処理が増える。
	;     se_06_bacura.bin は xevi-re が ch21列(C5→C#5)のみに簡略化済み。重ねが無くても
	;     鋭アタック(vmode=2)の短い金属音で「カンッ」は十分表現できるため 1ch で可とした。
	incbin	"roms/arcade/xevious-out/sound/se_06_bacura.bin"
; BGM 状態機械(mainloop から毎フレーム)。
;   無音条件 = game_over または ship_inv!=0(死亡の爆発/復活無敵中)。
;   無音→再生へ移る瞬間(開始・復活・次エリア)は opening(tune1) を1回。
;   opening 終了(snd_bgm_active=0)で arpeggio(tune E) へ、以降 arpeggio をループ。
;   死亡で停止(PSG 音量 0、C は爆発SFXが使うので保護)。
snd_bgm_mgr:
	ld	a, (game_over)
	or	a
	jr	nz, .silent
	ld	a, (ship_inv)
	or	a
	jr	nz, .silent
	; --- BGM を鳴らすべき状態 ---
	ld	a, (bgm_silent)
	or	a
	jr	nz, .restart_opening	; 無音→再生: opening 再開
	; 通常飛行: opening → arpeggio → arpeggio ループ
	call	snd_bgm_active
	or	a
	ret	nz			; まだ再生中
	ld	a, (bgm_phase)
	or	a
	jr	nz, .arp
	ld	a, 1
	ld	(bgm_phase), a		; opening 終了 → 以降 arpeggio
.arp:
	ld	ix, snd_bgm_arpeggio
	jp	snd_play_bgm
.restart_opening:
	xor	a
	ld	(bgm_silent), a
	ld	(bgm_phase), a		; =0: opening フェーズ
	ld	ix, snd_bgm_opening
	jp	snd_play_bgm
.silent:
	ld	a, (bgm_silent)
	or	a
	ret	nz			; 既に無音
	ld	a, 1
	ld	(bgm_silent), a
	jp	snd_bgm_stop

; BGM 停止: tick 無効化 + PSG A/B 音量 0。C(R10)は SFX 稼働中なら保護。
snd_bgm_stop:
	xor	a
	ld	(bgm_nch), a		; snd_tick が BGM voice を進めない
	ld	d, 8			; R8 = vol A
	ld	e, 0
	call	psg_set
	ld	d, 9			; R9 = vol B
	ld	e, 0
	call	psg_set
	ld	a, (sfx_type)
	or	a
	ret	nz			; SFX が PSG C を使用中 → R10 は触らない
	ld	d, 10			; R10 = vol C
	ld	e, 0
	jp	psg_set
bgm_phase:	db	0		; 0=opening, 1=arpeggio
bgm_silent:	db	1		; 1=無音(起動時/死亡中/game_over)。起動時1で opening が始まる
	ENDIF
	ENDIF

	IFDEF	ALLAREAS
	INCLUDE	"tools/fdcload.inc"	; [⑦] FDC→EMM ローダ(fdc_init/fdc_load/fdc_read0_ram/set_emm_dst)
	ENDIF

	; [再発防止] program 終端が作業バッファ(0xCC00〜)と衝突しないこと(衝突するとスクロール書込が
	;   コードを破壊しハング)。肥大したらビルドエラーになる。バッファ上限がスタックに迫らないことも。
	ASSERT	$ <= WORKBUF_BASE
	ASSERT	WORKBUF_TOP <= 0xE800

	; [出現②] flying 出現コマンド列(areaNN_fly.bin, 計~1.5KB)を program ではなく作業バッファ上の
	;   空き(0xD808〜0xE800)に配置。ロード時にこの範囲へ展開され(gap はゼロ埋め=作業バッファは実行時に
	;   初期化されるので無害)、fly_load が EMM 経由でなく直接参照できる。
	IFDEF	ENEMY_EXTDATA
	DS	0xD808 - $		; ギャップを埋めて fly データを 0xD808 に正しく配置(--raw は ORG を pad しない)
fly_a01: INCBIN "roms/arcade/xevious-out/enemies/area01_fly.bin"
fly_a02: INCBIN "roms/arcade/xevious-out/enemies/area02_fly.bin"
fly_a03: INCBIN "roms/arcade/xevious-out/enemies/area03_fly.bin"
fly_a04: INCBIN "roms/arcade/xevious-out/enemies/area04_fly.bin"
fly_a05: INCBIN "roms/arcade/xevious-out/enemies/area05_fly.bin"
fly_a06: INCBIN "roms/arcade/xevious-out/enemies/area06_fly.bin"
fly_a07: INCBIN "roms/arcade/xevious-out/enemies/area07_fly.bin"
fly_a08: INCBIN "roms/arcade/xevious-out/enemies/area08_fly.bin"
fly_a09: INCBIN "roms/arcade/xevious-out/enemies/area09_fly.bin"
fly_a10: INCBIN "roms/arcade/xevious-out/enemies/area10_fly.bin"
fly_a11: INCBIN "roms/arcade/xevious-out/enemies/area11_fly.bin"
fly_a12: INCBIN "roms/arcade/xevious-out/enemies/area12_fly.bin"
fly_a13: INCBIN "roms/arcade/xevious-out/enemies/area13_fly.bin"
fly_a14: INCBIN "roms/arcade/xevious-out/enemies/area14_fly.bin"
fly_a15: INCBIN "roms/arcade/xevious-out/enemies/area15_fly.bin"
fly_a16: INCBIN "roms/arcade/xevious-out/enemies/area16_fly.bin"
fly_area_tab:
	dw	fly_a01, fly_a02, fly_a03, fly_a04, fly_a05, fly_a06, fly_a07, fly_a08
	dw	fly_a09, fly_a10, fly_a11, fly_a12, fly_a13, fly_a14, fly_a15, fly_a16
fly_rec:	ds	FLY_REC_MAX * 4	; パース済 (T,kind,num,off)×
ffreq_rec:	ds	FFREQ_REC_MAX * 3	; [③] パース済 (T,mask_id,mask)× (program 節約で高位メモリへ)
turret_list:	ds	MAX_TURRETS * TURREC	; [③(A)] 地上砲台 (col2,row,mask_id,timer)×
used_buf:	ds	1024		; 共通index列(program 節約で高位メモリへ, 最大 n=431→862B)
	; [③disk] map LZSS 展開の状態(boot のみ使用)。ring/inbuf は EQU で TBUF 域(0xCC00/0xCD00)。
lz_inptr:	ds	3		; 入力 EMM アドレス(24bit, EMM_LZTEMP から)
lz_outbase:	ds	3		; 出力 EMM ベースアドレス(24bit)
lz_flag:	db	0		; 現フラグバイト(回転)
lz_bitcnt:	db	0		; 残ビット数(0..8)
lz_dist:	db	0		; マッチ距離(1..256)
lz_len:		db	0		; マッチ長
	ASSERT	$ <= 0xE800
	ENDIF

	END
