/**
 * @file	compiler.h
 * @brief	PSP (MIPS Allegrex, little endian, 32bit, newlib) 用 compiler.h
 *
 *			libretro/compiler.h をベースに PSP 向けへ調整したもの。
 *			全コアソースが最初に include する前提の共通ヘッダ。
 */

#pragma once

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* PSP (MIPS Allegrex) はリトルエンディアン固定 */
#define	BYTESEX_LITTLE

/* 文字コードは UTF-8 / 英語リソース (PSP 側で変換レイヤを持たない簡素構成) */
#define	OSLANG_UTF8
#define	OSLINEBREAK_CRLF
#define	RESOURCE_US

typedef	signed int			SINT;
typedef	unsigned int		UINT;
typedef	signed char			SINT8;
typedef	unsigned char		UINT8;
typedef	signed short		SINT16;
typedef	unsigned short		UINT16;
typedef	signed int			SINT32;
typedef	unsigned int		UINT32;
#define	INTPTR				intptr_t

#define	BRESULT				UINT
#define	OEMCHAR				char
#define	OEMTEXT(string)		string
#define	OEMSPRINTF			sprintf
#define	OEMSTRLEN			strlen

/*
 * 画面サイズ: SIZE_VGA = X1 ネイティブ解像度 (640x400) で描画する。
 * RGB16 は UINT16 (RGB565)。PSP 実機 (480x272) への縮小はフロントエンド側
 * (GE スケーリング想定、px68k と同方式) が担当する。
 * ※ libretro の PSP ターゲットは -DSIZE_QVGA (RGB16=UINT32) だったが、
 *   vram コード全体の ABI が変わるため採用しない。フロントエンドと要整合。
 */
#define	SIZE_VGA
#define	RGB16		UINT16

typedef	signed char		CHAR;
typedef	unsigned char	BYTE;

typedef signed char BOOL;

#ifndef	TRUE
#define	TRUE	1
#endif

#ifndef	FALSE
#define	FALSE	0
#endif

#ifndef	MAX_PATH
#define	MAX_PATH	256
#endif

#ifndef	max
#define	max(a,b)	(((a) > (b)) ? (a) : (b))
#endif
#ifndef	min
#define	min(a,b)	(((a) < (b)) ? (a) : (b))
#endif

#ifndef	ZeroMemory
#define	ZeroMemory(d,n)		memset((d), 0, (n))
#endif
#ifndef	CopyMemory
#define	CopyMemory(d,s,n)	memcpy((d), (s), (n))
#endif
#ifndef	FillMemory
#define	FillMemory(a, b, c)	memset((a), (c), (b))
#endif

#include "common.h"
#include "milstr.h"
#include "_memory.h"
#include "rect.h"
#include "lstarray.h"
#include "trace.h"

/* フロントエンド (psp/) が提供する tick 関数。リンク時に解決される */
long GetTicks(void);
#define	GETTICK()			GetTicks()
#define	__ASSERT(s)
#define	SPRINTF				sprintf
#define	STRLEN				strlen

#define	SUPPORT_UTF8

/* 描画は 16bpp のみ (PSP の GE テクスチャに RGB565 で渡す想定) */
#define	SUPPORT_16BPP
#define	SCREEN_BPP		16

#define	MEMOPTIMIZE		2

/* サウンド構成 (不要なら以下の define を外す):
 *   SUPPORT_OPM    - FM 音源 (OPM) 対応
 *   SUPPORT_TURBOZ - X1turboZ 対応 (拡張パレット・FM 音量等)
 */
#define	SUPPORT_OPM
#define	SUPPORT_TURBOZ

//#define SOUND_CRITICAL
#define	SOUNDRESERVE	100
