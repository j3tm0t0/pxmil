; sndplay.inc - X1 PSG サウンドプレイヤ(独立モジュール, emmscroll64.asm 非依存)
;   VBLANK ごとに snd_tick を1回呼ぶ。BGM(最大3ch)+ 効果音(1ch, SFX優先でPSG Cを奪取)。
;   音量エンベロープ(byte_517 vmode/attack, env_vol)を毎フレーム再現。
;
;   === API ===
;     snd_init                : 最初に1回。PSG 無音化 + ミキサ初期化。
;     snd_play_bgm (IX=BGMデータ): BGM開始(PSG A/B/C)。
;     snd_play_se  (HL=SEデータ先頭): 効果音開始。PSG C を奪取(tone/noise)、終了後
;                               BGM ch2 へ復帰。id→ptr 変換は caller 側(se_table 不要):
;                                 ld hl, se_table : ld a, id : add a,a : ... ld hl,(se_table+2*id)
;                               SEデータは RAM/EMM/バンクどこでも可(ポインタ渡し)。
;     snd_tick                : 毎フレーム更新(BGM全ch + SFX)。
;     snd_bgm_active          : A=BGMの残アクティブch数(0=BGM終了)。
;   破壊: AF,BC,DE,HL,IY(必要なら呼び出し側で退避)。
;
;   === 最大サイクル(4MHz, 実測histogram)===  snd_tick 最悪 ~4048T。
;       最頻~1700T、裾が4048Tまで滑らか(多voice同時ノート切替+エンベロープ更新)。
;       予算5000T内(emm-scroll 最重フレーム79%の残り ~14000T)。
;
;   データ形式(xevi_sound.py 出力):
;     BGM:  ch_count(1B) + ch毎[offset:2B(LE), vmode:1B, attack:1B] + ストリーム群。
;     tone SFX: [0x00, vmode, attack] + 1ストリーム。
;     noise SFX:[0x01, noise_period, dur, init_vol, dstep_lo, dstep_hi]。
;     ストリーム event=3B [period_lo, (rest<<7)|period_hi, dur_frames]。終端=dur0。

PSG_REG		EQU	0x1C00		; レジスタ選択(OUT)
PSG_DAT		EQU	0x1B00		; データ(OUT)
; voice 構造体(10B)
V_PTRL	EQU 0
V_PTRH	EQU 1
V_CNT	EQU 2			; ノート開始からの経過 tick (120Hz、1 フレーム 2 tick)
V_NDUR	EQU 3			; 現ノートの総フレーム
V_PERL	EQU 4
V_PERH	EQU 5
V_REST	EQU 6
V_VMODE	EQU 7
V_ATTACK EQU 8
V_ACT	EQU 9
VSIZE	EQU 10

; ---- PSG レジスタ書き込み: D=reg, E=値 ----
psg_set:
	IFDEF	SFXTRACE
	; [SFXTRACE] R7(ミキサ) を shadow 保存、R10(vol C)書込を計数
	push	af
	ld	a, d
	cp	7
	jr	nz, .nps7
	ld	a, e
	ld	(r7_shadow), a
.nps7:	ld	a, d
	cp	10
	jr	nz, .nps10
	push	hl
	ld	hl, (psgc_cnt)
	inc	hl
	ld	(psgc_cnt), hl
	pop	hl
.nps10:	pop	af
	ENDIF
	ld	bc, PSG_REG
	out	(c), d
	ld	bc, PSG_DAT
	out	(c), e
	ret

; ---- 初期化 ----
snd_init:
	xor	a
	ld	(bgm_nch), a
	ld	(sfx_type), a
	ld	e, 0
	ld	d, 8
	call	psg_set			; R8=0
	ld	d, 9
	call	psg_set
	ld	d, 10
	call	psg_set
	ld	d, 7
	ld	e, 0x38			; R7: tone ABC on, noise off
	call	psg_set
	ret

; ---- env_vol: IY=voice -> A=音量(0-15) ----
env_vol:
	ld	a, (iy+V_REST)
	or	a
	jr	nz, ev_zero		; 休符 -> 0
	ld	b, (iy+V_CNT)		; B=cnt
	ld	a, (iy+V_VMODE)
	cp	2
	jr	c, ev_nv2		; vmode<2
	ld	a, b
	cp	6
	jr	nc, ev_nv2		; cnt>=6
	ld	a, 15			; vmode>=2, cnt<6 -> 15-cnt
	sub	b
	ret
ev_nv2:
	ld	a, (iy+V_VMODE)
	cp	1
	jr	nz, ev_sus		; vmode!=1
	ld	a, b
	cp	8
	jr	nc, ev_sus		; cnt>=8
	ld	a, b			; vmode==1, cnt<8 -> cnt*2
	add	a, a
	ret
ev_sus:
	ld	a, (iy+V_ATTACK)
	or	a
	jr	z, ev_ten		; attack==0 -> 10
	ld	c, a			; C=attack
	ld	a, b
	cp	c
	jr	c, ev_ten		; cnt<attack -> 10
	ld	a, b			; v = 10-(cnt-attack)
	sub	c
	ld	c, a			; C=cnt-attack
	ld	a, 10
	sub	c
	ret	nc			; >=0
	xor	a			; <0 -> 0
	ret
ev_ten:
	ld	a, 10
	ret
ev_zero:
	xor	a
	ret

; ---- voice の次ノートをロード(IY=voice)。PSG書込みはしない ----
voice_load:
	ld	l, (iy+V_PTRL)
	ld	h, (iy+V_PTRH)
	ld	e, (hl)			; perL
	inc	hl
	ld	d, (hl)			; (rest<<7)|perHi
	inc	hl
	ld	a, (hl)			; dur
	inc	hl
	ld	(iy+V_PTRL), l
	ld	(iy+V_PTRH), h
	or	a
	jr	nz, vl_ok
	ld	(iy+V_ACT), a		; dur==0 -> act=0
	ret
vl_ok:
	ld	(iy+V_NDUR), a
	ld	(iy+V_PERL), e
	ld	a, d
	and	0x0F
	ld	(iy+V_PERH), a
	ld	a, d
	and	0x80
	ld	(iy+V_REST), a
	xor	a
	ld	(iy+V_CNT), a		; cnt=0
	ret

; ---- period 書込み(IY=voice, cur_pch=PSG ch, cur_mute!=0 でPSG書込みスキップ) ----
voice_wperiod:
	ld	a, (cur_mute)
	or	a
	ret	nz
	ld	a, (cur_pch)
	add	a, a
	ld	d, a			; reg = pch*2
	ld	e, (iy+V_PERL)
	call	psg_set
	ld	a, (cur_pch)
	add	a, a
	inc	a
	ld	d, a			; reg = pch*2+1
	ld	e, (iy+V_PERH)
	call	psg_set
	ret

; ---- vol 書込み(env_vol 算出, IY=voice, cur_pch, cur_mute!=0 でスキップ)----
voice_wvol:
	ld	a, (cur_mute)
	or	a
	ret	nz
	call	env_vol
	ld	e, a
	ld	a, (cur_pch)
	add	a, 8
	ld	d, a
	call	psg_set
	ret

; ---- voice 1フレーム更新(IY=voice, cur_pch 設定済)----
voice_tick:
	ld	a, (iy+V_ACT)
	or	a
	ret	z
	; アーケードのサウンド CPU NMI は 1 フレーム 2 回 (scanline 64/192 = 120Hz)。
	; cnt/ndur はその tick 単位なので 1 フレームで 2 tick 進める
	ld	a, (iy+V_CNT)
	ld	b, (iy+V_NDUR)
	inc	a
	cp	b
	jr	nc, vt_next1		; 1 tick 目で終了 -> 次ノートは 1 tick 経過
	inc	a
	cp	b
	jr	c, vt_hold		; cnt<ndur
	xor	a			; 2 tick 目で終了 -> 次ノートは 0 tick 経過
	jr	vt_next
vt_next1:
	ld	a, 1
vt_next:
	push	af
	call	voice_load		; 次ノート
	pop	bc			; B=経過 tick
	ld	a, (iy+V_ACT)
	or	a
	jr	z, vt_ended
	ld	(iy+V_CNT), b
	call	voice_wperiod
	jr	vt_wvol
vt_hold:
	ld	(iy+V_CNT), a
vt_wvol:
	call	voice_wvol
	ret
vt_ended:
	ld	a, (cur_mute)
	or	a
	ret	nz
	xor	a			; 終了: vol 0
	ld	e, a
	ld	a, (cur_pch)
	add	a, 8
	ld	d, a
	call	psg_set
	ret

; ---- BGM 開始(IX=BGMデータ)----
snd_play_bgm:
	push	ix
	pop	hl
	ld	(bgm_base), hl
	ld	a, (hl)
	ld	(bgm_nch), a		; ch_count
	or	a
	ret	z
	ld	(sp_cnt), a
	inc	hl			; -> ch毎ヘッダ[off:2, vmode, attack]
	ld	(sp_hdr), hl
	ld	iy, bgm_v0
	xor	a
	ld	(sp_i), a
spb_lp:
	ld	hl, (sp_hdr)
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE=offset
	inc	hl
	ld	a, (hl)
	ld	(iy+V_VMODE), a
	inc	hl
	ld	a, (hl)
	ld	(iy+V_ATTACK), a
	inc	hl
	ld	(sp_hdr), hl
	ld	hl, (bgm_base)
	add	hl, de			; ptr = base+offset
	ld	(iy+V_PTRL), l
	ld	(iy+V_PTRH), h
	ld	a, 1
	ld	(iy+V_ACT), a
	; cur_pch = sp_i
	ld	a, (sp_i)
	ld	(cur_pch), a
	call	voice_load
	ld	a, (iy+V_ACT)
	or	a
	jr	z, spb_nx
	call	voice_wperiod
	call	voice_wvol
spb_nx:
	ld	de, VSIZE
	add	iy, de
	ld	a, (sp_i)
	inc	a
	ld	(sp_i), a
	ld	b, a
	ld	a, (sp_cnt)
	dec	a
	ld	(sp_cnt), a
	jr	nz, spb_lp
	ret

; ---- SE 開始(HL=SEデータ先頭)----
;   snd_play_se    : 通常優先度(低)。
;   snd_play_se_hi : 高優先度。高優先 SFX 再生中は低優先の新規 SFX を無視する
;                    (自機爆発=死亡音が後続の地上/空中/カンカン SFX に PSG C を
;                     奪われて途切れないように。死亡演出を目立たせるため)。
snd_play_se_hi:
	ld	a, 1
	ld	(sfx_prio_req), a
	jr	sps_entry
snd_play_se:
	xor	a
	ld	(sfx_prio_req), a
sps_entry:
	ld	a, (sfx_type)
	or	a
	jr	z, sps_accept		; 無音 → 受理
	ld	a, (sfx_prio_req)
	or	a
	jr	nz, sps_accept		; 新規が高優先 → 上書き許可
	ld	a, (sfx_prio)
	or	a
	ret	nz			; 高優先 SFX 再生中 + 低優先新規 → 無視
sps_accept:
	IFDEF	SFX2CH
	ld	a, (sfx_type)		; 2ch SFX を中断するなら先に PSG B を解放
	cp	3			;   (sfx_v2 が取り残されて C#7 がドローンするのを防ぐ)
	jr	nz, .nob
	push	hl
	call	sfx_release_b
	pop	hl
.nob:
	ENDIF
	ld	a, (sfx_prio_req)
	ld	(sfx_prio), a
	IFDEF	SFX2CH
	ld	(se_base), hl		; データ先頭(2ch offset 用)
	ENDIF
	IFDEF	SFXTRACE
	push	hl
	ld	hl, (sps_cnt)		; [SFXTRACE] snd_play_se 呼出回数
	inc	hl
	ld	(sps_cnt), hl
	pop	hl
	ENDIF
	ld	a, (hl)
	inc	hl
	or	a
	jr	z, sps_tone		; type0 -> tone 1ch
	cp	1
	jr	z, sps_noise		; type1 -> noise
	IFDEF	SFX2CH
	cp	2
	jp	z, sps_tone2		; type2 -> tone 2ch(PSG B+C)
	ENDIF
	jr	sps_noise		; 既定(未知 type)は noise 扱い
sps_tone:
	; tone SFX: [vmode, attack] + stream
	ld	a, (hl)
	ld	(sfx_v+V_VMODE), a
	inc	hl
	ld	a, (hl)
	ld	(sfx_v+V_ATTACK), a
	inc	hl
	ld	(sfx_v+V_PTRL), hl	; L->PTRL, H->PTRH(連続)
	ld	a, 1
	ld	(sfx_v+V_ACT), a
	ld	a, 2
	ld	(cur_pch), a		; PSG C
	; [SFX修正] tone SFX 開始時に tone C を有効化(R7=0x38)。sps_noise は R7=0x1C
	;   (tone C off/noise C on)に設定するので、直前が noise SFX だと tone C が無効の
	;   まま残り tone SFX(kankan/zapper/blaster)が無音になる。noise の R7 設定と対称に
	;   tone 側も開始時に設定する(終了時の復帰と合わせて全 SFX が鳴り続ける)。
	ld	d, 7
	ld	e, 0x38
	call	psg_set
	ld	iy, sfx_v
	call	voice_load
	ld	a, (sfx_v+V_ACT)
	or	a
	jr	z, sps_t0
	call	voice_wperiod
	call	voice_wvol
sps_t0:
	ld	a, 1
	ld	(sfx_type), a		; tone
	ret
sps_noise:
	; [noise_period, dur, init_vol, dstep_lo, dstep_hi]
	ld	a, (hl)
	ld	(n_period), a
	inc	hl
	ld	a, (hl)
	ld	(n_dur), a
	inc	hl
	ld	a, (hl)
	ld	(n_vol+1), a		; vfx hi = init_vol
	xor	a
	ld	(n_vol), a		; vfx lo = 0
	inc	hl
	ld	a, (hl)
	ld	(n_step), a
	inc	hl
	ld	a, (hl)
	ld	(n_step+1), a
	; R6 = noise period
	ld	d, 6
	ld	a, (n_period)
	ld	e, a
	call	psg_set
	; R7 = tone A,B on / tone C off / noise C on = 0x1C
	ld	d, 7
	ld	e, 0x1C
	call	psg_set
	ld	a, 2
	ld	(sfx_type), a		; noise
	ret

	IFDEF	SFX2CH
; ---- 2ch tone SFX 開始(type=0x02)。PSG B(sfx_v2)+ PSG C(sfx_v)----
;   データ: [0x02, vm0, at0, off0:2, vm1, at1, off1:2] + stream0 + stream1。
;     off は SE データ先頭(se_base)からのオフセット。HL は type の次(vm0)を指す。
;   arpeggio(PSG A のみ)と共存可。opening(3ch)中は PSG B+C を一時奪取。
sps_tone2:
	; --- voice C (sfx_v) ---
	ld	a, (hl)
	ld	(sfx_v+V_VMODE), a
	inc	hl
	ld	a, (hl)
	ld	(sfx_v+V_ATTACK), a
	inc	hl
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE=off0
	inc	hl
	push	hl
	ld	hl, (se_base)
	add	hl, de
	ld	(sfx_v+V_PTRL), hl	; L->PTRL, H->PTRH
	pop	hl
	ld	a, 1
	ld	(sfx_v+V_ACT), a
	; --- voice B (sfx_v2) ---
	ld	a, (hl)
	ld	(sfx_v2+V_VMODE), a
	inc	hl
	ld	a, (hl)
	ld	(sfx_v2+V_ATTACK), a
	inc	hl
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE=off1
	ld	hl, (se_base)
	add	hl, de
	ld	(sfx_v2+V_PTRL), hl
	ld	a, 1
	ld	(sfx_v2+V_ACT), a
	; R7 = tone ABC on
	ld	d, 7
	ld	e, 0x38
	call	psg_set
	xor	a
	ld	(cur_mute), a
	; voice C ロード/発音
	ld	a, 2
	ld	(cur_pch), a
	ld	iy, sfx_v
	call	voice_load
	ld	a, (sfx_v+V_ACT)
	or	a
	jr	z, .skipc
	call	voice_wperiod
	call	voice_wvol
.skipc:
	; voice B ロード/発音
	ld	a, 1
	ld	(cur_pch), a
	ld	iy, sfx_v2
	call	voice_load
	ld	a, (sfx_v2+V_ACT)
	or	a
	jr	z, .skipb
	call	voice_wperiod
	call	voice_wvol
.skipb:
	ld	a, 3
	ld	(sfx_type), a		; 3 = tone 2ch
	ret
	ENDIF

; ---- 毎フレーム更新 ----
snd_tick:
	xor	a
	ld	(cur_mute), a		; voice0/1 は非mute
	ld	a, (bgm_nch)
	or	a
	jr	z, st_sfx
	; BGM voice 0..nch-1 -> PSG 0..nch-1
	ld	iy, bgm_v0
	xor	a
	ld	(cur_pch), a
	call	voice_tick
	ld	a, (bgm_nch)
	cp	2
	jr	c, st_sfx
	ld	iy, bgm_v1
	ld	a, 1
	ld	(cur_pch), a
	IFDEF	SFX2CH
	ld	a, (sfx_type)		; 2ch SFX(type3)稼働中は PSG B も mute
	cp	3
	jr	nz, .v1nm
	ld	(cur_mute), a
.v1nm:
	ld	a, 1
	ld	(cur_pch), a
	ENDIF
	call	voice_tick
	IFDEF	SFX2CH
	xor	a
	ld	(cur_mute), a		; PSG C 判定のため戻す
	ENDIF
	ld	a, (bgm_nch)
	cp	3
	jr	c, st_sfx
	; voice2: SFX 稼働中は mute(状態は進めるが PSG C へ書かない)
	ld	a, (sfx_type)
	ld	(cur_mute), a
	ld	iy, bgm_v2
	ld	a, 2
	ld	(cur_pch), a
	call	voice_tick
st_sfx:
	xor	a
	ld	(cur_mute), a		; SFX は非mute
	ld	a, (sfx_type)
	or	a
	ret	z
	IFDEF	SFX2CH
	cp	3
	jp	z, st_tone2		; 2ch tone SFX
	ENDIF
	cp	2
	jr	z, st_noise
	; tone SFX -> PSG C(上書き)
	ld	iy, sfx_v
	ld	a, 2
	ld	(cur_pch), a
	call	voice_tick
	ld	a, (sfx_v+V_ACT)
	or	a
	ret	nz
	IFNDEF	OLDSFXEND
	call	sfx_end_restore		; R7/vol C 復帰 + BGM period 残留防止
	ENDIF
	xor	a			; SFX終了
	ld	(sfx_type), a
	ld	(sfx_prio), a
	ret
st_noise:
	call	noise_tick
	ret

; ---- SFX 終了時の PSG 復帰(PSG C)----
;   R7(ミキサ)を 0x38 に戻す。PSG C が BGM channel(bgm_nch>=3=opening)なら
;   その voice の period/vol を即書き戻す。これを怠ると SFX の残留 period のまま
;   BGM の vol だけ書かれ、次の BGM ノート境界まで誤ピッチが鳴る(潜在バグ修正)。
;   PSG C が BGM でない(arpeggio 1ch 等)なら vol C=0。
sfx_end_restore:
	ld	d, 7
	ld	e, 0x38
	call	psg_set
	ld	a, (bgm_nch)
	cp	3
	jr	c, ser_coff		; bgm_nch<3 → PSG C は BGM でない
	xor	a
	ld	(cur_mute), a
	ld	a, 2
	ld	(cur_pch), a
	ld	iy, bgm_v2
	ld	a, (iy+V_ACT)
	or	a
	jr	z, ser_coff		; voice 終了 → vol C=0
	call	voice_wperiod		; period 復帰(残留ピッチ防止)
	jp	voice_wvol		; vol も現 env で復帰(末尾呼出)
ser_coff:
	ld	d, 10
	ld	e, 0
	jp	psg_set

	IFDEF	SFX2CH
; ---- 2ch tone SFX 更新(PSG C=sfx_v, PSG B=sfx_v2)----
st_tone2:
	ld	iy, sfx_v
	ld	a, 2
	ld	(cur_pch), a
	call	voice_tick
	ld	iy, sfx_v2
	ld	a, 1
	ld	(cur_pch), a
	call	voice_tick
	ld	a, (sfx_v+V_ACT)		; 両 voice 終了で SFX 終了
	ld	hl, sfx_v2+V_ACT
	or	(hl)
	ret	nz
	call	sfx_end_restore2
	xor	a
	ld	(sfx_type), a
	ld	(sfx_prio), a
	ret

; ---- 2ch SFX 終了時の PSG 復帰(PSG B+C)。sfx_end_restore の 2ch 版 ----
sfx_end_restore2:
	ld	d, 7
	ld	e, 0x38
	call	psg_set
	; PSG C (bgm_v2)
	ld	a, (bgm_nch)
	cp	3
	jr	c, .coff
	xor	a
	ld	(cur_mute), a
	ld	a, 2
	ld	(cur_pch), a
	ld	iy, bgm_v2
	ld	a, (iy+V_ACT)
	or	a
	jr	z, .coff
	call	voice_wperiod
	call	voice_wvol
	jr	sfx_release_b
.coff:
	ld	d, 10
	ld	e, 0
	call	psg_set
	; fall through → PSG B 解放
; ---- PSG B を解放(2ch SFX の終了・中断共通)。bgm_v1(PSG B)が BGM なら
;   period/vol を復帰、でなければ vol B=0。sfx_v2 を無効化(残留ドローン防止)。
;   ※2ch SFX が別 SFX に中断されると sfx_v2 が V_ACT=1 のまま取り残され、
;     R9/R2/R3 がカンカンの最後の音(C#7)を鳴らし続けるため、中断時も必ず呼ぶ。
sfx_release_b:
	xor	a
	ld	(sfx_v2+V_ACT), a
	ld	a, (bgm_nch)
	cp	2
	jr	c, .boff
	xor	a
	ld	(cur_mute), a
	ld	a, 1
	ld	(cur_pch), a
	ld	iy, bgm_v1
	ld	a, (iy+V_ACT)
	or	a
	jr	z, .boff
	call	voice_wperiod
	jp	voice_wvol
.boff:
	ld	d, 9
	ld	e, 0
	jp	psg_set
	ENDIF

; ---- noise SFX 更新 ----
noise_tick:
	ld	a, (n_dur)
	dec	a
	ld	(n_dur), a
	jr	nz, nt_cont
	; 終了: R7/vol C 復帰(BGM period 残留防止含む), sfx_type/prio=0
	call	sfx_end_restore
	xor	a
	ld	(sfx_type), a
	ld	(sfx_prio), a
	ret
nt_cont:
	ld	hl, (n_vol)		; vfx(8.8)
	ld	de, (n_step)
	or	a
	sbc	hl, de
	jr	nc, nt_ok
	ld	hl, 0
nt_ok:
	ld	(n_vol), hl
	ld	a, h			; vol = vfx>>8
	and	0x0F
	ld	e, a
	ld	d, 10			; R10 = vol C
	call	psg_set
	ret

; ---- BGM アクティブ ch 数(A)----
snd_bgm_active:
	ld	a, (bgm_nch)
	or	a
	ret	z
	ld	c, 0
	ld	iy, bgm_v0
	ld	b, a
sba_lp:
	ld	a, (iy+V_ACT)
	add	a, c
	ld	c, a
	ld	de, VSIZE
	add	iy, de
	djnz	sba_lp
	ld	a, c
	ret

; ---- 状態 ----
bgm_nch:	db 0
bgm_base:	dw 0
bgm_v0:		ds VSIZE
bgm_v1:		ds VSIZE
bgm_v2:		ds VSIZE
sfx_type:	db 0			; 0=none 1=tone 2=noise 3=tone2ch(SFX2CH)
sfx_v:		ds VSIZE
sfx_prio:	db 0			; 現 SFX の優先度(0=通常, 1=高=死亡音)
sfx_prio_req:	db 0			; snd_play_se 呼出の要求優先度
	IFDEF	SFX2CH
sfx_v2:		ds VSIZE		; 2ch SFX の 2本目(PSG B)
se_base:	dw 0			; SE データ先頭(2ch offset 用)
	ENDIF
n_period:	db 0
n_dur:		db 0
n_vol:		dw 0			; 8.8 固定小数
n_step:		dw 0
cur_pch:	db 0
cur_mute:	db 0
sp_hdr:		dw 0
sp_cnt:		db 0
sp_i:		db 0
	IFDEF	SFXTRACE
sps_cnt:	dw 0			; [SFXTRACE] snd_play_se 呼出累計
psgc_cnt:	dw 0			; [SFXTRACE] R10(vol C)書込累計
r7_shadow:	db 0x38			; [SFXTRACE] 最後に書いた R7(ミキサ)
	ENDIF
