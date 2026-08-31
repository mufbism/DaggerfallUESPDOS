/* mouse -- thin wrapper over the INT 33h mouse driver API (DOS's de
 * facto standard, works with any driver: CTMOUSE, real Microsoft
 * MOUSE.COM, DOSBox's built-in one, etc). Every function here degrades
 * safely if no driver is loaded: pm_mouse_reset_detect() is the only
 * one that must be called first, and its return value is the single
 * source of truth for whether a mouse exists. Callers (pmwin.c) never
 * call the other functions unless that returned true. */
#ifndef PM_MOUSE_H
#define PM_MOUSE_H

/* Resets the driver and asks whether one is present. Returns 1/0.
 * Safe to call even with no driver loaded (INT 33h with no handler
 * present is a silent no-op on real DOS; DJGPP's __dpmi_int always
 * returns cleanly either way). */
int pm_mouse_reset_detect(void);

void pm_mouse_show(void);
void pm_mouse_hide(void);

/* Character-cell column/row and a button bitmask (bit0=left,
 * bit1=right, bit2=middle) as of right now. */
void pm_mouse_get(int *col, int *row, int *buttons);

#endif
