/**
 * @file	xmil.h
 * @brief	PSP frontend definitions
 */

#pragma once

typedef struct {
	BYTE	NOWAIT;
	BYTE	DRAW_SKIP;
} XMILOSCFG;

enum {
	FULLSCREEN_WIDTH	= 640,
	FULLSCREEN_HEIGHT	= 400
};

/* PSP physical screen size */
enum {
	PSP_SCREEN_WIDTH	= 480,
	PSP_SCREEN_HEIGHT	= 272
};

extern	XMILOSCFG	xmiloscfg;
