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
  - テンポ: byte_4ED@0x4ED[snd]。note長(frame)= duration * tempo(60fps NMI)。
  - HWは毎フレーム無条件更新 → プレイヤは毎フレームのテーブル歩行でよい。
X1 PSG(AY-3-8910相当, clock=2MHz): period = round(2e6/(16*f)) = round(125000/f), 12bit。
54xx 爆発ノイズは別MCUのため抽出不可 → PSGノイズのエンベロープで近似(明記)。
出力: roms/arcade/xevious-out/sound/(非コミット)。
"""
import os, struct, wave, math

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
# ★オクターブ較正定数★ WSG出力 = freq_reg * WSG_HZ。相対音程は byte_568(平均律)で
#   確実だが、絶対オクターブは実機照合が必要。候補: 96000/2^16=1.465(旋律~700Hz,自然),
#   3.072e6/2^20=2.93(旋律~1.4kHz,明るめ)。倍=1オクターブ。既定は 1.465。
WSG_HZ = 96000.0 / 65536.0     # 1.4648 Hz / freq_reg(=96kHz/2^16)
FPS = 60.0
PSG_CLK = 2000000.0            # X1 AY-3-8910 clock

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

# ---- tune 定義(prototype スコープ)----
#   sound番号 -> (name, 'bgm'|'sfx')
TUNES = {
    0:  ("fanfare", "bgm"),   # 開始テーマ(2ch)
    1:  ("bgm",     "bgm"),   # ゲーム中BGM(3ch, ゼビウスのテーマ)
    0xB:("zapper",  "sfx"),   # ザッパー
    0xC:("blaster", "sfx"),   # ブラスター
}

def tune_seqs(snd_no):
    base, cnt, wsel = TRIPLET[snd_no]
    chans = []
    for i in range(cnt):
        idx = base + i
        if idx >= len(PTR):
            break
        chans.append(read_seq(idx))
    return chans, wsel, TEMPO[snd_no]

# ---- WSG WAV 合成(wavetable, 試聴用)----
SR = 44100
def synth_channel(seq, tempo, wave_idx):
    wavetbl = WAVES[wave_idx % 8]
    samples = []
    phase = 0.0
    for (pitch, dur) in seq:
        hz = note_hz(pitch)
        nframes = max(1, dur * tempo)
        nsamp = int(SR * nframes / FPS)
        if hz <= 0:
            samples.extend([0.0] * nsamp); continue
        step = hz * 32.0 / SR     # wavetable 進行/サンプル
        # 簡易デケイエンベロープ(アタック即時, 緩やか減衰)
        for n in range(nsamp):
            idx = int(phase) & 31
            v = (wavetbl[idx] - 7.5) / 7.5
            env = 1.0 - 0.3 * (n / nsamp)   # 軽い減衰
            samples.append(v * env)
            phase += step
    return samples

def render_tune_wav(snd_no, path, wave_override=None):
    chans, wsel, tempo = tune_seqs(snd_no)
    chan_samps = []
    for ci, seq in enumerate(chans):
        wi = wave_override if wave_override is not None else 0
        chan_samps.append(synth_channel(seq, tempo, wi))
    n = max(len(c) for c in chan_samps) if chan_samps else 0
    mix = [0.0] * n
    for c in chan_samps:
        for i, s in enumerate(c):
            mix[i] += s
    g = 0.9 / max(1, len(chans))
    pcm = b"".join(struct.pack("<h", int(max(-1, min(1, s * g)) * 32767)) for s in mix)
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm)
    return n / SR

# ---- PSG プレイヤデータ(.bin)----
#   ヘッダ: ch_count(1B)。続いて ch_count 本のチャンネルストリーム。
#   各チャンネル: event 列。1 event = 3B [period_lo, (vol<<4)|period_hi, duration_frames]。
#     休符: vol=0。終端: duration=0。
#   チャンネルは順に連結、各末尾に終端(00 00 00)。オフセット表を先頭に置く。
VOL = 12   # 固定音量(prototype; 本来は byte_517 エンベロープ)
def tune_psg_bin(snd_no):
    chans, wsel, tempo = tune_seqs(snd_no)
    streams = []
    for seq in chans:
        buf = bytearray()
        for (pitch, dur) in seq:
            hz = note_hz(pitch)
            per = psg_period(hz)
            vol = 0 if hz <= 0 else VOL
            frames = max(1, min(255, dur * tempo))
            buf += bytes([per & 0xFF, ((vol & 0xF) << 4) | ((per >> 8) & 0xF), frames])
        buf += bytes([0, 0, 0])   # 終端
        streams.append(bytes(buf))
    # レイアウト: ch_count(1B) + ch_count*2B オフセット + ストリーム連結
    head = bytes([len(streams)])
    off0 = 1 + len(streams) * 2
    offs = bytearray(); body = bytearray(); cur = off0
    for s in streams:
        offs += struct.pack("<H", cur); body += s; cur += len(s)
    return head + bytes(offs) + bytes(body)

def main():
    os.makedirs(OUT, exist_ok=True)
    print("=== Xevious サウンド抽出 ===")
    print("freq表(byte_568):", " ".join("%04X" % f for f in FREQ))
    print("波形PROM xvi-2.7n: 8波×32サンプル(w1=矩形, w0=サイン系)")
    manifest = []
    for snd_no, (name, kind) in TUNES.items():
        chans, wsel, tempo = tune_seqs(snd_no)
        wav = os.path.join(OUT, "xevi_%s.wav" % name)
        dur = render_tune_wav(snd_no, wav, wave_override=0)
        b = tune_psg_bin(snd_no)
        binp = os.path.join(OUT, "xevi_%s.bin" % name)
        open(binp, "wb").write(b)
        manifest.append((name, kind, snd_no, len(chans), dur, len(b)))
        print("  snd%X %-8s %s ch=%d tempo=%d wsel=%d  wav=%.1fs  psg=%dB"
              % (snd_no, name, kind, len(chans), tempo, wsel, dur, len(b)))
    # 連結 WAV(sndtest 順: fanfare -> bgm -> zapper -> blaster)
    order = ["fanfare", "bgm", "zapper", "blaster"]
    allpcm = bytearray()
    for nm in order:
        p = os.path.join(OUT, "xevi_%s.wav" % nm)
        with wave.open(p, "rb") as w:
            allpcm += w.readframes(w.getnframes())
        allpcm += b"\x00\x00" * int(SR * 0.3)   # 0.3s 無音
    with wave.open(os.path.join(OUT, "xevi_all.wav"), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(bytes(allpcm))
    print("出力: roms/arcade/xevious-out/sound/ (xevi_*.wav 試聴用, xevi_*.bin PSGデータ)")
    print("※54xx爆発は別MCUのため非抽出(sndplay側でPSGノイズ近似)。")

if __name__ == "__main__":
    main()
