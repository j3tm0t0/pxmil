#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Xevious 全16エリアの「空中敵 出現スケジュール + 射撃頻度(ffreq)マスク」を
アーケード SUB CPU の area object command stream から抽出。

SUB のエリア列は可変長コマンド列。walker=_sub_fn_2__handle_objects(@SUB0x068F):
  entry = [trigger(byte0), type(byte1), payload...]
  trigger が scroll_cntr 上位と一致したら発火。type を loc_6AA[type-1](@SUB0x06AA)で
  SUB fn index(0-0x17)に remap し、dispatch 表(@SUB0x0701)のハンドラを実行。
  各 fn の entry 長はハンドラの bc 消費量で確定(下表 FNLEN。fn15 domogram のみ可変
  = 5+2*num, num=byte4)。エリア末尾は 1 バイト終端子 0x0D(次エリアは trig 0xFF 開始)。
  (全16エリアを walk し 14/16 が終端子直前に正着、残2も末尾のみ。area7 の可変長
   domogram 6件 len17/19 も含め検証済。)

空中敵の出現(確定):
  - fn2 set_flying_enemies(type0x02): payload=idx → flying_enemy_type_offset_tbl
    (SUB0x04D8)[idx]=(num, type_off) → num 体を flying_enemy_type_tbl(MAIN0x3C03)
    [type_off..type_off+num-1] の敵 type で出す(各スロットが空くたび連続湧き)。
  - fn3 inc_enemy_AI_and_flying(type0x03): payload 無し。難易度(DIP)分 enemy_AI_level
    を増やし、**その rank を index に** 同 offset_tbl を引く(num/types は実行時 rank 依存)。
  - col(X1 列) = (trigger + 0xFD) & 0xFF   … xevi_allareas.py と共通(地上物と同式)。

射撃頻度マスク(ffreq, 敵ごと・エリアごと): fn8..13,16,17,22 が各 ffreq_mask_* を設定。
  射撃間隔 = ((rnd & mask)+1)*8 フレーム。mask 大=低頻度。

出力(非コミット, roms/arcade/xevious-out/enemies/):
  area_enemies.txt            : 全エリアの読める出現/ffreq レポート
  fly_type_tbl.bin (0x80B)    : flying_enemy_type_tbl(MAIN0x3C03) そのまま(敵type列)
  fly_offset_tbl.bin          : flying_enemy_type_offset_tbl(SUB0x04D8) (num,off)×N
  areaNN_fly.bin              : エリア毎の出現/ffreq イベント(下記形式)
    u8  fly_count
    repeat: u16 col, u8 kind(2=fn2/3=fn3), u8 num, u8 off, u8[num] enemy_type
            (fn3 は num=0,off=0; 実行時 rank で offset_tbl から解決)
    u8  ffreq_count
    repeat: u16 col, u8 mask_id, u8 mask   (mask_id=SUB fn index 8..22)
クレジット: tcdev42/re (tcdev/jotd)。ROM/出力は非コミット。
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROMDIR = os.path.join(ROOT, "roms", "arcade", "xevious")
OUT = os.path.join(ROOT, "roms", "arcade", "xevious-out", "enemies")


def rd(n):
    return open(os.path.join(ROMDIR, n), "rb").read()


def load():
    sub = rd("xvi_5.3f") + rd("xvi_6.3j")
    main = (rd("xvi_1.3p") + rd("xvi_2.3m") + rd("xvi_3.2m") + rd("xvi_4.2l"))
    return sub, main


FET = 0x3C03   # MAIN flying_enemy_type_tbl
OFT = 0x04D8   # SUB flying_enemy_type_offset_tbl
REMAP = 0x06AA # SUB loc_6AA (type-1 -> fn)

# fn -> entry total length (trigger+type+payload). fn15 variable(=5+2*num).
FNLEN = {0:3,1:4,2:3,3:2,4:2,5:2,6:3,7:2,8:3,9:3,10:3,11:3,12:3,13:3,
         14:5,15:None,16:3,17:3,18:2,19:2,20:2,21:2,22:3,23:2}
FN_NAME = {0:"type_only",1:"ground",2:"set_flying",3:"inc_AI_flying",5:"reset_flying",
  6:"bacura_inc",7:"reset_bacura",8:"ffreq_derota",9:"ffreq_logram",10:"gnd_stop_fire",
  11:"ffreq_zoshi",12:"ffreq_terrazi",13:"ffreq_kapi",15:"domogram",16:"ffreq_boza",
  17:"ffreq_domogram",18:"sheonite_st",19:"sheonite_end",20:"andor_st",21:"andor_end",
  22:"ffreq_andor",23:"adjust_AI"}
FFREQ_FNS = {8,9,10,11,12,13,16,17,22}
ETN = {0x08:"Giddo",0x0A:"Toroid",0x0B:"Toroid_shoot",0x0C:"Zoshi_rnd",0x0D:"Zoshi_top",
  0x0E:"Zoshi_bot",0x0F:"Torkan",0x10:"Kapi",0x11:"Terrazi",0x12:"Zakato_slow",
  0x13:"Zakato_closeY",0x14:"Zakato_fast",0x15:"Zakato",0x16:"BragZakato_rnd",
  0x17:"BragZakato_closeY",0x18:"GaruZakato",0x55:"Jara_shoot",0x56:"Jara",0x00:"-"}


def main_dump():
    sub, main = load()
    ptrs = [sub[0x1000 + i*2] | (sub[0x1000 + i*2 + 1] << 8) for i in range(16)]

    def fn_of(typ):
        return sub[REMAP + (typ - 1)] if 1 <= typ <= 0x80 else -1

    def entry_len(p):
        fn = fn_of(sub[p+1])
        if fn == 15:
            return fn, 5 + 2*sub[p+4]
        return fn, FNLEN.get(fn)

    def walk(start, end):
        out = []; p = start
        while p < end - 1:
            fn, L = entry_len(p)
            if L is None or L < 2 or p + L > end:   # terminator / invalid -> stop
                break
            out.append((p, sub[p], sub[p+1], fn, L)); p += L
        return out

    def etypes(off, num):
        return [ETN.get(main[FET+off+k], "?%02X" % main[FET+off+k]) for k in range(num)]

    os.makedirs(OUT, exist_ok=True)
    open(os.path.join(OUT, "fly_type_tbl.bin"), "wb").write(main[FET:FET+0x80])
    open(os.path.join(OUT, "fly_offset_tbl.bin"), "wb").write(sub[OFT:OFT+0x40])

    rep = []
    rep.append("=== flying_enemy_type_offset_tbl (SUB0x04D8) idx -> (num, off) [types] ===")
    rep.append("  fn3 は rank(enemy_AI_level) を index に、fn2 は payload の idx をこの表に。")
    for idx in range(0x20):
        num = sub[OFT+idx*2]; off = sub[OFT+idx*2+1]
        rep.append("  idx%2d: num=%d off=0x%02X  %s" % (idx, num, off,
                   ", ".join(etypes(off, num)) if num else "-"))
    for a in range(16):
        start = ptrs[a]; end = ptrs[a+1] if a+1 < 16 else 0x1E52
        ents = walk(start, end)
        flys = []; ffqs = []
        for (p, trig, typ, fn, L) in ents:
            col = (trig + 0xFD) & 0xFF
            if fn == 2:
                idx = sub[p+2]; num = sub[OFT+idx*2]; off = sub[OFT+idx*2+1]
                flys.append((col, 2, num, off, [main[FET+off+k] for k in range(num)]))
            elif fn == 3:
                flys.append((col, 3, 0, 0, []))   # rank += d, then offset_tbl[rank]
            elif fn == 5:
                flys.append((col, 5, 0, 0, []))   # reset: num_flying=0 (stop spawning)
            elif fn in FFREQ_FNS:
                ffqs.append((col, fn, sub[p+2]))
        # write areaNN_fly.bin
        buf = bytearray([len(flys)])
        for (col, kind, num, off, tys) in flys:
            buf += struct.pack("<H", col) + bytes([kind, num, off]) + bytes(tys)
        buf += bytes([len(ffqs)])
        for (col, fn, mask) in ffqs:
            buf += struct.pack("<H", col) + bytes([fn, mask])
        open(os.path.join(OUT, "area%02d_fly.bin" % (a+1)), "wb").write(buf)
        # report
        rep.append("=== AREA %d (0x%04X, %d entries) ===" % (a+1, start, len(ents)))
        for (col, kind, num, off, tys) in flys:
            if kind == 2:
                rep.append("  col=%3d(trig0x%02X) POP=set x%d off0x%02X: %s" %
                           (col, (col-0xFD) & 0xFF, num, off,
                            ", ".join(etypes(off, num))))
            elif kind == 3:
                rep.append("  col=%3d(trig0x%02X) POP=rank+=d then offset_tbl[rank] (実行時)" %
                           (col, (col-0xFD) & 0xFF))
            else:
                rep.append("  col=%3d(trig0x%02X) POP=stop (num_flying=0)" %
                           (col, (col-0xFD) & 0xFF))
        for (col, fn, mask) in ffqs:
            rep.append("  col=%3d ffreq %-14s = 0x%02X" % (col, FN_NAME[fn], mask))
    txt = "\n".join(rep)
    open(os.path.join(OUT, "area_enemies.txt"), "w").write(txt + "\n")
    print(txt)
    print("\n出力: roms/arcade/xevious-out/enemies/ (area_enemies.txt, areaNN_fly.bin, "
          "fly_type_tbl.bin, fly_offset_tbl.bin)")


if __name__ == "__main__":
    main_dump()
