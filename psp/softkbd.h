#ifndef PXMIL_PSP_SOFTKBD_H
#define PXMIL_PSP_SOFTKBD_H

#ifdef __cplusplus
extern "C" {
#endif

int softkbd_isvisible(void);
void softkbd_toggle(void);
void softkbd_move(int dx, int dy);
void softkbd_press(void);
void softkbd_release(void);
void softkbd_draw(UINT16 *dst);
void skb_fillrect(UINT16 *dst, int x, int y, int w, int h, UINT16 c);
void skb_drawtext(UINT16 *dst, int x, int y, const char *s, UINT16 c, int scale);
void skb_drawtext_s(UINT16 *dst, int stride, int x, int y, const char *s, UINT16 c, int scale);

#ifdef __cplusplus
}
#endif

#endif
