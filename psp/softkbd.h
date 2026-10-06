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

#ifdef __cplusplus
}
#endif

#endif
