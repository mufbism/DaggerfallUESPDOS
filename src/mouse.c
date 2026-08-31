/* mouse.c -- see mouse.h. All calls go through INT 33h via DJGPP's
 * __dpmi_int, which reflects the call to real mode -- the same
 * technique already used elsewhere in this codebase for the INT 10h
 * blink-mode call (see pmwin.c's set_blink_mode()). */
#include <dpmi.h>
#include "mouse.h"

int pm_mouse_reset_detect(void) {
    __dpmi_regs r;
    r.x.ax = 0x0000;
    __dpmi_int(0x33, &r);
    return (r.x.ax == 0xFFFF);
}

void pm_mouse_show(void) {
    __dpmi_regs r;
    r.x.ax = 0x0001;
    __dpmi_int(0x33, &r);
}

void pm_mouse_hide(void) {
    __dpmi_regs r;
    r.x.ax = 0x0002;
    __dpmi_int(0x33, &r);
}

void pm_mouse_get(int *col, int *row, int *buttons) {
    __dpmi_regs r;
    r.x.ax = 0x0003;
    __dpmi_int(0x33, &r);
    *col = r.x.cx / 8;
    *row = r.x.dx / 8;
    *buttons = r.x.bx;
}
