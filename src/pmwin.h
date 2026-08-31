/* pmwin -- a small Pegasus-Mail-flavored text-mode window toolkit for
 * FreeDOS/DJGPP. Blue "TUI" look: framed, drop-shadowed windows, a
 * pull-down menu bar, a modal list box, and a modal text pager.
 *
 * Keyboard drives every single feature. A mouse (INT 33h) is detected
 * once at startup and, if present, can additionally click the same
 * things a keyboard user can reach -- but nothing in this toolkit or
 * anything built on it may ever require the mouse. See mouse.h/.c.
 *
 * Not tied to any particular content -- this file has no knowledge of
 * Judaic texts, sacred-texts.com, or any specific app built on it.
 */
#ifndef PMWIN_H
#define PMWIN_H

/* ---------------------------------------------------------------- colors */

#define PM_BLACK        0
#define PM_BLUE         1
#define PM_GREEN        2
#define PM_CYAN         3
#define PM_RED          4
#define PM_MAGENTA      5
#define PM_BROWN        6
#define PM_LIGHTGRAY    7
#define PM_DARKGRAY     8
#define PM_LIGHTBLUE    9
#define PM_LIGHTGREEN   10
#define PM_LIGHTCYAN    11
#define PM_LIGHTRED     12
#define PM_LIGHTMAGENTA 13
#define PM_YELLOW       14
#define PM_WHITE        15

/* Attribute byte builder. Background is masked to 3 bits (0-7) so bit 7
 * of the attribute byte can never be set here: on stock VGA/CGA/EGA text
 * mode that bit means BLINK, not a bright background, and setting it by
 * accident makes text flash instead of showing a bright color. (Real bug
 * hit once building MUFBlog -- see that project's notes.) pm_video_init()
 * additionally reprograms the adapter so bit 7 means intensity anyway,
 * but the mask stays here too as a second, independent line of defense. */
#define PM_ATTR(fg, bg) ((unsigned char)((((bg) & 0x07) << 4) | ((fg) & 0x0F)))

/* Theme, reverse-engineered by actually running the real PMAIL.EXE in
 * DOSBox and looking at it (see project notes) rather than guessing.
 * Pegasus Mail's own DOS UI alternates between two distinct surfaces,
 * not one flat color scheme:
 *
 *   - "blue" surface: the main desktop, the menu bar, the text pager,
 *     and simple info/confirm dialogs. Blue field, white double-line
 *     border and title, light-gray body text, plain white hint text
 *     with no separate colored bar underneath it.
 *   - "gray" surface: floating pickers -- its own folder-select popup
 *     is the clearest example. Light-gray field, blue label text, a
 *     solid blue selection bar with bright white text. Pull-down
 *     menus, list boxes, and the always-visible library panel all use
 *     this surface, since they're all "floating picker" widgets.
 *
 * Both surfaces share the same solid-black drop shadow. */

/* This project's own theme -- black background, white text, blue links
 * (user request) -- NOT the PMAIL blue-surface look these constant names
 * were originally written for. This pmwin.c/h is an independent fork of
 * Sefer's (see project notes), so retheming here has no effect on Sefer;
 * kept the same constant names rather than renaming them everywhere they're
 * used, since "the toolkit's one blue-ish surface" is still the right
 * concept, just repainted. */
#define PM_COL_DESKTOP        PM_ATTR(PM_WHITE,     PM_BLACK)
#define PM_COL_MENUBAR        PM_ATTR(PM_WHITE,     PM_BLACK)
#define PM_COL_MENUHOT        PM_ATTR(PM_YELLOW,    PM_BLACK)
#define PM_COL_HINT           PM_ATTR(PM_WHITE,     PM_BLACK)
#define PM_COL_SHADOW         PM_ATTR(PM_BLACK,     PM_BLACK)

#define PM_COL_BLUE_BORDER    PM_ATTR(PM_WHITE,     PM_BLACK)
#define PM_COL_BLUE_TITLE     PM_ATTR(PM_WHITE,     PM_BLACK)
#define PM_COL_BLUE_BODY      PM_ATTR(PM_WHITE,     PM_BLACK)

#define PM_COL_GRAY_BORDER    PM_ATTR(PM_LIGHTGRAY, PM_LIGHTGRAY)
#define PM_COL_GRAY_TITLE     PM_ATTR(PM_WHITE,     PM_LIGHTGRAY)
#define PM_COL_GRAY_BODY      PM_ATTR(PM_BLUE,      PM_LIGHTGRAY)
#define PM_COL_GRAY_HOT       PM_ATTR(PM_RED,       PM_LIGHTGRAY)
#define PM_COL_GRAY_SELECTED  PM_ATTR(PM_WHITE,     PM_BLUE)
#define PM_COL_GRAY_SELHOT    PM_ATTR(PM_YELLOW,    PM_BLUE)
/* Yellow on the same light-gray fill every other gray-surface body
 * color already uses -- distinct from PM_COL_GRAY_SELHOT just above
 * (yellow-on-BLUE, for a selected item's own hot key on a blue
 * surface). User request: Daily Learning's own segment text. */
#define PM_COL_GRAY_YELLOW    PM_ATTR(PM_YELLOW,    PM_LIGHTGRAY)

/* ---------------------------------------------------------- video lifecycle */

/* Call once at startup, before drawing anything. Saves the whole screen
 * and reprograms attribute bit 7 to mean "bright background" instead of
 * "blink" (BIOS INT 10h AX=1003h). */
void pm_video_init(void);

/* Call once at shutdown. Restores the original blink behavior and
 * whatever was on screen before pm_video_init(), so a program that ran
 * before this one (e.g. COMMAND.COM) isn't left in a weird state. */
void pm_video_shutdown(void);

/* --------------------------------------------------------- screen drawing */

void pm_screen_clear_desktop(void);
void pm_hline(int x, int y, int w, int ch, unsigned char attr);
/* Clipped string output -- for small always-visible desktop fixtures
 * (like pmwin.c's own pm_panel_draw) that draw straight onto the
 * desktop rather than through a modal window. */
void pm_puts(int x, int y, const char *s, unsigned char attr);
void pm_box(int x, int y, int w, int h, unsigned char border_attr,
            unsigned char fill_attr, const char *title, unsigned char title_attr);
/* The solid-black drop shadow trailing a box's bottom-right corner --
 * shared by every window/panel/link-tile in the toolkit. Callers that
 * draw their own fixture (not via pm_win_open or pm_panel_draw) should
 * call this immediately before pm_box() for the same x,y,w,h. */
void pm_shadow(int x, int y, int w, int h);

/* --------------------------------------------------------------- windows */

/* Windows nest on an internal snapshot stack: pm_win_open() snapshots
 * the *entire* screen (whatever is on it, including other open windows),
 * then draws a bordered, drop-shadowed box on top and returns a handle.
 * pm_win_close() restores the exact prior snapshot. This means windows
 * always nest/restore correctly with no manual region bookkeeping, as
 * long as they're closed in the reverse order they were opened (true of
 * every widget in this toolkit -- they're all strictly modal). */
#define PM_MAX_WINDOWS 8
int  pm_win_open(int x, int y, int w, int h, const char *title);
void pm_win_close(int handle);
void pm_win_interior(int handle, int *ix, int *iy, int *iw, int *ih);

/* ---------------------------------------------------------------- events */

typedef enum { PM_EV_NONE, PM_EV_KEY, PM_EV_CLICK } PMEventType;

typedef struct {
    PMEventType type;
    int key;    /* valid when type == PM_EV_KEY: a getxkey()-style code */
    int mx, my; /* valid when type == PM_EV_CLICK: character-cell coords */
    int button; /* valid when type == PM_EV_CLICK: 0 = left, 1 = right */
} PMEvent;

/* Detects a mouse driver (harmless, fully optional, does nothing if one
 * isn't found) and shows the cursor if one was. Call once at startup,
 * after pm_video_init(). */
void pm_event_init(void);
void pm_event_shutdown(void);
int  pm_mouse_available(void);

/* Blocks until either a key is pressed or, only if pm_mouse_available(),
 * a mouse button is clicked. Every widget in this toolkit consumes this
 * single unified event stream, so nothing here ever has to branch on
 * whether a mouse exists -- when there isn't one, PM_EV_CLICK events
 * simply never occur. */
void pm_poll_event(PMEvent *ev);

/* -------------------------------------------------------------- menu bar */

/* '~' in a label marks the hotkey letter, Turbo-Vision style, e.g.
 * "~F~ile" underlines/highlights the F and its Alt+F activates the menu.
 * A label may contain at most one '~' pair. */
typedef struct {
    const char *label;
    int id; /* returned by pm_menubar_handle() when this item is chosen */
} PMMenuItem;

typedef struct {
    const char *title;
    const PMMenuItem *items;
    int nitems;
} PMMenu;

void pm_menubar_draw(const PMMenu *menus, int nmenus);

#define PM_MENU_NONE   (-1) /* event had nothing to do with the menu bar */
#define PM_MENU_CANCEL (-2) /* a menu was opened, then dismissed */

/* Feed every polled event through this before handling it yourself. If
 * the event activates the menu bar (F10, Alt+hotkey, or a click on the
 * bar), this runs its own modal loop right here and returns either a
 * chosen item's id, or PM_MENU_CANCEL. Otherwise it returns PM_MENU_NONE
 * immediately, having not consumed the event -- go ahead and handle it
 * yourself. */
int pm_menubar_handle(const PMMenu *menus, int nmenus, const PMEvent *ev);

/* ---------------------------------------------------------------- panel */

/* A permanent, non-modal list display -- unlike pm_listbox, this does
 * NOT open on the window stack or run its own event loop. The caller
 * redraws it every frame (as part of its normal desktop redraw) and
 * handles its own Up/Down/Left/Right/Enter/click navigation against
 * `sel`/`top` state it owns itself; see sefer.c's main loop for the
 * pattern. This is what makes an always-visible picker possible --
 * PMAIL's folder list being the model -- so an entire category of
 * navigation doesn't have to live behind a menu.
 *
 * Items lay out in up to `cols` columns of `rows` rows each,
 * column-major (a column fills completely -- ascending -- before the
 * next begins), matching how a caller widens the box outward before
 * ever needing to scroll -- see pm_panel_page_layout() below, which
 * computes both, and is how a caller should always arrive at the
 * `cols`/`rows`/`top` values passed in here (this function trusts them
 * as already-resolved, it does no layout math of its own). `cols`==1
 * reproduces this widget's original single-column-only behavior
 * exactly. */
/* `title2` -- a second title-bar line, drawn as its own centered row
 * directly under the recessed border's title row, item rows pushed
 * down by one to make room -- lets a caller (sefer.c's panel_geometry()
 * / wrap_panel_title()) word-wrap a title too long for one line rather
 * than let it overflow past the box's own right border. Pass "" (not
 * NULL) when the title didn't need to wrap; `h` must already include
 * the extra row when `title2` is non-empty (panel_geometry() does
 * this). */
void pm_panel_draw(int x, int y, int w, int h, const char *title, const char *title2,
                    const char * const *items, int nitems, int top, int sel,
                    int cols, int rows);

/* Computes the column/row layout for ONE PAGE of a pm_panel_draw()
 * list: given the item list's full `nitems` and the flat index `top`
 * of the first item on the currently-displayed page (0 the first
 * time, then only ever moved by the caller in exact multiples of a
 * full page -- e.g. by +-`cols_cap * max_rows` on Left/Right at a page
 * edge, or PgUp/PgDn -- this widget doesn't own navigation, see
 * pm_panel_draw()'s own comment), returns:
 *   *cols -- columns THIS page needs (<= max_cols)
 *   *rows -- rows per column THIS page needs (<= max_rows)
 * A page shorter than `max_rows` needs only 1 column (identical to
 * this widget's original shrink-to-fit single-column sizing); once it
 * needs more than `max_rows` items, columns are added -- up to
 * `max_cols` -- before ever assuming the caller will scroll instead.
 * The caller is responsible for its own width budget: pm_panel_draw()
 * has no idea how wide any item's text is, so a caller with a max
 * total width should compute its own `max_cols` from that budget and
 * this item list's longest string before calling this -- see
 * panel_geometry() in sefer.c for the pattern (its own PANEL_MAXCOLS
 * further caps how many columns it's ever willing to use even when
 * width would allow more). */
void pm_panel_page_layout(int nitems, int top, int max_rows, int max_cols,
                           int *cols, int *rows);

/* -------------------------------------------------------------- list box */

/* Modal list picker. Opens its own window, handles Up/Down/PgUp/PgDn/
 * Home/End/Enter/Esc (and, if present, click-to-select /
 * double-click-to-choose / click-outside-to-cancel) itself. Returns the
 * chosen index, or -1 if cancelled. */
int pm_listbox(int x, int y, int w, int h, const char *title,
               const char * const *items, int nitems, int start_sel);

/* ------------------------------------------------------------ text pager */

/* Inline formatting markers a chapter-text line may contain -- two
 * control-character bytes (0x01/0x02, never appearing in real scraped
 * English text) that toggle bold on/off mid-line. pm_textpager()'s own
 * line renderer is the only thing that interprets them: it treats them
 * as zero-width (not drawn, not counted toward column position) and
 * switches between the pager's normal body color and a brighter
 * variant of it for everything between an ON and the next OFF. Added
 * for the Talmud content (the William Davidson translation's own
 * bold/plain distinction -- bold is the core translated text, plain is
 * Steinsaltz's inserted connective phrases -- is worth keeping visible
 * rather than flattening away just because a flat DOS text file has no
 * room for real formatting metadata). A line with neither byte renders
 * exactly as it always has -- fully backward compatible with every
 * chapter file already deployed, none of which contain these bytes. */
#define PM_BOLD_ON  '\x01'
#define PM_BOLD_OFF '\x02'

/* Modal scrolling viewer over an in-memory array of already-wrapped
 * lines. PgUp/PgDn/Up/Down/Home/End/Esc (and, if present, clicking the
 * top/bottom border edges to scroll, or clicking outside to close).
 * `start_line` opens already scrolled to that line (clamped in range);
 * pass 0 to start at the top as before. Lines may contain
 * PM_BOLD_ON/PM_BOLD_OFF, rendered as described above.
 *
 * Returns why it closed: PM_PAGER_CLOSED for Esc/outside-click (the
 * normal case), or PM_PAGER_PREV/PM_PAGER_NEXT if Left/Right was
 * pressed -- this widget has no idea what "adjacent" means (it's just
 * an array of lines), so it hands the request back to the caller
 * rather than trying to load anything itself; the caller decides
 * whether an adjacent chapter/daf actually exists and reopens a new
 * pager for it if so. */
#define PM_PAGER_CLOSED 0
#define PM_PAGER_PREV  (-1)
#define PM_PAGER_NEXT  1
int pm_textpager(int x, int y, int w, int h, const char *title,
                  const char * const *lines, int nlines, int start_line);

/* -------------------------------------------------------------- text input */

/* Modal single-line text entry: a prompt line over an editable field.
 * `buf` is used both as the starting value (pre-fill by writing into
 * it before calling, or pass an empty string) and the result buffer,
 * up to `bufsize` bytes including the terminator. Left/Right/Home/End/
 * Backspace/Delete/insert-at-cursor, or (if present) clicking within
 * the field to reposition the cursor. Returns 1 with `buf` filled in
 * on Enter, or 0 (buf left as whatever was typed, caller should
 * ignore it) on Esc or a click outside the window. */
int pm_input(int x, int y, int w, const char *title, const char *prompt,
             char *buf, int bufsize);

/* -------------------------------------------------------------- misc UI */

void pm_hint(const char *text);
void pm_msgbox(const char *title, const char *msg);

#endif /* PMWIN_H */
