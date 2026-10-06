/**
 * @file	sysmenu.c
 * @brief	PSP 用システムメニュー (スタブ)
 *
 * 起動確認マイルストーンではメニュー UI を無効化する。
 * menuopen が常に失敗するため menubase/scrnmng_entermenu は呼ばれない。
 */

#include "compiler.h"
#include "sysmenu.h"

BRESULT sysmenu_create(void) {

	return(SUCCESS);
}

void sysmenu_destroy(void) {
}

BRESULT sysmenu_menuopen(UINT menutype, int x, int y) {

	(void)menutype;
	(void)x;
	(void)y;
	return(FAILURE);
}
