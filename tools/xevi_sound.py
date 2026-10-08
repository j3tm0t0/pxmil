#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Xevious アーケードサウンド(Namco WSG 3ch)を抽出し、
   (a) WSG モデルで WAV 合成(試聴・検証用)
   (b) X1 PSG 用プレイヤデータ(.bin)へ変換。

サウンドCPU ROM = xvi_7.2c(ROMオフセット=ファイルオフセット)。波形PROM = xvi-2.7n。
RE(sub2.lst 一次情報で裏取り済):
  - シーケンス: byte_48A@0x48A の 2BLE ポインタ表 → 2バイト[pitch,duration]ペア, 0xFF=終端。
  - pitch: 高ニブル=note(byte_568@0x568 の半音16bit index), 低ニブル=octave(右シフト量)。
    0xC0=休符。freq_reg = byte_568[note] >> octave。
  - WSG出力Hz = freq_reg * 3.072MHz / 2^20 ≈ freq_reg * 2.9297(20bitアキュム,index bit[19:15])。
  - tune(sound番号)→ byte_4C0@0x4C0 triplet[ch_base,ch_count,wave_sel]→ byte_48A[ch_base..]。
  - テンポ: byte_4ED@0x4ED[snd]。note長(tick)= duration * tempo。サウンド CPU の NMI は 1 フレーム 2 回
    (MAME galaga.cpp cpu3_interrupt_callback: scanline 64/192) なので tick = 120Hz。
  - HWは毎フレーム無条件更新 → プレイヤは毎フレームのテーブル歩行でよい。
X1 PSG(AY-3-8910相当, clock=1.9968MHz=4MHz/2): period = round(clk/(16*f)) = round(124800/f), 12bit。
54xx 爆発ノイズは別MCUのため抽出不可 → PSGノイズのエンベロープで近似(明記)。
出力: roms/arcade/xevious-out/sound/(非コミット)。
"""
import os, struct, wave, math, random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROM = os.path.join(ROOT, "roms", "arcade", "xevious")
OUT = os.path.join(ROOT, "roms", "arcade", "xevious-out", "sound")

snd = open(os.path.join(ROM, "xvi_7.2c"), "rb").read()
wp = open(os.path.join(ROM, "xvi-2.7n"), "rb").read()

FREQ = [struct.unpack_from("<H", snd, 0x568 + i*2)[0] for i in range(12)]
FREQ_ALT = [struct.unpack_from("<H", snd, 0x582 + i*2)[0] for i in range(12)]
WAVES = [[(wp[w*32 + i] & 0xF) for i in range(32)] for w in range(8)]
PTR = [struct.unpack_from("<H", snd, 0x48A + i*2)[0] for i in range(27)]
TRIPLET = [(snd[0x4C0 + i*3], snd[0x4C0 + i*3 + 1], snd[0x4C0 + i*3 + 2]) for i in range(15)]
TEMPO = [snd[0x4ED + i] for i in range(15)]
# WSG出力 = freq_reg * WSG_HZ。導出: 20bitアキュム, index=bit[19:15], BCは下位16bitに
#   格納, マスタ3.072MHz → 3.072e6/2^20 = 2.9297(旋律~1.4kHz, Xeviousテーマは明るく高い
#   のでこれが妥当)。相対音程は byte_568(平均律)で確実。もし実機で1オクターブ高ければ
#   1オクターブ下げ(/2 = 96000/2^16 = 1.465)に変更可。
WSG_HZ = 3072000.0 / (2**20)   # 2.9297 Hz / freq_reg(標準 Namco WSG)
FPS = 120.0                    # サウンド CPU NMI レート(1 フレーム 2 回)
PSG_CLK = 1996800.0            # X1 AY-3-8910 clock(4MHz/2。beep実測 period256→~464Hz で裏付け)

def chan_env(slot):
    """channel-slot(byte_48A index)の (wave, vmode, attack)。byte_517 由来。"""
    wave = snd[0x517 + slot]
    vmode = snd[0x532 + slot]        # byte_517+0x1B
    attack = snd[0x54D + slot]       # byte_517+0x36
    return wave, vmode, attack

def env_vol(cnt, vmode, attack, rest):
    """ノート開始からの経過フレーム cnt → 音量 0-15(sub_307 の音量計算を再現)。"""
    if rest:
        return 0
    if vmode >= 2 and cnt < 6:
        return 15 - cnt               # 鋭いアタック 15..10
    if vmode == 1 and cnt < 8:
        return cnt * 2                # 緩いアタック 0,2,..14
    if attack == 0:
        return 10                     # 減衰なし(サステイン10)
    if cnt < attack:
        return 10
    v = 10 - (cnt - attack)           # frame=attack から10フレームで 10→0
    return v if v > 0 else 0

def read_seq(ptr_idx):
    a = PTR[ptr_idx]; out = []
    while True:
        p = snd[a]
        if p == 0xFF:
            break
        out.append((p, snd[a + 1])); a += 2
    return out

def note_hz(pitch, alt=False):
    if pitch == 0xC0:
        return 0.0
    note = pitch >> 4; octsh = pitch & 0xF
    reg = (FREQ_ALT if alt else FREQ)[note] >> octsh
    return reg * WSG_HZ

def psg_period(hz):
    if hz <= 0:
        return 0
    p = int(round(PSG_CLK / (16.0 * hz)))
    return max(1, min(4095, p))

# ---- tune / SFX 定義 ----
# ゲーム中 BGM の正体(サウンドCPU sub2.lst 解析で確定):
#   opening(tune1=main_theme_snd@0xA001): MAIN が **エリア開始時に1回** set(0x0590)。
#     sub2 loc_1E2 が鳴らし、曲末で自己 0 クリア(=1回だけ。ループしない)。メロディ。
#   arpeggio(tune E=solvalou_sound@0xA00E): MAIN handle_solvalou が **毎フレーム** set
#     (0x14F4, 自機生存中)。sub2 loc_101 が鳴らし続ける=**飛行中ずっとループ**。
#     音型 C6 C5 B6 C5 E5 C5 B6 C5 ... の分散和音(エンジン/飛行音)。死亡で停止。
#   → 正しい鳴らし方: エリア開始で opening を1回 → 飛行中 arpeggio をループ。
#     (現 X1 は fanfare/bgm をループ=オープニングが繰り返す誤り。)
#   tune0=fanfare(ゲーム開始ジングル), tune2/3/4=ハイスコア/1UP ジングル(BGMでない)。
BGM_SND = {"fanfare": 0, "opening": 1, "arpeggio": 0x0E}   # BGM系(複数ch)
SE_LIST = [                                  # snd_play_se の id 順(0..)
    ("zapper",       "tone",  0xB),
    ("blaster",      "tone",  0xC),
    ("flyhit",       "tone",  0x5),
    ("teleport",     "tone",  0x9),
    ("oneup",        "tone",  0x4),
    ("bonus",        "tone",  0xD),
    ("bacura",       "tone",  0xA),
    ("exp_aerial",   "noise", "exp_aerial"),
    ("exp_ground",   "noise", "exp_ground"),
    ("exp_solvalou", "noise", "exp_solvalou"),
]

def tune_seqs(snd_no):
    base, cnt, wsel = TRIPLET[snd_no]
    chans = []
    for i in range(cnt):
        idx = base + i
        if idx >= len(PTR):
            break
        chans.append(read_seq(idx))
    return chans, wsel, TEMPO[snd_no], base

# ---- WSG WAV 合成(wavetable, 試聴用)----
SR = 44100
def synth_channel(seq, tempo, slot):
    """チャンネルを env_vol + 実WSG波形で合成(試聴用)。"""
    wave_i, vmode, attack = chan_env(slot)
    wavetbl = WAVES[wave_i % 8]
    samples = []; phase = 0.0
    for (pitch, dur) in seq:
        frames = max(1, dur * tempo)
        nsamp = int(SR * frames / FPS)
        hz = note_hz(pitch)
        rest = (pitch == 0xC0) or hz <= 0
        step = (hz * 32.0 / SR) if hz > 0 else 0.0
        for n in range(nsamp):
            fr = int(n * FPS / SR)                       # ノート内フレーム番号
            vol = env_vol(fr, vmode, attack, rest)
            v = (wavetbl[int(phase) & 31] - 7.5) / 7.5
            samples.append(v * (vol / 15.0))
            phase += step
    return samples

def render_tune_wav(snd_no, path):
    chans, wsel, tempo, base = tune_seqs(snd_no)
    chan_samps = [synth_channel(s, tempo, base + i) for i, s in enumerate(chans)]
    n = max((len(c) for c in chan_samps), default=0)
    mix = [0.0] * n
    for c in chan_samps:
        for i, s in enumerate(c):
            mix[i] += s
    g = 0.7 / max(1, len(chans))
    pcm = b"".join(struct.pack("<h", int(max(-1, min(1, s * g)) * 32767)) for s in mix)
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm)
    return n / SR

# ---- PSG データ(.bin)新形式(音量エンベロープ対応)----
#   BGM:  ch_count(1B) + ch毎[offset:2B(LE), vmode:1B, attack:1B] + ストリーム群。
#   tone SFX: [type=0x00, vmode, attack] + 1ストリーム(ch0のみ使用)。
#   noise SFX: [type=0x01, noise_period, dur_frames, init_vol]。
#   ストリーム event=3B [period_lo, (rest<<7)|period_hi, dur_frames]。終端=dur0。
#   音量は再生時に env_vol(cnt,vmode,attack,rest) で毎フレーム算出(データに持たない)。
def encode_stream(seq, tempo):
    buf = bytearray()
    for (pitch, dur) in seq:
        frames = max(1, min(255, dur * tempo))
        if pitch == 0xC0:
            buf += bytes([0, 0x80, frames])                  # 休符(bit7)
        else:
            per = psg_period(note_hz(pitch))
            buf += bytes([per & 0xFF, (per >> 8) & 0x0F, frames])
    buf += bytes([0, 0, 0])                                  # 終端(dur=0)
    return bytes(buf)

def bgm_bin(snd_no):
    chans, wsel, tempo, base = tune_seqs(snd_no)
    n = len(chans)
    streams = [encode_stream(s, tempo) for s in chans]
    out = bytearray([n]); body = bytearray(); cur = 1 + n * 4
    for i, s in enumerate(streams):
        _, vmode, attack = chan_env(base + i)
        out += struct.pack("<H", cur) + bytes([vmode, attack])
        body += s; cur += len(s)
    return bytes(out) + bytes(body)

def sfx_tone_bin(snd_no):
    chans, wsel, tempo, base = tune_seqs(snd_no)
    _, vmode, attack = chan_env(base)
    return bytes([0x00, vmode, attack]) + encode_stream(chans[0], tempo)

def sfx_noise_bin(name):
    #   [type=0x01, noise_period, dur_frames, init_vol, dstep_lo, dstep_hi]
    #   dstep = (init_vol<<8)//dur = 8.8 固定小数の毎フレーム減衰量。
    p = EXPLOSIONS[name]
    iv, dur = p["vol"], p["dur"]
    dstep = (iv << 8) // max(1, dur)
    return bytes([0x01, p["nperiod"], dur, iv, dstep & 0xFF, (dstep >> 8) & 0xFF])

# ---- 爆発音(54xx 抽出不可 → PSG ノイズ + 減衰で近似。params は設計値)----
#   PSG: R6=noise period(0-31, 大=低く唸る), R7 でノイズをchに有効, R(8+ch)=音量(減衰)。
#   noise周波数 = clock/(16*nperiod)。
EXPLOSIONS = {
    "exp_aerial":   dict(nperiod=10, dur=12, vol=10),   # 空中敵(小・速)
    "exp_ground":   dict(nperiod=16, dur=20, vol=13),   # 地上物(中)
    "exp_solvalou": dict(nperiod=22, dur=40, vol=15),   # ソルバルウ(大・長・最大)
}
def gen_explosion_wav(path, nperiod, dur, vol):
    rng = random.Random(0x5EED)
    nsamp = int(SR * dur / FPS)
    noise_hz = PSG_CLK / (16.0 * max(1, nperiod))
    step = noise_hz / SR
    cur = 1.0; acc = 0.0; out = []
    for n in range(nsamp):
        acc += step
        while acc >= 1.0:
            cur = 1.0 if rng.random() < 0.5 else -1.0
            acc -= 1.0
        env = (1.0 - n / nsamp) ** 2     # 二乗減衰(アタック即・緩やか尾)
        out.append(cur * (vol / 15.0) * env)
    pcm = b"".join(struct.pack("<h", int(max(-1, min(1, s)) * 32767)) for s in out)
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm)
    return nsamp / SR

def main():
    os.makedirs(OUT, exist_ok=True)
    print("=== Xevious サウンド抽出(音量エンベロープ + SFX)===")
    print("freq表(byte_568):", " ".join("%04X" % f for f in FREQ))
    # BGM系(fanfare, bgm)
    for name, snd_no in BGM_SND.items():
        chans, wsel, tempo, base = tune_seqs(snd_no)
        render_tune_wav(snd_no, os.path.join(OUT, "xevi_%s.wav" % name))
        b = bgm_bin(snd_no)
        open(os.path.join(OUT, "xevi_%s.bin" % name), "wb").write(b)
        envs = [chan_env(base + i)[1:] for i in range(len(chans))]
        print("  BGM %-8s snd%X ch=%d tempo=%2d env(vmode,attack)=%s psg=%dB"
              % (name, snd_no, len(chans), tempo, envs, len(b)))
    # SFX(id 順)。se_<id>_<name>.bin + WAV
    print("SFX(snd_play_se id 順):")
    for sid, (name, typ, ref) in enumerate(SE_LIST):
        if typ == "tone":
            b = sfx_tone_bin(ref)
            render_tune_wav(ref, os.path.join(OUT, "xevi_%s.wav" % name))
            _, vmode, attack = chan_env(TRIPLET[ref][0])
            info = "tone snd%X vmode=%d attack=%d" % (ref, vmode, attack)
        else:
            b = sfx_noise_bin(ref)
            p = EXPLOSIONS[ref]
            gen_explosion_wav(os.path.join(OUT, "xevi_%s.wav" % name), **p)
            info = "noise nperiod=%d dur=%d vol=%d" % (p["nperiod"], p["dur"], p["vol"])
        open(os.path.join(OUT, "se_%02d_%s.bin" % (sid, name)), "wb").write(b)
        print("  id%2d %-13s %-30s psg=%dB" % (sid, name, info, len(b)))
    print("出力: roms/arcade/xevious-out/sound/(xevi_*.wav 試聴, xevi_bgm/fanfare.bin, se_NN_*.bin)")
    print("※音色=PSG矩形(WSG波形は未再現)。tone SFX再生はch0のみ(多ch SFXの和声は簡略)。")
    print("※音量エンベロープ(byte_517 vmode/attack)はプレイヤ側 env_vol で毎フレーム再現。")

if __name__ == "__main__":
    main()
