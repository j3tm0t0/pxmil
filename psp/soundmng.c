/**
 * @file	soundmng.c
 * @brief	PSP 用サウンドマネージャ (無音スタブ)
 *
 * 起動確認マイルストーンではサウンド出力を行わない。
 * API は sdl2/soundmng.h と同一。将来 pspaudio / SDL_audio で実装する。
 */

#include "compiler.h"
#include "soundmng.h"

UINT soundmng_create(UINT rate, UINT ms) {

	(void)rate;
	(void)ms;
	return(0);
}

void soundmng_destroy(void) {
}

void soundmng_play(void) {
}

void soundmng_stop(void) {
}


// ----

void soundmng_initialize(void) {
}

void soundmng_deinitialize(void) {
}
