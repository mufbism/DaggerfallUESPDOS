/* pmwin.c -- implementation. See pmwin.h for the API contract. */
#include <pc.h>
#include <keys.h>
#include <dpmi.h>
#include <string.h>
#include <stdlib.h>
#include "pmwin.h"
#include "mouse.h"

#define PM_ROWS 25
#define PM_COLS 80

/* ---------------------------------------------------------- video lifecycle */

static unsigned char g_startup_screen[PM_ROWS * PM_COLS * 2];

static void set_blink_mode(int enable_blink) {
    __dpmi_regs r;
    r.x.ax = 0x1003;
    r.h.bl = enable_blink ? 0x01 : 0x00;
    __dpmi_int(0x10, &r);
}

void pm_video_init(void) {
    ScreenRetrieve(g_startup_screen);
    set_blink_mode(0);
}

void pm_video_shutdown(void) {
    set_blink_mode(1);
    ScreenUpdate(g_startup_screen);
    ScreenSetCursor(PM_ROWS - 1, 0);
}

/* --------------------------------------------------------- screen drawing */

void pm_screen_clear_desktop(void) {
    /* CP437 177 (medium shade, "▒") instead of a plain space -- user
     * request, after checking the real PMAIL.EXE directly (still
     * available locally from this project's original color-matching
     * phase) and finding no textured background anywhere in it; this
     * is a deliberate departure from that precedent, not a match to
     * it. Same PM_COL_DESKTOP attribute as before (light gray on
     * blue), so it reads as a subtle texture on the existing blue,
     * not a different color. */
    int r, c;
    for (r = 0; r < PM_ROWS; r++)
        for (c = 0; c < PM_COLS; c++)
            ScreenPutChar(177, PM_COL_DESKTOP, c, r);
}

void pm_hline(int x, int y, int w, int ch, unsigned char attr) {
    int i;
    for (i = 0; i < w; i++) {
        if (x + i < 0 || x + i >= PM_COLS || y < 0 || y >= PM_ROWS) continue;
        ScreenPutChar(ch, attr, x + i, y);
    }
}

static void put_clip(int x, int y, int ch, unsigned char attr) {
    if (x < 0 || x >= PM_COLS || y < 0 || y >= PM_ROWS) return;
    ScreenPutChar(ch, attr, x, y);
}

static void puts_clip(int x, int y, const char *s, unsigned char attr) {
    int i;
    for (i = 0; s[i]; i++) put_clip(x + i, y, (unsigned char)s[i], attr);
}

void pm_puts(int x, int y, const char *s, unsigned char attr) {
    puts_clip(x, y, s, attr);
}

/* Like puts_clip(), but interprets PM_BOLD_ON/PM_BOLD_OFF (see pmwin.h)
 * as zero-width toggles between `attr` and a brighter variant of it
 * (same background, PM_WHITE foreground -- this theme's existing
 * "emphasis" color, already used for borders/titles) instead of
 * printing them or advancing the column. Only pm_textpager() uses this
 * -- kept static rather than promoted to the public API, matching this
 * file's existing pattern of only exposing a helper once something
 * outside pmwin.c actually needs it directly (see pm_puts()/
 * pm_shadow()'s own promotion story). If a line happens to contain an
 * ON with no matching OFF (e.g. truncated by a line-length limit
 * upstream), the bold state doesn't leak into the next line -- `cur`
 * always starts fresh from `attr` at the top of each call, since every
 * pager row is drawn with its own separate call.
 *
 * `maxcol` stops drawing once `col` reaches it, regardless of how much
 * of `s` is left -- real bug this fixes: a chapter line is supposed to
 * already be wrapped to fit, but a source line containing an
 * un-normalized multi-byte character (a scholarly transliteration mark
 * like the "h with a dot under it" used for Hebrew names, counted as
 * one character by the wrapping step but several raw bytes on disk)
 * can still come in a couple of columns too wide. Before this bound
 * existed, an overlong line would write straight past the pager's own
 * right border -- and since the border itself (unlike the content) is
 * only ever drawn once, when the window opens, that corruption never
 * got fixed by scrolling away and back, only accumulated the more of a
 * long chapter you scrolled through. Found via a real report from
 * real hardware -- confirmed the actual bytes being written were
 * always correct (verified with a byte-level dump, not guessed) before
 * finally checking where the *border* itself, not just this new
 * function's own output, could get overwritten. */
static void puts_clip_rich(int x, int y, const char *s, unsigned char attr, int maxcol) {
    unsigned char bold_attr = PM_ATTR(PM_WHITE, (attr >> 4) & 0x07);
    unsigned char cur = attr;
    int i, col = 0;
    for (i = 0; s[i] && col < maxcol; i++) {
        if (s[i] == PM_BOLD_ON) { cur = bold_attr; continue; }
        if (s[i] == PM_BOLD_OFF) { cur = attr; continue; }
        put_clip(x + col, y, (unsigned char)s[i], cur);
        col++;
    }
}

void pm_box(int x, int y, int w, int h, unsigned char border_attr,
            unsigned char fill_attr, const char *title, unsigned char title_attr) {
    int r, c;
    /* fill */
    for (r = y; r < y + h; r++)
        for (c = x; c < x + w; c++)
            put_clip(c, r, ' ', fill_attr);
    /* corners + edges (single-line box drawing, CP437) */
    put_clip(x, y, 218, border_attr);             /* top-left */
    put_clip(x + w - 1, y, 191, border_attr);      /* top-right */
    put_clip(x, y + h - 1, 192, border_attr);      /* bottom-left */
    put_clip(x + w - 1, y + h - 1, 217, border_attr); /* bottom-right */
    for (c = x + 1; c < x + w - 1; c++) {
        put_clip(c, y, 196, border_attr);
        put_clip(c, y + h - 1, 196, border_attr);
    }
    for (r = y + 1; r < y + h - 1; r++) {
        put_clip(x, r, 179, border_attr);
        put_clip(x + w - 1, r, 179, border_attr);
    }
    if (title && title[0]) {
        int tlen = (int)strlen(title);
        int tx = x + (w - (tlen + 2)) / 2;
        if (tx < x + 1) tx = x + 1;
        put_clip(tx, y, ' ', title_attr);
        puts_clip(tx + 1, y, title, title_attr);
        put_clip(tx + 1 + tlen, y, ' ', title_attr);
    }
}

void pm_shadow(int x, int y, int w, int h) {
    int r, c;
    for (r = y + 1; r <= y + h; r++) {
        put_clip(x + w, r, ' ', PM_COL_SHADOW);
        put_clip(x + w + 1, r, ' ', PM_COL_SHADOW);
    }
    for (c = x + 2; c < x + w + 2; c++)
        put_clip(c, y + h, ' ', PM_COL_SHADOW);
}

/* --------------------------------------------------------------- windows */

static unsigned char win_stack[PM_MAX_WINDOWS][PM_ROWS * PM_COLS * 2];
static struct { int x, y, w, h; } win_rect[PM_MAX_WINDOWS];
static int win_top = 0; /* number of windows currently open */

static int win_open_colors(int x, int y, int w, int h, const char *title,
                            unsigned char border_attr, unsigned char fill_attr,
                            unsigned char title_attr, int want_shadow) {
    int handle;
    if (win_top >= PM_MAX_WINDOWS) return -1;
    handle = win_top++;
    ScreenRetrieve(win_stack[handle]);
    win_rect[handle].x = x; win_rect[handle].y = y;
    win_rect[handle].w = w; win_rect[handle].h = h;
    if (want_shadow) pm_shadow(x, y, w, h);
    pm_box(x, y, w, h, border_attr, fill_attr, title, title_attr);
    return handle;
}

/* "Blue surface" window -- see the theme comment in pmwin.h. Used for
 * simple message boxes. Pull-down menus and list boxes are "gray
 * surface" floating pickers instead; they call win_open_colors()
 * directly with PM_COL_GRAY_* below. */
int pm_win_open(int x, int y, int w, int h, const char *title) {
    return win_open_colors(x, y, w, h, title,
                            PM_COL_BLUE_BORDER, PM_COL_BLUE_BODY, PM_COL_BLUE_TITLE, 1);
}

/* Same "blue surface" window as pm_win_open(), but skips the drop
 * shadow -- pm_textpager() uses this so the column its shadow would
 * have occupied (x+w) can be reclaimed for a scrollbar instead
 * (user's own idea, rather than shrinking the interior text width and
 * needing to re-wrap/re-scrape every already-deployed chapter file to
 * fit). Kept static/local to pmwin.c, not promoted to pmwin.h, same
 * "only expose what's needed outside this file" rule pm_puts()/
 * pm_shadow() already followed -- pm_textpager() is the only caller. */
static int pm_win_open_noshadow(int x, int y, int w, int h, const char *title) {
    return win_open_colors(x, y, w, h, title,
                            PM_COL_BLUE_BORDER, PM_COL_BLUE_BODY, PM_COL_BLUE_TITLE, 0);
}

void pm_win_close(int handle) {
    if (handle < 0 || handle >= win_top) return;
    ScreenUpdate(win_stack[handle]);
    win_top = handle; /* also closes anything opened after it, if any */
}

void pm_win_interior(int handle, int *ix, int *iy, int *iw, int *ih) {
    if (handle < 0 || handle >= win_top) { *ix = *iy = *iw = *ih = 0; return; }
    *ix = win_rect[handle].x + 1;
    *iy = win_rect[handle].y + 1;
    *iw = win_rect[handle].w - 2;
    *ih = win_rect[handle].h - 2;
}

/* ---------------------------------------------------------------- events */

static int g_mouse_present = 0;

void pm_event_init(void) {
    g_mouse_present = pm_mouse_reset_detect();
    /* Deliberately NOT calling pm_mouse_show() here -- pm_poll_event()
     * now owns cursor visibility entirely (see its own comment), and
     * the very first frame this app ever draws happens before the
     * first pm_poll_event() call, same as every other frame; leaving
     * the cursor hidden until pm_poll_event() first shows it keeps
     * that first draw consistent with every later one. */
}

void pm_event_shutdown(void) {
    if (g_mouse_present) pm_mouse_hide();
}

int pm_mouse_available(void) { return g_mouse_present; }

/* getxkey() reports dedicated arrow/Home/End/PgUp/PgDn/Ins/Del keys
 * using their "enhanced keyboard" K_E*-prefixed codes, not the plain
 * K_Up-style ones -- only a numpad key pressed with NumLock off sends
 * the plain code. Virtually every real keyboard (and DOSBox) counts as
 * "enhanced", so every widget in this toolkit would otherwise ignore
 * the arrow keys entirely. Normalized centrally here so every call
 * site can just compare against the plain K_* constants. (Same gotcha
 * already hit once building MUFBlog -- missed re-applying it when this
 * toolkit was written from scratch.) */
static int normalize_key(int k) {
    switch (k) {
    case K_EUp:       return K_Up;
    case K_EDown:     return K_Down;
    case K_ELeft:     return K_Left;
    case K_ERight:    return K_Right;
    case K_EHome:     return K_Home;
    case K_EEnd:      return K_End;
    case K_EPageUp:   return K_PageUp;
    case K_EPageDown: return K_PageDown;
    case K_EInsert:   return K_Insert;
    case K_EDelete:   return K_Delete;
    default:          return k;
    }
}

/* Owns mouse-cursor visibility for the whole app, not just its own
 * polling logic: shows the cursor for exactly the stretch where
 * nothing is happening but waiting for the next input (safe -- no
 * video writes happen during that wait), then hides it again right
 * before returning control to the caller, since every caller's very
 * next action is to draw the next frame directly into video memory.
 * A DOS mouse driver's on-screen cursor is normally an XOR overlay it
 * manages itself; writing straight into video memory underneath it
 * without hiding first desyncs its own restore math and leaves
 * corruption behind. Real bug this fixes: pm_textpager()'s scrollbar
 * repeatedly rewrites one fixed column at the window's right edge
 * every frame while scrolling -- exactly where a resting cursor tends
 * to end up -- and was leaving garbage glyphs there on real hardware
 * (confirmed live; every other widget's draws happened to not collide
 * with a resting cursor often enough for this to surface before now,
 * but the gap -- never hiding the cursor around a direct screen
 * write, anywhere in this codebase -- was already there). */
void pm_poll_event(PMEvent *ev) {
    static int prev_buttons = 0;
    if (g_mouse_present) pm_mouse_show();
    for (;;) {
        if (kbhit()) {
            ev->type = PM_EV_KEY;
            ev->key = normalize_key(getxkey());
            if (g_mouse_present) pm_mouse_hide();
            return;
        }
        if (g_mouse_present) {
            int col, row, buttons, pressed;
            pm_mouse_get(&col, &row, &buttons);
            pressed = buttons & ~prev_buttons;
            prev_buttons = buttons;
            if (pressed & 0x01) {
                ev->type = PM_EV_CLICK; ev->mx = col; ev->my = row; ev->button = 0;
                pm_mouse_hide();
                return;
            }
            if (pressed & 0x02) {
                ev->type = PM_EV_CLICK; ev->mx = col; ev->my = row; ev->button = 1;
                pm_mouse_hide();
                return;
            }
        }
    }
}

/* -------------------------------------------------------------- labels */

/* Splits a "~F~ile" style label into plain text ("File") and the hotkey
 * char ('F'). dst must hold at least strlen(label) bytes. Returns the
 * hotkey char, or 0 if the label has no '~' markers. */
static char split_label(const char *label, char *dst) {
    int i, o = 0;
    char hot = 0;
    int in_tilde_run = 0;
    for (i = 0; label[i]; i++) {
        if (label[i] == '~') {
            if (!in_tilde_run && label[i + 1]) hot = label[i + 1];
            in_tilde_run = !in_tilde_run;
            continue;
        }
        dst[o++] = label[i];
    }
    dst[o] = 0;
    return hot;
}

static int hotkey_pos_in_plain(const char *label, int *out_pos) {
    /* Position (0-based, in the *plain* rendered string) of the char
     * immediately following the first '~'. */
    int i, plain_i = 0;
    for (i = 0; label[i]; i++) {
        if (label[i] == '~') { *out_pos = plain_i; return 1; }
        plain_i++;
    }
    return 0;
}

static void draw_label(int x, int y, const char *label, unsigned char normal,
                        unsigned char hot) {
    char plain[128];
    int hpos = -1, i, has_hot;
    split_label(label, plain);
    has_hot = hotkey_pos_in_plain(label, &hpos);
    for (i = 0; plain[i]; i++)
        put_clip(x + i, y, (unsigned char)plain[i], (has_hot && i == hpos) ? hot : normal);
}

static int alt_code_for_letter(char c) {
    static const char letters[] = "QWERTYUIOPASDFGHJKLZXCVBNM";
    static const int codes[] = {
        0x110,0x111,0x112,0x113,0x114,0x115,0x116,0x117,0x118,0x119, /* QWERTYUIOP */
        0x11e,0x11f,0x120,0x121,0x122,0x123,0x124,0x125,0x126,       /* ASDFGHJKL */
        0x12c,0x12d,0x12e,0x12f,0x130,0x131,0x132                    /* ZXCVBNM */
    };
    int i;
    char uc = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    for (i = 0; letters[i]; i++)
        if (letters[i] == uc) return codes[i];
    return 0;
}

/* -------------------------------------------------------------- menu bar */

static int menu_title_x(const PMMenu *menus, int nmenus, int idx) {
    int x = 1, i;
    for (i = 0; i < idx; i++) {
        char plain[128];
        split_label(menus[i].title, plain);
        x += (int)strlen(plain) + 2;
    }
    return x;
}

void pm_menubar_draw(const PMMenu *menus, int nmenus) {
    int i;
    pm_hline(0, 0, PM_COLS, ' ', PM_COL_MENUBAR);
    for (i = 0; i < nmenus; i++) {
        int x = menu_title_x(menus, nmenus, i);
        draw_label(x, 0, menus[i].title, PM_COL_MENUBAR, PM_COL_MENUHOT);
    }
}

/* Runs the pulldown for menus[idx] modally; may switch to an adjacent
 * top-level menu on Left/Right without returning. Returns a chosen
 * item's id, or PM_MENU_CANCEL. */
static int menu_modal(const PMMenu *menus, int nmenus, int idx) {
    for (;;) {
        const PMMenu *m = &menus[idx];
        int i, maxlen = 0, sel = 0, handle, ix, iy, iw, ih;
        int px = menu_title_x(menus, nmenus, idx);

        for (i = 0; i < m->nitems; i++) {
            char plain[128];
            split_label(m->items[i].label, plain);
            if ((int)strlen(plain) > maxlen) maxlen = (int)strlen(plain);
        }
        {
            int w = maxlen + 4, h = m->nitems + 2;
            if (px + w > PM_COLS) px = PM_COLS - w;
            if (px < 0) px = 0;
            handle = win_open_colors(px, 1, w, h, NULL,
                                      PM_COL_GRAY_BORDER, PM_COL_GRAY_BODY, PM_COL_GRAY_TITLE, 1);
            pm_win_interior(handle, &ix, &iy, &iw, &ih);
        }

        for (;;) {
            PMEvent ev;
            for (i = 0; i < m->nitems; i++) {
                unsigned char normal = (i == sel) ? PM_COL_GRAY_SELECTED : PM_COL_GRAY_BODY;
                unsigned char hot = (i == sel) ? PM_COL_GRAY_SELHOT : PM_COL_GRAY_HOT;
                pm_hline(ix, iy + i, iw, ' ', normal);
                draw_label(ix + 1, iy + i, m->items[i].label, normal, hot);
            }

            pm_poll_event(&ev);

            if (ev.type == PM_EV_KEY) {
                if (ev.key == K_Escape) { pm_win_close(handle); return PM_MENU_CANCEL; }
                if (ev.key == K_Up) { sel = (sel - 1 + m->nitems) % m->nitems; continue; }
                if (ev.key == K_Down) { sel = (sel + 1) % m->nitems; continue; }
                if (ev.key == K_Return) { pm_win_close(handle); return m->items[sel].id; }
                if (ev.key == K_Left || ev.key == K_Right) {
                    pm_win_close(handle);
                    idx = (ev.key == K_Left) ? (idx - 1 + nmenus) % nmenus
                                              : (idx + 1) % nmenus;
                    goto next_menu;
                }
                /* direct hotkey match within this pulldown */
                for (i = 0; i < m->nitems; i++) {
                    char hp[128]; char hot;
                    hot = split_label(m->items[i].label, hp);
                    if (hot && (ev.key == (unsigned char)hot ||
                                ev.key == (unsigned char)(hot | 0x20) ||
                                ev.key == (unsigned char)(hot & ~0x20))) {
                        pm_win_close(handle);
                        return m->items[i].id;
                    }
                }
            } else { /* PM_EV_CLICK */
                if (ev.my >= iy && ev.my < iy + ih && ev.mx >= ix && ev.mx < ix + iw) {
                    sel = ev.my - iy;
                    pm_win_close(handle);
                    return m->items[sel].id;
                }
                /* click landed on another top-level menu title -> switch */
                for (i = 0; i < nmenus; i++) {
                    int tx = menu_title_x(menus, nmenus, i);
                    char plain[128]; int tlen;
                    split_label(menus[i].title, plain);
                    tlen = (int)strlen(plain);
                    if (ev.my == 0 && ev.mx >= tx && ev.mx < tx + tlen) {
                        pm_win_close(handle);
                        idx = i;
                        goto next_menu;
                    }
                }
                /* click elsewhere -> cancel */
                pm_win_close(handle);
                return PM_MENU_CANCEL;
            }
        }
    next_menu:
        continue;
    }
}

int pm_menubar_handle(const PMMenu *menus, int nmenus, const PMEvent *ev) {
    int i;
    if (ev->type == PM_EV_KEY) {
        if (ev->key == K_F10) return menu_modal(menus, nmenus, 0);
        for (i = 0; i < nmenus; i++) {
            char plain[128]; char hot = split_label(menus[i].title, plain);
            if (hot && alt_code_for_letter(hot) == ev->key)
                return menu_modal(menus, nmenus, i);
        }
        return PM_MENU_NONE;
    }
    if (ev->type == PM_EV_CLICK && ev->my == 0) {
        for (i = 0; i < nmenus; i++) {
            int tx = menu_title_x(menus, nmenus, i);
            char plain[128]; int tlen;
            split_label(menus[i].title, plain);
            tlen = (int)strlen(plain);
            if (ev->mx >= tx && ev->mx < tx + tlen)
                return menu_modal(menus, nmenus, i);
        }
    }
    return PM_MENU_NONE;
}

/* ---------------------------------------------------------------- panel */

void pm_panel_page_layout(int nitems, int top, int max_rows, int max_cols,
                           int *cols, int *rows) {
    int this_page, cap;
    if (max_rows < 1) max_rows = 1;
    if (max_cols < 1) max_cols = 1;
    cap = max_cols * max_rows;
    this_page = nitems - top;
    if (this_page > cap) this_page = cap;
    if (this_page < 0) this_page = 0;
    if (this_page <= max_rows) {
        *cols = 1;
        *rows = this_page > 0 ? this_page : 1;
    } else {
        *cols = (this_page + max_rows - 1) / max_rows; /* ceil */
        if (*cols > max_cols) *cols = max_cols;
        *rows = max_rows;
    }
}

void pm_panel_draw(int x, int y, int w, int h, const char *title, const char *title2,
                    const char * const *items, int nitems, int top, int sel,
                    int cols, int rows) {
    /* "Recessed" look (user request): a second, untitled box drawn 1
     * cell inside the outer one -- same border/fill colors, no
     * separate visual weight of its own, just a thin inset frame
     * around the real content, like a mat around a picture. Usable
     * content starts 1 cell further in than a plain single-border box
     * would (ix/iy step in by 2 total, not 1) -- see panel_geometry()'s
     * own comment for how its width/height budgeting accounts for the
     * same overhead. iy steps one further row still when `title2` is
     * non-empty, to leave its own row free just below the border --
     * see the loop just below. */
    int ix = x + 2, iy = y + 2, iw = w - 4;
    int c, r, colw;
    pm_shadow(x, y, w, h);
    /* The outer border carries no title (user request: "all text
     * inside, including the box titles") -- it's a plain frame, purely
     * decorative, with nothing touching the true outer edge. The title
     * lives on the *inset* border instead, so it (and everything else)
     * reads as genuinely inside the recessed frame rather than
     * stranded out on the box's true boundary. */
    pm_box(x, y, w, h, PM_COL_GRAY_BORDER, PM_COL_GRAY_BODY, NULL, PM_COL_GRAY_TITLE);
    /* PM_COL_GRAY_BORDER is light-gray-on-light-gray -- invisible
     * against this same box's own fill, which is exactly right for the
     * *outer* border (a solid-looking rectangle against the blue
     * desktop is the whole point there) but would make this *inner*
     * recessed frame invisible too, defeating its purpose. Borrows
     * PM_COL_GRAY_TITLE (white-on-light-gray, already used for this
     * surface's box titles) instead, purely for its foreground color --
     * a real visible line, not a color that clashes with the theme. */
    pm_box(x + 1, y + 1, w - 2, h - 2, PM_COL_GRAY_TITLE, PM_COL_GRAY_BODY, title, PM_COL_GRAY_TITLE);
    if (title2 && title2[0]) {
        /* A second title row, drawn as ordinary (non-border) content
         * directly under the inset border's own title row -- pm_box()
         * itself only ever draws a title on a border LINE, so a wrapped
         * second line can't live there too; this is the plain-content
         * equivalent instead, centered the same way pm_box() centers
         * its own title, using PM_COL_GRAY_TITLE so it still reads as
         * part of the title rather than a list item. Item rows (the
         * loop below) start one row lower than usual to make room --
         * matching iy's own +1 here exactly, and matching the caller's
         * independent click-hit-testing math in sefer.c's main loop
         * (see its own comment on panel_title_l2). */
        int tlen = (int)strlen(title2);
        int tx2 = ix + (iw - tlen) / 2;
        if (tx2 < ix) tx2 = ix;
        pm_hline(ix, iy, iw, ' ', PM_COL_GRAY_BODY);
        puts_clip(tx2, iy, title2, PM_COL_GRAY_TITLE);
        iy++;
    }
    if (cols < 1) cols = 1;
    colw = iw / cols;
    for (c = 0; c < cols; c++) {
        int cx = ix + c * colw;
        int cw = (c == cols - 1) ? (iw - c * colw) : colw; /* last column absorbs any remainder */
        for (r = 0; r < rows; r++) {
            int idx = top + c * rows + r;
            unsigned char a = (idx < nitems && idx == sel) ? PM_COL_GRAY_SELECTED : PM_COL_GRAY_BODY;
            pm_hline(cx, iy + r, cw, ' ', a);
            if (idx < nitems) puts_clip(cx + 1, iy + r, items[idx], a);
        }
    }
}

/* -------------------------------------------------------------- list box */

int pm_listbox(int x, int y, int w, int h, const char *title,
               const char * const *items, int nitems, int start_sel) {
    int handle, ix, iy, iw, ih;
    int sel = start_sel, top = 0, visible, i;

    /* PM_COL_GRAY_TITLE, not PM_COL_GRAY_BORDER, for the border color --
     * user caught this live: the "Open Which Source?" picker had no
     * visible border at all, blending straight into whatever gray-
     * surface box was sitting behind/around it (the Explore browse
     * screen). PM_COL_GRAY_BORDER is light-gray-on-light-gray,
     * deliberately invisible against this surface's own fill (see
     * pm_panel_draw()'s own comment on this exact color) -- correct
     * for THAT function's own outer shadow-casting rectangle, which
     * gets a second, visible recessed inset border drawn on top of it,
     * but pm_listbox() never draws that second box at all, so its
     * only border was always the invisible one. Deliberately NOT
     * replicating pm_panel_draw()'s full double-recessed-border
     * treatment here (a second inset box would cost 2 more rows/cols
     * of interior space on every side) -- this widget is regularly
     * used at heights as small as 5 rows (see test_listbox_keyboard's
     * own fixture), where losing 4 total rows to a second border would
     * be a real, disproportionate cost; a single VISIBLE border (same
     * color pm_panel_draw()'s own inset border already reuses for
     * exactly this "make it a real visible line" purpose) fixes the
     * actual complaint with zero geometry change -- ix/iy/iw/ih below
     * are untouched, so every existing caller's own coordinates
     * (sefer.c's citation picker, every pm_listbox() test) still land
     * correctly with no further changes needed. */
    handle = win_open_colors(x, y, w, h, title,
                              PM_COL_GRAY_TITLE, PM_COL_GRAY_BODY, PM_COL_GRAY_TITLE, 1);
    pm_win_interior(handle, &ix, &iy, &iw, &ih);
    visible = ih;
    if (sel < 0) sel = 0;
    if (sel >= nitems) sel = nitems > 0 ? nitems - 1 : 0;

    for (;;) {
        PMEvent ev;
        if (sel < top) top = sel;
        if (sel >= top + visible) top = sel - visible + 1;

        for (i = 0; i < visible; i++) {
            int idx = top + i;
            pm_hline(ix, iy + i, iw, ' ', PM_COL_GRAY_BODY);
            if (idx < nitems) {
                unsigned char a = (idx == sel) ? PM_COL_GRAY_SELECTED : PM_COL_GRAY_BODY;
                pm_hline(ix, iy + i, iw, ' ', a);
                puts_clip(ix + 1, iy + i, items[idx], a);
            }
        }

        pm_poll_event(&ev);

        if (ev.type == PM_EV_KEY) {
            if (ev.key == K_Escape) { pm_win_close(handle); return -1; }
            if (ev.key == K_Return) { pm_win_close(handle); return sel; }
            if ((ev.key == K_Up) && sel > 0) sel--;
            else if ((ev.key == K_Down) && sel < nitems - 1) sel++;
            else if (ev.key == K_PageUp) { sel -= visible; if (sel < 0) sel = 0; }
            else if (ev.key == K_PageDown) { sel += visible; if (sel > nitems - 1) sel = nitems - 1; }
            else if (ev.key == K_Home) sel = 0;
            else if (ev.key == K_End) sel = nitems > 0 ? nitems - 1 : 0;
        } else { /* PM_EV_CLICK */
            if (ev.mx >= ix && ev.mx < ix + iw && ev.my >= iy && ev.my < iy + visible) {
                int idx = top + (ev.my - iy);
                if (idx < nitems) {
                    if (idx == sel) { pm_win_close(handle); return sel; } /* 2nd click on same row = choose */
                    sel = idx;
                }
            } else {
                pm_win_close(handle);
                return -1;
            }
        }
    }
}

/* ------------------------------------------------------------ text pager */

int pm_textpager(int x, int y, int w, int h, const char *title,
                  const char * const *lines, int nlines, int start_line) {
    int handle, ix, iy, iw, ih;
    int top, visible, i;
    int has_scrollbar, sb_x;

    handle = pm_win_open_noshadow(x, y, w, h, title);
    pm_win_interior(handle, &ix, &iy, &iw, &ih);
    visible = ih;
    top = start_line;
    if (top > nlines - visible) top = nlines - visible;
    if (top < 0) top = 0;

    /* Only shown when there's actually more to scroll to -- a chapter
     * that already fits keeps the exact look it always had. Lives in
     * the column immediately right of the window's own right border
     * (x+w), i.e. exactly where the drop shadow pm_win_open_noshadow()
     * skips would have started -- interior width/wrapping untouched,
     * so every already-scraped chapter file still fits correctly. */
    has_scrollbar = nlines > visible;
    sb_x = x + w;

    for (;;) {
        PMEvent ev;
        for (i = 0; i < visible; i++) {
            int idx = top + i;
            pm_hline(ix, iy + i, iw, ' ', PM_COL_BLUE_BODY);
            if (idx < nlines) puts_clip_rich(ix, iy + i, lines[idx], PM_COL_BLUE_BODY, iw);
        }
        if (has_scrollbar) {
            /* Standard proportional scrollbar math: thumb size shrinks
             * with how much longer the chapter is than one screenful,
             * thumb position tracks `top` linearly across the range of
             * positions the thumb can actually occupy (visible -
             * thumb_h) against the range `top` can actually take
             * (nlines - visible) -- not just `top`'s own raw fraction
             * of `nlines`, which would let the thumb hang half off the
             * bottom of the track when near the end. */
            int thumb_h = (visible * visible) / nlines;
            int max_top = nlines - visible;
            int thumb_top;
            if (thumb_h < 1) thumb_h = 1;
            thumb_top = (max_top > 0) ? (top * (visible - thumb_h)) / max_top : 0;
            for (i = 0; i < visible; i++) {
                int on_thumb = (i >= thumb_top && i < thumb_top + thumb_h);
                put_clip(sb_x, iy + i, on_thumb ? 219 : 176, PM_COL_BLUE_BODY);
            }
        }

        pm_poll_event(&ev);

        if (ev.type == PM_EV_KEY) {
            if (ev.key == K_Escape) { pm_win_close(handle); return PM_PAGER_CLOSED; }
            else if (ev.key == K_Left) { pm_win_close(handle); return PM_PAGER_PREV; }
            else if (ev.key == K_Right) { pm_win_close(handle); return PM_PAGER_NEXT; }
            else if (ev.key == K_Up && top > 0) top--;
            else if (ev.key == K_Down && top < nlines - visible) top++;
            else if (ev.key == K_PageUp) { top -= visible; if (top < 0) top = 0; }
            else if (ev.key == K_PageDown) {
                top += visible;
                if (top > nlines - visible) top = nlines - visible;
                if (top < 0) top = 0;
            }
            else if (ev.key == K_Home) top = 0;
            else if (ev.key == K_End) { top = nlines - visible; if (top < 0) top = 0; }
        } else { /* PM_EV_CLICK */
            int in_content = (ev.mx >= ix && ev.mx < ix + iw && ev.my >= iy && ev.my < iy + visible);
            int on_scrollbar = (has_scrollbar && ev.mx == sb_x && ev.my >= iy && ev.my < iy + visible);
            if (in_content || on_scrollbar) {
                /* Clicking above/below the thumb pages up/down, same
                 * convention the content area itself already used --
                 * an exact jump-to-position click would need drag
                 * support this toolkit doesn't have. */
                if (ev.my < iy + visible / 2) { top -= visible; if (top < 0) top = 0; }
                else { top += visible; if (top > nlines - visible) top = nlines - visible; if (top < 0) top = 0; }
            } else {
                pm_win_close(handle);
                return PM_PAGER_CLOSED;
            }
        }
    }
}

/* -------------------------------------------------------------- text input */

#define PM_INPUT_H 5 /* border, prompt, blank, field, border */

int pm_input(int x, int y, int w, const char *title, const char *prompt,
             char *buf, int bufsize) {
    int handle, ix, iy, iw, ih;
    int len = (int)strlen(buf);
    int pos = len;

    handle = pm_win_open(x, y, w, PM_INPUT_H, title);
    pm_win_interior(handle, &ix, &iy, &iw, &ih);

    for (;;) {
        PMEvent ev;
        pm_hline(ix, iy, iw, ' ', PM_COL_BLUE_BODY);
        puts_clip(ix, iy, prompt, PM_COL_BLUE_BODY);
        pm_hline(ix, iy + 2, iw, ' ', PM_COL_BLUE_BODY);
        puts_clip(ix, iy + 2, buf, PM_COL_BLUE_BODY);
        ScreenSetCursor(iy + 2, ix + pos);

        pm_poll_event(&ev);

        if (ev.type == PM_EV_KEY) {
            if (ev.key == K_Return) {
                ScreenSetCursor(PM_ROWS - 1, 0);
                pm_win_close(handle);
                return 1;
            }
            if (ev.key == K_Escape) {
                ScreenSetCursor(PM_ROWS - 1, 0);
                pm_win_close(handle);
                return 0;
            }
            if (ev.key == K_Left && pos > 0) pos--;
            else if (ev.key == K_Right && pos < len) pos++;
            else if (ev.key == K_Home) pos = 0;
            else if (ev.key == K_End) pos = len;
            else if (ev.key == K_BackSpace && pos > 0) {
                memmove(buf + pos - 1, buf + pos, len - pos + 1); /* incl. NUL */
                pos--; len--;
            } else if (ev.key == K_Delete && pos < len) {
                memmove(buf + pos, buf + pos + 1, len - pos); /* incl. NUL */
                len--;
            } else if (ev.key >= 32 && ev.key < 127 && len < bufsize - 1) {
                memmove(buf + pos + 1, buf + pos, len - pos + 1); /* incl. NUL */
                buf[pos] = (char)ev.key;
                pos++; len++;
            }
        } else { /* PM_EV_CLICK */
            if (ev.mx >= ix && ev.mx < ix + iw && ev.my == iy + 2) {
                pos = ev.mx - ix;
                if (pos < 0) pos = 0;
                if (pos > len) pos = len;
            } else if (!(ev.mx >= x && ev.mx < x + w && ev.my >= y && ev.my < y + PM_INPUT_H)) {
                ScreenSetCursor(PM_ROWS - 1, 0);
                pm_win_close(handle);
                return 0;
            }
        }
    }
}

/* -------------------------------------------------------------- misc UI */

void pm_hint(const char *text) {
    pm_hline(0, PM_ROWS - 1, PM_COLS, ' ', PM_COL_HINT);
    puts_clip(1, PM_ROWS - 1, text, PM_COL_HINT);
}

void pm_msgbox(const char *title, const char *msg) {
    int w = (int)strlen(msg) + 6;
    int h = 4;
    int x, y, handle, ix, iy, iw, ih;
    if (w > PM_COLS - 4) w = PM_COLS - 4;
    x = (PM_COLS - w) / 2;
    y = (PM_ROWS - h) / 2;
    handle = pm_win_open(x, y, w, h, title);
    pm_win_interior(handle, &ix, &iy, &iw, &ih);
    puts_clip(ix + 1, iy, msg, PM_COL_BLUE_BODY);
    for (;;) {
        PMEvent ev;
        pm_poll_event(&ev);
        if (ev.type == PM_EV_KEY && (ev.key == K_Return || ev.key == K_Escape)) break;
        if (ev.type == PM_EV_CLICK) break;
    }
    pm_win_close(handle);
}
