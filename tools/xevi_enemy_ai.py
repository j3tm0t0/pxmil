#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Xevious 空中敵の出現・軌道・弾・難易度(rank)に関わる ROM 表を抽出。

アーケード主 CPU / サブ CPU の逆アセンブル(tcdev42/re の注釈付き listing で
関数・表の位置のみ参照、ソース非取り込み)で特定した表を、我々の ROM から
直接読んで出力・検証する。逆アセンブルで確認した「確定」事項のみを扱う。

ROM 連結(非コミット, roms/arcade/xevious/):
  MAIN = xvi_1.3p + xvi_2.3m + xvi_3.2m + xvi_4.2l  (0x0000-0x3FFF)
  SUB  = xvi_5.3f + xvi_6.3j                         (0x0000-0x1FFF)

主要アドレス(確定):
  MAIN 0x3C83 obj_handler_tbl       : TYPE -> ハンドラ addr (dw)
  MAIN 0x3C03 flying_enemy_type_tbl : offset -> 敵TYPE 列(空中敵の中身)
  MAIN 0x3DF3 angle_dX_dY_tbl            (aimed,   r=0x20=1.0px/f) 32x(dY,dX)
  MAIN 0x3DB3 angle_dX_dY_terrazi_torkan (        r=0x30=1.5px/f)
  MAIN 0x3E33 angle_dX_dY_toroid_tbl     (toroid/zoshi r=0x18=0.75px/f)
  MAIN 0x3D73 angle_dX_dY_sheonite_tbl   (        r=0x40=2.0px/f)
  MAIN 0x0C34 enemy_AI_dec_value    : 死亡時 rank 減算量 {0x10,0x18,8,0}[DIP残機]
  MAIN 0x209F toroid_sprite_tbl     : {F,E,D,C,B,A,9,8}
  MAIN 0x235C jara_right_sprite_tbl : {A0..A5}  / 0x2362 left {A5..A0}
  SUB  0x04D8 flying_enemy_type_offset_tbl : rank/AI index -> (num, type_off)
  SUB  0x0498 flying_enemy_offset_tbl      : 直接指定 (num, type_off)
  SUB  0x0408 difficulty_tbl        : rank 増分 {2,...}(DIP難易度, doc=0/2/6/16)
"""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROMDIR = os.path.join(ROOT, "roms", "arcade", "xevious")


def rd(n):
    return open(os.path.join(ROMDIR, n), "rb").read()


def load():
    main = rd("xvi_1.3p") + rd("xvi_2.3m") + rd("xvi_3.2m") + rd("xvi_4.2l")
    sub = rd("xvi_5.3f") + rd("xvi_6.3j")
    return main, sub


def s8(b):
    return b - 256 if b >= 128 else b


# TYPE -> 名前(obj_handler_tbl と flying_enemy_type_tbl の読み用)
TYPE_NAME = {
    0x01: "Bacura", 0x06: "Bullet(aimed)", 0x07: "GaruZakatoBullet",
    0x08: "GiddoSpario", 0x09: "BragSpario(homing)", 0x0A: "Toroid",
    0x0B: "Toroid_shoots", 0x0C: "Zoshi_rnd", 0x0D: "Zoshi_top",
    0x0E: "Zoshi_bottom", 0x0F: "Torkan", 0x10: "Kapi", 0x11: "Terrazi",
    0x12: "Zakato_slow", 0x13: "Zakato_closeY", 0x14: "Zakato_fast",
    0x15: "Zakato", 0x16: "BragZakato_rnd", 0x17: "BragZakato_closeY",
    0x18: "GaruZakato",
    0x19: "Logram?", 0x1A: "Derota?", 0x1B: "Derota", 0x1C: "Derota?",
    0x1D: "Sol", 0x1E: "Barra", 0x1F: "Zolbak", 0x20: "GaruBarra",
    0x21: "GaruDerota", 0x26: "Logram", 0x2C: "Grobda", 0x2D: "BozaLogram",
    0x2E: "Domogram", 0x31: "Sheonite_R", 0x32: "Sheonite_L",
    0x55: "Jara_shoots", 0x56: "Jara",
    0x78: "AndorGenesis..", 0x79: "AG", 0x7A: "AG", 0x7B: "AG",
    0x7C: "AG", 0x7D: "AG", 0x7E: "AG", 0x7F: "AG", 0x00: "-none-",
}


def dump_angle_tbl(main, addr, name, radius):
    print("\n[%s @0x%04X] r=0x%02X (%.3gpx/f)  32x(dY,dX) 符号付" %
          (name, addr, radius, radius / 32.0))
    maxr = 0
    for i in range(32):
        dy, dx = s8(main[addr + i * 2]), s8(main[addr + i * 2 + 1])
        r = (dx * dx + dy * dy) ** 0.5
        maxr = max(maxr, r)
        if i < 9 or i == 8:
            print("  [%2d] dY=%+4d dX=%+4d |r|=%.1f" % (i, dy, dx, r))
    print("  ... max|r|=%.1f (=%.3gpx)" % (maxr, maxr / 32.0))


def main_dump():
    main, sub = load()

    # obj_handler_tbl 先頭(0x3C83)= type 0x01 のエントリ。handler = word[0x3C83+(type-1)*2]
    print("=== obj_handler_tbl @0x3C83 (TYPE->handler; 先頭=type0x01) ===")
    for i in range(0, 0x20):
        t = i + 1
        lo, hi = main[0x3C83 + i * 2], main[0x3C83 + i * 2 + 1]
        a = lo | (hi << 8)
        if a:
            print("  type 0x%02X -> 0x%04X  %s" % (t, a, TYPE_NAME.get(t, "?")))

    print("\n=== flying_enemy_type_tbl @0x3C03 (offset->敵TYPE列) ===")
    row = []
    for i in range(0x80):
        v = main[0x3C03 + i]
        row.append("%02X" % v)
        if len(row) == 16:
            print("  +0x%02X: %s" % (i - 15, " ".join(row)))
            row = []
    print("  (値=敵TYPE。例: offset1.. = 0A 0A..=Toroid 群)")

    # 角度/速度表
    dump_angle_tbl(main, 0x3DF3, "angle_dX_dY_tbl(aimed)", 0x20)
    dump_angle_tbl(main, 0x3DB3, "angle_dX_dY_terrazi_torkan", 0x30)
    dump_angle_tbl(main, 0x3E33, "angle_dX_dY_toroid(toroid/zoshi)", 0x18)
    dump_angle_tbl(main, 0x3D73, "angle_dX_dY_sheonite", 0x40)

    print("\n=== rank(enemy_AI_level) 関連 ===")
    dec = [main[0x0C34 + i] for i in range(4)]
    print("  enemy_AI_dec_value @0x0C34 (死亡時 -rank, DIP残機[5,2,1,3]順):",
          ["0x%02X" % x for x in dec])
    diff = [sub[0x0408 + i] for i in range(4)]
    print("  difficulty_tbl @SUB0x0408 (進行時 +rank, DIP難易度順):",
          ["0x%02X" % x for x in diff], "(doc=0/2/6/16)")

    print("\n=== flying_enemy_type_offset_tbl @SUB0x04D8 (rank/AI index -> num,off) ===")
    for i in range(16):
        num, off = sub[0x04D8 + i * 2], sub[0x04D8 + i * 2 + 1]
        print("  idx%2d: num=%d off=0x%02X" % (i, num, off))
    print("  (... 先頭16件。rank(=enemy_AI_level)を index に num体を offset から spawn)")

    print("\n=== 軌道アニメ sprite 表 ===")
    print("  toroid_sprite_tbl @0x209F:",
          ["0x%02X" % main[0x209F + i] for i in range(8)])
    print("  jara_right @0x235C:", ["0x%02X" % main[0x235C + i] for i in range(6)])
    print("  jara_left  @0x2362:", ["0x%02X" % main[0x2362 + i] for i in range(6)])


if __name__ == "__main__":
    main_dump()
