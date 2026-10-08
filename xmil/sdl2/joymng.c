#include	"compiler.h"
#include	"joymng.h"

#if defined(XMIL_PROBE_SUPPORT)
#include	<stdlib.h>
#include	<string.h>
#include	"keystat.h"
#include	"vram.h"	/* tram[], gram[], GRAM_SIZE */
#include	"iocore.h"	/* pcg.d */

/* makescrn.c: drawn-frame counter */
extern UINT32 pxmil_frame;

/* pxmil: scripted joystick for native testing without a real pad.
 *   Env XMIL_JOYSCRIPT="frame:value,frame:value,..." makes joymng return
 *   that value (PSG port A, negative logic) from the given frame onward.
 *   e.g. "60:0xFE,120:0xFF" = press bit0(up) at frame 60, release at 120.
 *   Unset -> 0xFF (no input). */
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

	/* pxmil: XMIL_AUTOFIRE=<hexbase> -> auto-tap trigger1 (bit5) every other
	 * frame (press 1f / release 1f) on top of <hexbase>. Lets perf tests drive
	 * sustained rapid fire without the 64-entry JOYSCRIPT limit.
	 * e.g. XMIL_AUTOFIRE=FF (zapper tap only), =BF (zapper tap + blaster held). */
	{
		static int af_init = 0;
		static int af_on = 0;
		static unsigned af_base = 0xff;
		if (!af_init) {
			const char *s = getenv("XMIL_AUTOFIRE");
			af_init = 1;
			if (s) { af_on = 1; af_base = (unsigned)strtoul(s, NULL, 16); }
		}
		if (af_on) {
			UINT8 v = (UINT8)af_base;
			if (pxmil_frame & 1) v |= 0x20;		/* release (bit5=1) */
			else v = (UINT8)(v & ~0x20);		/* press   (bit5=0) */
			return v;
		}
	}

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

/* pxmil: scripted keyboard for native testing without a real keyboard.
 *   Env XMIL_KEYSCRIPT="frame:code,frame:code,..." taps the X1 key (scancode
 *   from sdlkbd.c s_table) at the given pxmil_frame: keydown at frame, keyup
 *   KS_TAP frames later.  e.g. "600:0x34" taps SPACE at frame 600
 *   (RETURN=0x1c, SPACE=0x34, Z=0x29, X=0x2a, 1=0x01).  Called once per frame
 *   from makescrn.c. */
#define	KS_MAX	32
#define	KS_TAP	6
static int	ks_init = 0;
static int	ks_n = 0;
static UINT32	ks_frame[KS_MAX];
static UINT8	ks_code[KS_MAX];

static void ks_parse(void) {
	const char *s;
	ks_init = 1;
	s = getenv("XMIL_KEYSCRIPT");
	if (s == NULL) {
		return;
	}
	while (*s && ks_n < KS_MAX) {
		long f, c;
		char *end;
		f = strtol(s, &end, 0);
		if (end == s || *end != ':') { break; }
		s = end + 1;
		c = strtol(s, &end, 0);
		if (end == s) { break; }
		ks_frame[ks_n] = (UINT32)f;
		ks_code[ks_n] = (UINT8)(c & 0xff);
		ks_n++;
		s = end;
		if (*s == ',') { s++; }
	}
}

void pxmil_keyscript(void) {
	int i;
	if (!ks_init) {
		ks_parse();
	}
	for (i = 0; i < ks_n; i++) {
		if (pxmil_frame == ks_frame[i]) {
			keystat_senddata(ks_code[i]);
			fprintf(stderr, "KEY frame=%u down=%02x\n",
							(unsigned)pxmil_frame, ks_code[i]);
		}
		else if (pxmil_frame == ks_frame[i] + KS_TAP) {
			keystat_senddata((UINT8)(ks_code[i] | 0x80));
		}
	}
}

/* pxmil: state dump for investigating how a guest draws (PCG/text/GRAM).
 *   Env XMIL_STATEDUMP=<prefix> enables. Each frame prints a summary to stderr
 *   (text cells, PCG-attr cells, non-zero GRAM bytes, PCG-RAM FNV hash and
 *   whether it changed since last frame = per-frame PCG rewrite detection).
 *   Every XMIL_STATEDUMP_EVERY frames (default 30) writes <prefix>_<frame>.bin
 *   = tram[0x800] as (ank,atr) pairs (4096B) + pcg.d (0x1800B). */
void pxmil_statedump(void) {
	static int	sd_init = 0;
	static const char *sd_pfx = NULL;
	static unsigned long sd_every = 30;
	static unsigned long sd_from = 0;
	static unsigned long sd_to = 0xffffffffUL;
	static UINT32	sd_prev = 0;
	UINT32	h;
	int	i, tcells, pcgcells;
	long	gramnz;

	if (!sd_init) {
		const char *e;
		sd_init = 1;
		sd_pfx = getenv("XMIL_STATEDUMP");
		e = getenv("XMIL_STATEDUMP_EVERY");
		if (e) { sd_every = strtoul(e, NULL, 0); if (!sd_every) sd_every = 1; }
		e = getenv("XMIL_STATEDUMP_FROM");
		if (e) { sd_from = strtoul(e, NULL, 0); }
		e = getenv("XMIL_STATEDUMP_TO");
		if (e) { sd_to = strtoul(e, NULL, 0); }
	}
	if (sd_pfx == NULL) {
		return;
	}
	h = 2166136261u;
	for (i = 0; i < 0x1800; i++) {
		h = (h ^ pcg.d[i]) * 16777619u;
	}
	tcells = 0; pcgcells = 0;
	{
		/* pxmil: per-category PCG cell counts by ank code range.
		 *   ship 0x10-0x9F / bullet 0xA0-0xAF / enemy 0xB0-0xBF / expl 0xC0-0xCB.
		 *   Counts BOTH windows (tram[0x800] holds POS and POS+1024). */
		int shipc = 0, bulc = 0, enec = 0, expc = 0;
		for (i = 0; i < 0x800; i++) {
			if (tram[i].ank != 0x20 && tram[i].ank != 0x00) {
				tcells++;
				if (tram[i].atr & 0x20) {
					UINT8 ak = tram[i].ank;
					pcgcells++;
					if (ak >= 0x10 && ak < 0xa0) { shipc++; }
					else if (ak >= 0xa0 && ak < 0xb0) { bulc++; }
					else if (ak >= 0xb0 && ak < 0xc0) { enec++; }
					else if (ak >= 0xc0 && ak < 0xcc) { expc++; }
				}
			}
		}
		fprintf(stderr, "SDPCG frame=%u ship=%d bullet=%d enemy=%d expl=%d\n",
				(unsigned)pxmil_frame, shipc, bulc, enec, expc);
	}
	gramnz = 0;
	for (i = 0; i < GRAM_SIZE; i++) {
		if (gram[i]) { gramnz++; }
	}
	/* crtc: SCRN_BITS (bit3=SCRN_DISPVRAM graphics-hide), dispmode,
	 *   start address pos / POSH:POSL (R12:R13) for scroll tracking. */
	fprintf(stderr, "SD frame=%u tcells=%d pcgcells=%d gramnz=%ld "
					"pcghash=%08x pcgchg=%d scrn=%02x ply=%02x "
					"dispmode=%02x pos=%u poshl=%02x%02x\n",
			(unsigned)pxmil_frame, tcells, pcgcells, gramnz,
			h, (h != sd_prev),
			crtc.s.SCRN_BITS, crtc.s.rgbp[CRTC_PLY],
			crtc.e.dispmode, (unsigned)crtc.e.pos,
			crtc.s.reg[CRTCREG_POSH], crtc.s.reg[CRTCREG_POSL]);
	sd_prev = h;
	if ((pxmil_frame % sd_every) == 0
		&& pxmil_frame >= sd_from && pxmil_frame <= sd_to) {
		char path[256];
		FILE *fp;
		snprintf(path, sizeof(path), "%s_%06u.bin", sd_pfx,
					(unsigned)pxmil_frame);
		fp = fopen(path, "wb");
		if (fp) {
			for (i = 0; i < 0x800; i++) {
				fputc(tram[i].ank, fp);
				fputc(tram[i].atr, fp);
			}
			fwrite(pcg.d, 1, 0x1800, fp);
			fwrite(gram, 1, GRAM_SIZE, fp);	/* GRAM for layer-role check */
			fclose(fp);
		}
	}
}

#else

BYTE joymng_getstat(void) {

	return(0xff);
}

void pxmil_keyscript(void) {
}

void pxmil_statedump(void) {
}

#endif
