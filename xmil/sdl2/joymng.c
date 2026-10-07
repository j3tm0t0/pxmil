#include	"compiler.h"
#include	"joymng.h"

#if defined(XMIL_PROBE_SUPPORT)
#include	<stdlib.h>
#include	<string.h>

/* makescrn.c: 描画フレーム数 */
extern UINT32 pxmil_frame;

/* pxmil: native で実機ジョイスティックに頼らず移動検証するための台本再生。
 *   環境変数 XMIL_JOYSCRIPT="frame:value,frame:value,..." を指定すると、
 *   各 frame 以降その value (PSG port A, 負論理バイト) を返す。
 *   例: "60:0xFE,120:0xFF" = frame60 で bit0(上)押下, frame120 で解放。
 *   未指定なら 0xFF (入力無し)。 */
#define	JS_MAX	64
static int	js_init = 0;
static int	js_n = 0;
static UINT32	js_frame[JS_MAX];
static UINT8	js_val[JS_MAX];

static void js_parse(void) {
	const char *s;
	js_init = 1;
	s = getenv("XMIL_JOYSCRIPT");
	if (s == NULL) {
		return;
	}
	while (*s && js_n < JS_MAX) {
		long f, v;
		char *end;
		f = strtol(s, &end, 0);
		if (end == s || *end != ':') { break; }
		s = end + 1;
		v = strtol(s, &end, 0);
		if (end == s) { break; }
		js_frame[js_n] = (UINT32)f;
		js_val[js_n] = (UINT8)(v & 0xff);
		js_n++;
		s = end;
		if (*s == ',') { s++; }
	}
}

BYTE joymng_getstat(void) {

	int i;
	UINT8 ret = 0xff;

	if (!js_init) {
		js_parse();
	}
	for (i = 0; i < js_n; i++) {
		if (pxmil_frame >= js_frame[i]) {
			ret = js_val[i];
		}
	}
	{
		static UINT8 lastret = 0xff;
		if (js_n && ret != lastret) {
			fprintf(stderr, "JOY frame=%u ret=%02x\n",
							(unsigned)pxmil_frame, ret);
			lastret = ret;
		}
	}
	return(ret);
}

#else

BYTE joymng_getstat(void) {

	return(0xff);
}

#endif
