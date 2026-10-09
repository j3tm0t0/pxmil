; domogram.inc - [Phase1] Domogram(移動する地上砲台)。sprite.inc から INCLUDE。
;   active slot は parallel array(IX=slot, inc ix)。1コマ(DOMO_BASE)静止。1窓描画+idle-skip。
;   データ: domo_data(=EMM_DOMO の当エリアブロックを area_advance で RAM コピー)。
;     ndomo,[col,row,nseg,(dir,dur)×nseg]。vector=32×(dY,dX)signed 1/32px(=frac)。
;   world 位置 wcx/wcy=cell<<8|frac。scol=world_col-coarse(16bit, turret_fire と同一写像)。
;   地面貼付でスクロール追従。破壊=爆発のみ(ROM は crater を残さない: xevi-re 確認済)。
; active slot = parallel array(SoA)。フィールド k の slot i は dm_slots + k*MAX_DOMOGRAM + i。
;   IX = dm_slots + i とし (ix + DM_xxx) でアクセス、次 slot は inc ix(旧 AoS の add ix,DMREC を廃止)。
;   move は同じ配列を半分(4 slot)ずつ連続処理する。
DM_ACT	EQU	0 * MAX_DOMOGRAM	; active
DM_WCXL	EQU	1 * MAX_DOMOGRAM	; wcx lo(frac)  wcx = col<<8|frac
DM_WCXH	EQU	2 * MAX_DOMOGRAM	; wcx hi(col)
DM_WCYL	EQU	3 * MAX_DOMOGRAM	; wcy lo(frac)  wcy = row<<8|frac
DM_WCYH	EQU	4 * MAX_DOMOGRAM	; wcy hi(row)
DM_SEGPL	EQU	5 * MAX_DOMOGRAM	; 次セグメント ptr(domo_data 内)lo
DM_SEGPH	EQU	6 * MAX_DOMOGRAM	; 〃 hi
DM_DIR	EQU	7 * MAX_DOMOGRAM
DM_DUR	EQU	8 * MAX_DOMOGRAM
DM_NSEG	EQU	9 * MAX_DOMOGRAM
DM_FTMR	EQU	10 * MAX_DOMOGRAM
DM_PREVL	EQU	11 * MAX_DOMOGRAM	; page0(cur_pos bit10=0)に描いた cell lo。hi=0xFF で未描画
DM_PREVH	EQU	12 * MAX_DOMOGRAM
DM_LAST	EQU	13 * MAX_DOMOGRAM	; 最後に move した framecnt 下位(追いつき量の算出用)
DM_PRV1L	EQU	14 * MAX_DOMOGRAM	; page1 に描いた cell lo(自機/敵の SPR_1WIN と同じくページ別 prev)
DM_PRV1H	EQU	15 * MAX_DOMOGRAM
DM_PG1	EQU	DM_PRV1L - DM_PREVL	; page1 の prev を読むとき IX に足す量(L/H とも同じ差)
	ASSERT	DM_PRV1H - DM_PREVH == DM_PG1
DMFIELDS	EQU	16			; dm_slots = DMFIELDS * MAX_DOMOGRAM バイト


	IFDEF	DOMOMEAS
; [計測] name を body の前後サイクル差で包み、PROBE <T数> → PROBE 0xD0<mk> を出す。
	MACRO	DMWRAP name, body, mk
name:
	ld	bc, 0x00FC : in l, (c) : ld bc, 0x00FD : in h, (c) : push hl
	call	body
	ld	bc, 0x00FC : in e, (c) : ld bc, 0x00FD : in d, (c)
	pop	hl : ex de, hl : or a : sbc hl, de
	ld	bc, 0x00FE : out (c), l : ld bc, 0x00FF : out (c), h
	ld	a, mk : ld bc, 0x00FE : out (c), a : ld a, 0xD0 : ld bc, 0x00FF : out (c), a
	ret
	ENDM
	ENDIF

; dm_prev_inval: ix slot の両ページの prev を無効(hi=0xFF)に。a 破壊なし。
dm_prev_inval:
	ld	(ix + DM_PREVH), 0xFF
	ld	(ix + DM_PRV1H), 0xFF
	ret

; dm_erase_prev: 両ページに残る絵を消して prev を無効化(deactivate/破壊/画面外)。
;   TVRAM は自動で消えないので slot 解放時は必ず呼ぶ。
dm_erase_prev:
	call	.one				; page0
	ld	de, DM_PG1 : add ix, de
	call	.one				; page1
	ld	de, -DM_PG1 : add ix, de
	ret
.one:	ld	a, (ix + DM_PREVH) : cp 0xFF : ret z
	ld	l, (ix + DM_PREVL) : ld h, a
	call	ene_clr2x2
	ld	(ix + DM_PREVH), 0xFF
	ret

; domo_reset_active: 8 slot 無効化。旧絵が残っていれば消去(area 切替で TVRAM に残り得る)。
domo_reset_active:
	ld	ix, dm_slots
	ld	b, MAX_DOMOGRAM
.ra:	push	bc
	ld	(ix + DM_ACT), 0
	call	dm_erase_prev				; (遅延消去も含む)
	inc	ix
	pop	bc
	djnz	.ra
	xor	a : ld (dm_nact), a : ld (dm_pidx), a
	ret

; domo_emm_copy: area_cur の Domogram ブロックを EMM_DOMO から domo_data(RAM)へコピー。
;   EMM_DOMO 先頭に u16[17] offset。off[area]..off[area+1] がブロック範囲(off は file 先頭基準)。
domo_emm_copy:
	ld	a, (area_cur)
	add	a, a			; area*2(offset table index, <256)
	ld	bc, EMM_A0 : out (c), a
	ld	a, (EMM_DOMO >> 8) & 0xFF : ld bc, EMM_A0 + 1 : out (c), a
	ld	a, EMM_DOMO >> 16 : ld bc, EMM_A0 + 2 : out (c), a
	ld	bc, EMM_DAT
	in	a, (c) : ld l, a		; off[area] lo
	in	a, (c) : ld h, a		; off[area] hi → hl=off[area]
	in	a, (c) : ld e, a		; off[area+1] lo
	in	a, (c) : ld d, a		; off[area+1] hi → de=off[area+1]
	ld	(dm_off), hl		; save off[area]
	ex	de, hl			; hl=off[area+1], de=off[area]
	or	a : sbc hl, de		; hl = length
	ld	(dm_len), hl
	; EMM addr = EMM_DOMO + off[area]
	ld	hl, (dm_off)
	ld	de, EMM_DOMO & 0xFFFF
	add	hl, de			; low16(off<=0x534 なので繰上無し)
	ld	a, l : ld bc, EMM_A0 : out (c), a
	ld	a, h : ld bc, EMM_A0 + 1 : out (c), a
	ld	a, EMM_DOMO >> 16 : ld bc, EMM_A0 + 2 : out (c), a
	ld	hl, domo_data
	ld	de, (dm_len)
	ld	bc, EMM_DAT
.cpl:	in	a, (c) : ld (hl), a : inc hl
	dec	de : ld a, d : or e : jr nz, .cpl
	ret

; domo_load: domo_data を dm_pend[] に展開 + active リセット。area_advance で domo_emm_copy の後に呼ぶ。
;   pend record(6B): col, row, ppath_lo, ppath_hi, nseg, spawn。ppath は domo_data 内を指す。
;   ndomo は MAX_DOMO_PEND を超えない(bake の最大 ndomo=19=MAX_DOMO_PEND、切り詰めは起きない)。
domo_load:
	ld	hl, domo_data
	ld	a, (hl) : inc hl		; ndomo
	cp	MAX_DOMO_PEND + 1 : jr c, .nok
	ld	a, MAX_DOMO_PEND
.nok:	ld	(dm_pn), a
	push	hl : push af
	call	domo_reset_active
	pop	af : pop hl
	or	a : ret z
	ld	b, a
	ld	de, dm_pend
.pl:	ld	a, (hl) : inc hl : ld (de), a : inc de	; col
	ld	a, (hl) : inc hl : ld (de), a : inc de	; row
	ld	a, (hl) : ld c, a : inc hl		; nseg->c
	ld	a, l : ld (de), a : inc de		; ppath_lo(domo_data 内)
	ld	a, h : ld (de), a : inc de		; ppath_hi
	ld	a, c : ld (de), a : inc de		; nseg
	xor	a : ld (de), a : inc de			; spawn=0
	ld	a, c : add a, a				; nseg*2(path スキップ)
	push	de : ld e, a : ld d, 0 : add hl, de : pop de
	djnz	.pl
	ret

; domo_find_free: 空き slot の IX を返す(Z=1 で成功, NZ で空き無し)。破壊 a,de。
domo_find_free:
	ld	ix, dm_slots
	ld	a, MAX_DOMOGRAM
.ff:	bit	0, (ix + DM_ACT)
	ret	z			; 空き(Z=1)
	inc	ix
	dec	a
	jr	nz, .ff
	or	1			; NZ(空き無し)
	ret

; domo_spawn: pending 走査。scol=col-coarse(16bit)が [0,COLS)に入った未出現を空き slot へ。
;   coarse が col を過ぎた(scol<0)未出現は見逃しとして spawned 扱い。
	IFDEF	DOMOMEAS_SPAWN
	DMWRAP	domo_spawn, domo_spawn_b, 1
domo_spawn_b:
	ELSE
domo_spawn:
	ENDIF
	IFDEF	NODOMO
	ret				; [perf baseline] Domogram を出さない(既存密度との A/B 用)
	ENDIF
	; [perf] pending は col 昇順(bake で整列)。先頭ポインタ dm_pidx から、画面右端に入った分だけ見る。
.sl:	ld	a, (dm_pn) : ld c, a : ld a, (dm_pidx) : cp c : ret nc	; 全て処理済
	ld	l, a : ld h, 0 : add hl, hl : ld d, h : ld e, l : add hl, hl : add hl, de	; *6
	ld	de, dm_pend : add hl, de : push hl : pop iy
	ld	l, (iy + 0) : ld h, 0			; col(u8)
	ld	de, (coarse) : or a : sbc hl, de		; scol = col - coarse
	jr	c, .next				; scol<0 = 見逃し → 捨てる
	ld	a, h : or a : ret nz			; まだ遠い(以降はさらに遠い)
	ld	a, l : cp COLS : ret nc			; まだ画面右外
	call	domo_find_free : ret nz			; 空き無し → 次フレーム再試行
	ld	(ix + DM_ACT), 1
	ld	(ix + DM_FTMR), 1			; 初弾は前方帯到達直後
	ld	a, (framecnt) : ld (ix + DM_LAST), a	; move の経過フレーム起点
	ld	a, (iy + 4) : ld (ix + DM_NSEG), a	; nseg
	ld	(ix + DM_WCXL), 0 : ld a, (iy + 0) : ld (ix + DM_WCXH), a	; wcx=col<<8
	ld	(ix + DM_WCYL), 0 : ld a, (iy + 1) : ld (ix + DM_WCYH), a	; wcy=row<<8
	call	dm_prev_inval
	ld	l, (iy + 2) : ld h, (iy + 3)		; ppath(domo_data 内)
	ld	a, (iy + 4) : or a
	jr	nz, .seg
	ld	(ix + DM_DIR), 16 : ld (ix + DM_DUR), 1	; 静止(vec[16]=0,0)
	jr	.setsegp
.seg:	ld	a, (hl) : inc hl : ld (ix + DM_DIR), a
	ld	a, (hl) : inc hl : ld (ix + DM_DUR), a
	dec	(ix + DM_NSEG)
.setsegp:
	ld	(ix + DM_SEGPL), l : ld (ix + DM_SEGPH), h
	ld	hl, dm_nact : inc (hl)
.next:	ld	hl, dm_pidx : inc (hl)
	jp	.sl

; domo_move: [perf] team-lead 方針。parallel array を半分(4 slot)ずつ連続処理し、
;   偶数フレーム=slot0-3 / 奇数=slot4-7(各 Domogram は 30Hz 更新)。スクロールステップ
;   フレーム(scroll_moved=1, 最重量)では move しない。各 slot は前回 move からの経過フレーム
;   n(通常2、休止直後3)ぶんを一括で進める(ベクトル×n, dur−n)=平均速度は ROM の 60Hz と同じ。
	IFDEF	DOMOMEAS
	DMWRAP	domo_move, domo_move_b, 2
domo_move_b:
	ELSE
domo_move:
	ENDIF
	IFDEF	NODOMOMOVE
	ret
	ENDIF
	IFDEF	SUPROBE
	ld	a, (scroll_moved) : or a : jr z, .nsp
	ld	a, (framecnt) : and 7 : ld bc, 0x00FE : out (c), a	; [検証] PROBE 0x82<framecnt&7>
	ld	a, 0x82 : ld bc, 0x00FF : out (c), a
.nsp:
	ENDIF
	ld	a, (dm_nact) : or a : ret z
	ld	a, (scroll_moved) : or a : ret nz	; スクロールステップは休み(次フレームで追いつく)
	ld	a, (framecnt) : ld (dmm_now), a
	and	1 : add a, a : add a, a : ld e, a : ld d, 0	; 半分 h*4
	ld	ix, dm_slots : add ix, de
	ld	b, MAX_DOMOGRAM / 2
.ml:	push	bc
	bit	0, (ix + DM_ACT) : jp z, .mn
	ld	a, (dmm_now) : ld c, a
	sub	(ix + DM_LAST) : ld (ix + DM_LAST), c	; a = n(経過フレーム)
	jp	z, .mn
	cp	DMM_NMAX + 1 : jr c, .nok
	ld	a, DMM_NMAX				; 3 を超える空白(切替直後等)は 3 で打ち切り
.nok:	ld	(dmm_n), a
	ld	c, a
	ld	a, (ix + DM_DUR) : sub c : jr z, .adv : jr c, .adv
	ld	(ix + DM_DUR), a : jr .apply		; dur -= n
.adv:	ld	(ix + DM_DUR), 0
	ld	a, (ix + DM_NSEG) : or a : jr z, .apply	; 最終セグメントは dir 継続
	dec	(ix + DM_NSEG)
	ld	l, (ix + DM_SEGPL) : ld h, (ix + DM_SEGPH)
	ld	a, (hl) : inc hl : ld (ix + DM_DIR), a
	ld	a, (hl) : inc hl : ld (ix + DM_DUR), a
	ld	(ix + DM_SEGPL), l : ld (ix + DM_SEGPH), h
.apply:	; ROM(xevi-re 確定): 毎フレーム画面上で spriteY += 2dY(横断), spriteX += 2dX(自機方向,
	;   dX>=0)。地形スクロールは加算しない(画面基準)。単位 1/32px(=wcx/wcy の frac, 1セル=256)。
	;   X1 world(画面 = world − スクロール 16/f, 自機方向 = scol 減少)に直すと
	;   wcy += 2dY, wcx += 16 − 2dX(dX=8 で地面に静止、dir0 は画面上 1px/f で自機へ、dir16 は画面静止)。
	ld	a, (ix + DM_DIR) : add a, a : ld e, a : ld d, 0
	ld	hl, domogram_vec : add hl, de : ex de, hl	; de = &vec[dir](X1 換算済 2dY, 16−2dX)
	ld	a, (de)
	IFDEF	ROT180
	neg			; [180°] row 反転 → 横断 wcy は −2dY(wcx=16−2dX は不変。vec データは未変更)
	ENDIF
	call	dmm_scale					; hl = (±)2dY*n
	ld	c, (ix + DM_WCYL) : ld b, (ix + DM_WCYH) : add hl, bc
	ld	(ix + DM_WCYL), l : ld (ix + DM_WCYH), h
	inc	de
	ld	a, (de) : call dmm_scale			; hl = (16−2dX)*n
	ld	c, (ix + DM_WCXL) : ld b, (ix + DM_WCXH) : add hl, bc
	ld	(ix + DM_WCXL), l : ld (ix + DM_WCXH), h
.mn:	inc	ix
	pop	bc
	dec	b : jp nz, .ml
	ret
DMM_NMAX	EQU	3			; 通常 n=2(半数交互)、ステップ休止直後 3、spawn 直後 1

; dmm_scale: a=符号付成分 v, (dmm_n)=n(1..3) → hl = v*n。ループ/乗算なし。bc 破壊。
dmm_scale:
	ld	c, a : rla : sbc a, a : ld b, a : ld h, b : ld l, c
	ld	a, (dmm_n) : dec a : ret z
	add	hl, bc : dec a : ret z
	add	hl, bc
	ret

; domo_draw: [perf] 1窓 + idle-skip。scol=col-coarse(16bit)。[0,COLS-1)のみ描画。
;   scol<0(左に消えた)で deactivate+消去。新 GRAM cell が DM_PREV と同じなら何もしない。
	IFDEF	DOMOMEAS
	DMWRAP	domo_draw, domo_draw_b, 3
domo_draw_b:
	ELSE
domo_draw:
	ENDIF
	IFDEF	NODOMODRAW
	ret
	ENDIF
	ld	a, (dm_nact) : or a : ret z
	; [perf] 非ステップでセルが変わり得るのは今フレーム move した半分だけ → その半分のみ見る。
	;   ステップ(ページ切替)は全 slot を新ページへ描く必要がある(半分を翌フレームに回すと、
	;   その半分が1フレーム消えて 8f 周期でちらつく: RT3 連続フレームで確認済)。
	ld	ix, dm_slots
	ld	b, MAX_DOMOGRAM
	ld	a, (scroll_moved) : or a : jr nz, .dl
	ld	a, (framecnt) : and 1 : add a, a : add a, a : ld e, a : ld d, 0
	add	ix, de
	ld	b, MAX_DOMOGRAM / 2
.dl:	push	bc
	bit	0, (ix + DM_ACT) : jp z, .dn
	ld	l, (ix + DM_WCXH) : ld h, 0		; col
	ld	de, (coarse) : or a : sbc hl, de		; scol
	jp	c, .offleft				; scol<0 → 左に消えた
	ld	a, h : or a : jp nz, .dn			; scol>=256 → 右遠く(未出現)
	ld	a, l : cp COLS - 1 : jp nc, .dn		; scol>=COLS-1 → 右外(まだ)
	ld	c, a					; c=ex(scol)
	ld	a, (ix + DM_WCYH)			; ey(row)
	cp	ROWS - 1 : jr nc, .vhide		; 2x2 が画面下(row>=24)/負行にはみ出す→非表示
	ld	l, a : ld h, 0
	add	hl, hl : add hl, hl : add hl, hl	; ×8
	ld	d, h : ld e, l
	add	hl, hl : add hl, hl : add hl, de	; ×40
	ld	e, c : ld d, 0 : add hl, de		; +ex = soff
	ld	de, (cur_pos) : add hl, de
	ld	a, h : and 0x07 : ld h, a		; hl = 新 cell(表示ページ内)
	; prev は表示ページ別(自機/敵の SPR_1WIN と同じ)。非表示ページの旧絵は消さずに残すので、
	;   ページ切替直後もそのページの旧絵が見え、描き直しがラスタに遅れても消えない。
	ld	de, 0 : bit 2, h : jr z, .pg		; h bit2 = cur_pos bit10 = 表示ページ
	ld	e, DM_PG1
.pg:	add	ix, de : push de			; page1 なら IX+DM_PG1 で page1 の prev を指す
	ld	a, (ix + DM_PREVL) : cp l : jr nz, .chg	; idle-skip: このページの prev と同じなら何もしない
	ld	a, (ix + DM_PREVH) : cp h : jr nz, .chg
	; 同じセル: 他スプライト(自分の撃った弾・上空の敵)の消去で欠けていないか 4f に1回 TVRAM を確認し、
	;   欠けていれば描き直す(TVRAM は1層なので重なりでセルが空白になり得る)。
	ld	a, (framecnt) : and 2 : jr nz, .same
	push	hl : call dm_chk : pop hl : jr z, .same
	push	hl : jr .nope				; 描き直し(消去不要)
.chg:	push	hl					; 新 cell 退避
	ld	a, (ix + DM_PREVH) : cp 0xFF : jr z, .nope
	ld	l, (ix + DM_PREVL) : ld h, a
	call	ene_clr2x2				; このページの旧絵を消す
.nope:	pop	hl
	ld	(ix + DM_PREVL), l : ld (ix + DM_PREVH), h
	ld	a, DOMO_BASE : call ene_put2x2		; 新描画(hl=cell, a=code)
.same:	pop	de
	ld	a, e : or a : jp z, .dn
	ld	de, -DM_PG1 : add ix, de
	jp	.dn
.vhide:	call	dm_erase_prev				; 縦はみ出し: 消すだけ(active は維持)
	jp	.dn
.offleft:
	call	dm_erase_prev
	ld	(ix + DM_ACT), 0
	ld	hl, dm_nact : dec (hl)
.dn:	inc	ix
	pop	bc
	dec	b : jp nz, .dl
	ret

; dm_chk: hl=TL セル。2x2 の TVRAM コードが DOMO_BASE..+3 のままなら Z、1つでも違えば NZ。
dm_chk:
	ld	a, DOMO_BASE : call .c1 : ret nz
	inc	hl : ld a, DOMO_BASE + 1 : call .c1 : ret nz
	ld	de, COLS - 1 : add hl, de : ld a, DOMO_BASE + 2 : call .c1 : ret nz
	inc	hl : ld a, DOMO_BASE + 3
.c1:	ld	e, a
	ld	a, h : and 0x07 : or TVRAM >> 8 : ld b, a : ld c, l
	in	a, (c) : cp e
	ret

; domo_fire: turret_fire と同一のゲート(countup&7, 前方帯 gnd_stop, ffreq mask)で自機狙い弾(type6)。
;   mask=ffreq_reg[DOMO_MASK-8]。発射原点が動くだけ。xevi-re: Derota/Logram と同一機構。
	IFDEF	DOMOMEAS_FIRE
	DMWRAP	domo_fire, domo_fire_b, 4
domo_fire_b:
	ELSE
domo_fire:
	ENDIF
	IFDEF	NODOMOFIRE
	ret
	ENDIF
	ld	a, (game_over) : or a : ret nz
	ld	a, (dm_nact) : or a : ret z
	; [perf] 各 slot の判定は 8f に1回(ROM countup&7 と同じ周期)だが、slot ごとに位相をずらす:
	;   framecnt&7 = k(1..7)で slot k-1、k=1 では slot7 も。ステップフレーム(&7==0)では撃たない。
	;   旧版の「全 slot を同一フレームで処理」で出ていた ef_aim_spawn 集中(最大 9.3kT)を分散。
	ld	a, (framecnt) : and 7 : ret z
	dec	a : ld e, a : ld d, 0
	ld	ix, dm_slots : add ix, de
	call	.one
	ld	a, (framecnt) : and 7 : cp 1 : ret nz
	ld	ix, dm_slots + 7
.one:	bit	0, (ix + DM_ACT) : ret z
	ld	l, (ix + DM_WCXH) : ld h, 0		; col
	ld	de, (coarse) : or a : sbc hl, de		; scol
	ret	c					; 後方(自機過ぎ)→停止
	ld	a, h : or a : ret nz			; 前方遠すぎ
	ld	a, l : cp COLS : ret nc			; 画面右外
	ld	c, a					; c=scol
	ld	a, (ix + DM_WCYH) : cp ROWS : ret nc	; 画面外の行からは撃たない
	ld	a, (ffreq_reg + 2) : ld e, a		; gnd_stop_fire
	ld	a, COLS - 1 : sub e : jr nc, .gsok : xor a
.gsok:	cp	c : jr c, .front : ret nz		; 前方帯か(scol>=しきい値)
.front:	dec	(ix + DM_FTMR) : ret nz
	ld	hl, ffreq_reg + (DOMO_MASK - 8) : ld e, (hl)	; mask(index9)
	call	rnd_step : and e : inc a : ld (ix + DM_FTMR), a	; timer=(rnd&mask)+1
	ld	a, c : ld (ef_ex), a
	ld	a, (ix + DM_WCYH) : ld (ef_ey), a
	push	ix : call ef_aim_spawn : pop ix
	ret

; domo_hit_check: ブラスター標的(hit_tcol=発射時ロックの tilemap 列, col_ey=行)が Domogram の
;   ±10px 窓に入れば破壊(gobj_find と同じ ROM check_object_on_target 準拠)。照準 2x2 と Domogram 2x2 の
;   中心差 ±10px = TL 同士で ±1 セル。Domogram は frac を持つので TL を四捨五入(=中心に最も近い 2x2)。
;   tilemap 列 = world 列(GOBJ_OFFSET=0)なので coarse 不要。carry=1 でヒット(expl は呼出側)。
;   slot は即解放し、同じ 2x2 に crater を予約(爆発終了後に GRAM 焼込)。
domo_hit_check:
	ld	a, (dm_nact) : or a : ret z		; (or a で carry=0=ミス)
	ld	ix, dm_slots
	ld	b, MAX_DOMOGRAM
.hl:	push	bc
	bit	0, (ix + DM_ACT) : jr z, .hn
	ld	a, (ix + DM_WCXL) : add a, 0x80
	ld	a, (ix + DM_WCXH) : adc a, 0 : ld c, a	; c = TL 列(四捨五入)
	ld	a, (hit_tcol) : sub c : inc a : cp 3 : jr nc, .hn	; |標的列 − 列| <= 1
	ld	a, (ix + DM_WCYL) : add a, 0x80
	ld	a, (ix + DM_WCYH) : adc a, 0 : ld b, a	; b = TL 行(四捨五入)
	ld	a, (col_ey) : sub b : inc a : cp 3 : jr nc, .hn	; |標的行 − 行| <= 1
	push	bc
	call	dm_erase_prev				; ヒット→消去+破壊
	ld	(ix + DM_ACT), 0
	ld	hl, dm_nact : dec (hl)
	pop	bc
	call	dmc_push				; crater 予約(c=col, b=row)
	pop	bc
	scf
	ret
.hn:	inc	ix
	pop	bc
	djnz	.hl
	or	a					; ミス
	ret

; ---------------------------------------------------------------------
; [Domogram crater] ROM: 被弾 → 爆発 → bomb_explosion_finished で crater 化し地形と一緒に流れる。
;   X1: 爆発終了(DMC_DELAY f)後、破壊位置 2x2 の地形タイルに ROM tile 0xA6 を重ねた合成タイルを
;   固定スロット(タイル index DMC_SLOT0〜)へ作り EMM map を差し替え → crater_enqueue で GRAM へ。
;   GRAM は地形と一緒にスクロールし画面外で自然に消える(ROM のスプライト方式と見た目等価)。
;   合成は 1フレーム1象限(~5kT)に分散。map の差し替えは area_advance で LIFO 復元(ループ再訪用)。
; queue entry(DMCREC=5): tmr(0=空き), col, row, q(次象限 0..3), k(スロット組)
DMC_DELAY	EQU	3 * EXPL_ANIM + 1	; 爆発 3コマ×EXPL_ANIM の後
DMC_QMAX	EQU	4
DMCREC		EQU	5
DMC_SLOT0	EQU	431			; 全エリア used 最大 431 → 431 以降は常に空き
DMC_KMAX	EQU	MAX_DOMO_PEND		; エリア内の最大破壊数(=最大 ndomo)

; dmc_push: c=col, b=row を予約。キュー満杯なら捨てる(同時4件超は実質起きない)。
dmc_push:
	ld	hl, dmc_q
	ld	e, DMC_QMAX
.pf:	ld	a, (hl) : or a : jr z, .put
	ld	a, l : add a, DMCREC : ld l, a : jr nc, .pn : inc h
.pn:	dec	e : jr nz, .pf
	ret
.put:	ld	(hl), DMC_DELAY : inc hl
	IFDEF	DMCPROBE
	push	af : push bc
	ld	a, 0 : ld bc, 0x00FE : out (c), a
	ld	a, 0x84 : ld bc, 0x00FF : out (c), a
	pop	bc : pop af
	ENDIF
	ld	(hl), c : inc hl
	ld	(hl), b : inc hl
	ld	(hl), 0					; q=0
	ret

; dmc_tick: 毎フレーム。待機中の tmr を減らし、焼込段階(tmr=1)の先頭1件だけ1象限進める。

dmc_tick:
	ld	iy, dmc_q
	ld	b, DMC_QMAX
	ld	c, 0					; c=今フレーム焼込済み
.tl:	ld	a, (iy + 0) : or a : jr z, .tn
	cp	1 : jr z, .bake
	dec	(iy + 0) : jr .tn
.bake:	ld	a, c : or a : jr nz, .tn		; 1フレーム1象限まで
	inc	c
	push	bc
	call	dmc_step
	pop	bc
.tn:	ld	de, DMCREC : add iy, de
	djnz	.tl
	ret

; dmc_step: iy の entry を1象限焼き込む。q=0 でスロット組 k を確保、q=4 で GRAM 登録して解放。
dmc_step:
	IFDEF	DMCPROBE
	push	af : push bc
	ld	a, 0 : ld bc, 0x00FE : out (c), a
	ld	a, 0x85 : ld bc, 0x00FF : out (c), a
	pop	bc : pop af
	ENDIF
	ld	a, (iy + 3) : or a : jr nz, .haveK
	ld	a, (dmc_k) : cp DMC_KMAX : jp nc, .drop	; スロット枯渇(起きない想定)→捨てる
	ld	(iy + 4), a : inc a : ld (dmc_k), a
.haveK:	ld	a, (iy + 3) : cp 4 : jp z, .fin
	; 象限 q のセル: col+(q&1), row+(q>>1)
	ld	e, a
	and	1 : add a, (iy + 1) : ld c, a			; c=col'
	jp	c, .skip					; col 255 を越える→対象外
	ld	a, e : srl a : add a, (iy + 2)		; a=row'
	cp	ROWS : jp nc, .skip				; 画面行外
	ld	(dmc_row), a
	ld	a, c : ld (dmc_col), a
	; src = 現在の map 値(=地形タイルの RAM アドレス)
	ld	a, (dmc_row) : call dmc_seek
	ld	bc, EMM_DAT
	in	l, (c) : in h, (c)			; hl = src tile
	; 復元記録 push(col,row,orig)
	push	hl
	ld	a, (dmc_rn) : cp DMC_KMAX * 4 : jr nc, .norec
	ld	e, a : inc a : ld (dmc_rn), a
	ld	d, 0 : ex de, hl : add hl, hl : add hl, hl	; *4
	ld	de, dmc_rest : add hl, de
	ld	a, (dmc_col) : ld (hl), a : inc hl
	ld	a, (dmc_row) : ld (hl), a : inc hl
	pop	de : push de
	ld	(hl), e : inc hl : ld (hl), d
.norec:	pop	hl					; hl = src
	; dst = TILEBASE + (DMC_SLOT0 + k*4 + q)*48
	push	hl
	ld	a, (iy + 4) : add a, a : add a, a : add a, (iy + 3)	; k*4+q (<76)
	ld	l, a : ld h, 0
	ld	de, DMC_SLOT0 : add hl, de
	add	hl, hl : add hl, hl : add hl, hl : add hl, hl	; *16
	ld	d, h : ld e, l : add hl, hl : add hl, de	; *48
	ld	de, TILEBASE : add hl, de
	ex	de, hl					; de = dst
	; pattern = dmc_pat + q*48, mask = dmc_pat + 192 + q*8
	ld	a, (iy + 3) : add a, a : add a, a : add a, a	; q*8
	push	af
	ld	l, a : ld h, 0 : ld bc, dmc_pat + 192 : add hl, bc
	ld	(dmc_mp), hl
	pop	af
	ld	l, a : ld h, 0 : ld b, h : ld c, l : add hl, hl : add hl, bc	; q*24 … *2 で q*48
	add	hl, hl : ld bc, dmc_pat : add hl, bc
	ld	(dmc_pp), hl
	pop	hl					; hl = src
	push	de					; dst 退避(map 書換用)
	push	iy					; dmc_comp は ix/iy を壊す
	call	dmc_comp
	pop	iy
	pop	de
	ld	a, (dmc_col) : ld c, a
	ld	a, (dmc_row)
	call	crater_write_cell			; map[col',row'] = dst
.skip:	inc	(iy + 3)
	ret
.fin:	; 4象限完了: 表示中なら GRAM へ(crater_pump が数フレームで描く)。入り切っていない
	;   右端側は以後のスクロールが map から描くので不要。左端(scol<1)は消えるだけなので省略。
	ld	l, (iy + 1) : ld h, 0
	ld	de, (coarse) : or a : sbc hl, de	; scol
	jr	c, .drop
	ld	a, h : or a : jr nz, .drop
	ld	a, l : or a : jr z, .drop
	cp	COLS - 1 : jr nc, .drop
	ld	(col_ex), a
	IFDEF	DMCPROBE
	ld	bc, 0x00FE : out (c), a			; [検証] PROBE 0x83<scol>: crater GRAM 登録
	ld	a, 0x83 : ld bc, 0x00FF : out (c), a
	ld	a, (col_ex)
	ENDIF
	ld	a, (iy + 1) : ld (gc_col), a
	ld	a, (iy + 2) : ld (gc_row), a
	push	iy
	call	crater_enqueue				; (保留中なら内部で flush=pump を呼ぶ)
	pop	iy
.drop:	ld	(iy + 0), 0				; entry 解放
	IFDEF	DMCPROBE
	push	af : push bc
	ld	a, 0 : ld bc, 0x00FE : out (c), a
	ld	a, 0x86 : ld bc, 0x00FF : out (c), a
	pop	bc : pop af
	ENDIF
	ret

; dmc_seek: EMM アドレス = area_map_base + col*50 + row*2 に設定。in: dmc_col, a=row。
dmc_seek:
	add	a, a : ld e, a : ld d, 0
	ld	a, (dmc_col) : ld l, a : ld h, 0
	ld	b, h : ld c, l
	add	hl, hl : add hl, hl : add hl, hl		; *8
	push	de : ld d, h : ld e, l
	add	hl, hl : add hl, de : add hl, bc		; *25
	pop	de
	add	hl, hl : add hl, de			; *50 + row*2
	ld	de, (amb_lo16) : add hl, de
	ld	a, l : ld (emm_a0), a
	ld	a, h : ld (emm_a1), a
	ld	a, (amb_hi8) : adc a, 0 : ld (emm_a2), a
	jp	set_emm_addr

; dmc_comp: hl=src tile, de=dst tile。dst = (src & ~mask[y]) | pat(6面×8行)。
dmc_comp:
	ld	ix, (dmc_pp)
	ld	c, 6
.pl:	push	hl
	ld	hl, (dmc_mp) : push hl : pop iy		; iy = mask
	pop	hl
	ld	b, 8
.ry:	ld	a, (iy + 0) : cpl : and (hl) : or (ix + 0) : ld (de), a
	inc	hl : inc de : inc ix : inc iy
	djnz	.ry
	dec	c : jr nz, .pl
	ret

; dmc_restore: area_advance(area_switch 前=旧エリアの map base のまま)で呼ぶ。
;   差し替えた map を LIFO で元の地形に戻し、キュー/スロットを初期化。
dmc_restore:
	ld	a, (dmc_rn) : or a : jr z, .clr
.rl:	dec	a : ld (dmc_rn), a
	ld	l, a : ld h, 0 : add hl, hl : add hl, hl
	ld	de, dmc_rest : add hl, de
	ld	c, (hl) : inc hl			; col
	ld	a, (hl) : inc hl			; row
	ld	e, (hl) : inc hl : ld d, (hl)		; orig
	call	crater_write_cell
	ld	a, (dmc_rn) : or a : jr nz, .rl
.clr:	xor	a : ld (dmc_k), a
	ld	hl, dmc_q : ld b, DMC_QMAX * DMCREC
.cq:	ld	(hl), a : inc hl : djnz .cq
	IFDEF	DMC_VERIFY
	; [検証] 16エリア一巡(16回目の area_advance = area16→7)の時点で全マップを OUT 0xFB へ生ダンプして停止。
	;   全エリアで crater 差し替えが LIFO 復元済なら、元の map と全バイト一致するはず。
	ld	a, (dmv_cnt) : inc a : ld (dmv_cnt), a
	cp	16 : ret nz			; 16回目の切替(area16→7)=全エリア離脱済
	ld	hl, EMM_MAPS & 0xFFFF : ld (dmv_lo), hl
	ld	a, EMM_MAPS >> 16 : ld (dmv_hi), a
	ld	b, 16
.va:	push	bc
	ld	a, (dmv_lo) : ld (emm_a0), a
	ld	a, (dmv_lo + 1) : ld (emm_a1), a
	ld	a, (dmv_hi) : ld (emm_a2), a
	call	set_emm_addr
	ld	de, EMM_MAPS_STR
.vb:	ld	bc, EMM_DAT : in a, (c)
	ld	bc, 0x00FB : out (c), a
	dec	de : ld a, d : or e : jr nz, .vb
	ld	hl, (dmv_lo) : ld de, EMM_MAPS_STR : add hl, de : ld (dmv_lo), hl
	ld	a, (dmv_hi) : adc a, 0 : ld (dmv_hi), a
	pop	bc
	djnz	.va
	di
.vh:	jr	.vh
dmv_lo:	dw	0
dmv_hi:	db	0
dmv_cnt:	db	0
	ENDIF
	ret

	IFDEF	DMC_SELFTEST
; [検証] 32f ごとに、描画中の最初の Domogram へ擬似ブラスター命中(着弾セル=その TL)。
domo_selftest:
	ld	a, (framecnt) : and 31 : ret nz
	IFDEF	DMCPROBE
	push	af : push bc
	ld	a, 0 : ld bc, 0x00FE : out (c), a
	ld	a, 0x87 : ld bc, 0x00FF : out (c), a
	pop	bc : pop af
	ENDIF
	ld	ix, dm_slots
	ld	b, MAX_DOMOGRAM
.sl:	bit	0, (ix + DM_ACT) : jr z, .sn
	ld	a, (ix + DM_PREVH) : cp 0xFF : jr z, .sn	; 未描画は対象外
	ld	a, (ix + DM_WCXH) : ld (hit_tcol), a
	ld	hl, coarse : sub (hl) : ld (col_ex), a	; 爆発表示用の画面列
	ld	a, (ix + DM_WCYH) : ld (col_ey), a
	call	domo_hit_check
	ret	nc
	jp	expl_spawn
.sn:	inc	ix
	djnz	.sl
	ret
	ENDIF

	IFDEF	DOMOPROBE
; domo_probe: active Domogram 数を PROBE 出力(spawn/move 検証用)。XMIL_PROBE で "PROBE <count>"。
domo_probe:	; 表示ページに描画済み(prev 有効)の active 数
	ld	ix, dm_slots
	ld	b, MAX_DOMOGRAM
	ld	c, 0
	ld	a, (cur_pos + 1) : and 0x04 : ld d, a
.pl:	bit	0, (ix + DM_ACT) : jr z, .pn
	ld	a, d : or a : ld a, (ix + DM_PREVH) : jr z, .ck
	ld	a, (ix + DM_PRV1H)
.ck:	cp	0xFF : jr z, .pn
	inc	c
	push	bc					; [検証] 描画済み slot の位置 PROBE 0x89<scol>, 0x8A<row>
	ld	a, (ix + DM_WCXH) : ld hl, coarse : sub (hl) : ld bc, 0x00FE : out (c), a
	ld	a, 0x89 : ld bc, 0x00FF : out (c), a
	ld	a, (ix + DM_WCYH) : ld bc, 0x00FE : out (c), a
	ld	a, 0x8A : ld bc, 0x00FF : out (c), a
	pop	bc
.pn:	inc	ix
	djnz	.pl
	ld	a, c : ld bc, 0x00FE : out (c), a	; lo = count
	ld	a, 0x80 : ld bc, 0x00FF : out (c), a	; hi=0x80 → PROBE 0x80xx(dropped と区別)
	ret
	ENDIF

; gen_domogram: Domogram 本体 PCG(DOMO_BASE=0xA4 基点, 2x2=4セル静止)を定義。sprite_init で呼ぶ。
gen_domogram:
	ld	hl, domo_pcg_src
	ld	a, DOMO_BASE
	ld	b, 4
	jp	gen_pcg_set

; 本体 PCG(boot で gen_domogram が読む)。移動ベクトル表は work 領域(emmscroll64.asm)。
domo_pcg_src:
	INCBIN	"roms/arcade/xevious-out/aerial/domogram.bin"
