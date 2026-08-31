/* linkpager.c -- see linkpager.h. Structurally mirrors pmwin.c's
 * pm_textpager (same window-open/redraw/poll-event loop shape) but adds
 * link-focus tracking; small helpers below (put_clip, PM_ROWS/COLS) are
 * deliberately duplicated from pmwin.c rather than exposed from it,
 * matching this project's existing "duplicate small helpers rather than
 * share across files" convention (see e.g. Sefer's copy_bounded()). */
#include <pc.h>
#include <keys.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "pmwin.h"
#include "linkpager.h"

#define PM_ROWS 25
#define PM_COLS 80

/* "Find on page" (F key) state -- a list of case-insensitive substring
 * matches of the last query against the page's own visible text (control
 * bytes stripped out first, so `col` here is in the same "visible column"
 * space draw_rich_line() already counts in). Left/Right cycle `cur`;
 * draw_rich_line() highlights every hit, `cur` a brighter shade than the
 * rest. Lives for the lifetime of one dag_pager() call -- a fresh F
 * search replaces it outright, and it's never carried into a different
 * page (a new dag_pager() call gets a fresh zeroed one). */
#define FIND_MAX_HITS 200
typedef struct { int lineIndex; int col; } FindHit;
typedef struct {
    int active;      /* 1 once a search has found at least one hit */
    int hitCount;
    int cur;         /* index into hits[] of the currently-jumped-to match */
    int queryLen;    /* highlight span width, in visible columns */
    FindHit hits[FIND_MAX_HITS];
} FindState;

static void put_clip(int x, int y, int ch, unsigned char attr) {
    if (x < 0 || x >= PM_COLS || y < 0 || y >= PM_ROWS) return;
    ScreenPutChar(ch, attr, x, y);
}

/* Same zero-width-control-byte interpretation as pmwin.c's own
 * puts_clip_rich(), but for REN_LINK_ON/OFF instead of PM_BOLD_ON/OFF,
 * and switching to `linkAttr` normally or `focusAttr` when the span
 * being drawn is link index `focusLink` (the currently Tab-focused one,
 * or -1 if none) -- found by counting REN_LINK_ON occurrences on this
 * line and matching against rp->links entries whose lineIndex equals
 * `lineIdx` (those entries are already in left-to-right order within a
 * line, since render.c appends them in the same order it emits the
 * corresponding tokens). */
static void draw_rich_line(int x, int y, const char *s, unsigned char attr,
                            unsigned char linkAttr, unsigned char focusAttr,
                            int maxcol, const RenPage *rp, int lineIdx, int focusLink,
                            const FindState *fs) {
    int col = 0, i;
    int curLink = -1;
    int nthLinkOnLine = 0;
    int firstLinkIdx = -1, li;
    unsigned char matchAttr = PM_ATTR(PM_BLACK, PM_BROWN);
    unsigned char curMatchAttr = PM_ATTR(PM_BLACK, PM_YELLOW);

    for (li = 0; li < rp->linkCount; li++) {
        if (rp->links[li].lineIndex == lineIdx) { firstLinkIdx = li; break; }
    }

    for (i = 0; s[i] && col < maxcol; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == REN_LINK_ON) {
            curLink = (firstLinkIdx >= 0) ? firstLinkIdx + nthLinkOnLine : -1;
            nthLinkOnLine++;
            continue;
        }
        if (c == REN_LINK_OFF) { curLink = -1; continue; }
        {
            unsigned char useAttr = attr;
            if (curLink >= 0) useAttr = (curLink == focusLink) ? focusAttr : linkAttr;
            if (fs && fs->active) {
                int fi;
                for (fi = 0; fi < fs->hitCount; fi++) {
                    if (fs->hits[fi].lineIndex == lineIdx
                        && col >= fs->hits[fi].col && col < fs->hits[fi].col + fs->queryLen) {
                        useAttr = (fi == fs->cur) ? curMatchAttr : matchAttr;
                        break;
                    }
                }
            }
            put_clip(x + col, y, c, useAttr);
            col++;
        }
    }
}

/* Which link (if any) owns the visible column `clickCol` on line
 * `lineIdx` -- same span-counting logic as draw_rich_line() above, just
 * checking one specific column instead of drawing every character.
 * Returns the link's index into rp->links, or -1 if that column isn't
 * inside any link span (including if it's blank/past the end of the
 * line, or the line has no links at all). */
static int link_at_column(const RenPage *rp, int lineIdx, int clickCol) {
    const char *s;
    int col = 0, i;
    int curLink = -1;
    int nthLinkOnLine = 0;
    int firstLinkIdx = -1, li;

    if (lineIdx < 0 || lineIdx >= rp->lineCount) return -1;
    s = rp->lines[lineIdx];
    for (li = 0; li < rp->linkCount; li++) {
        if (rp->links[li].lineIndex == lineIdx) { firstLinkIdx = li; break; }
    }
    for (i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == REN_LINK_ON) { curLink = (firstLinkIdx >= 0) ? firstLinkIdx + nthLinkOnLine : -1; nthLinkOnLine++; continue; }
        if (c == REN_LINK_OFF) { curLink = -1; continue; }
        if (col == clickCol) return curLink;
        col++;
    }
    return -1;
}

#define SEARCH_MAX_RESULTS 20
#define FUZZY_MAX_RESULTS 5

/* '/' key: prompts for a query, searches by title (case-insensitive
 * substring first; if that finds nothing, falls back to Levenshtein
 * "did you mean" suggestions), and lets the user pick a result from a
 * list. Returns 1 with *outTarget set if the user picked something to
 * navigate to, 0 if they cancelled at any step (caller just keeps
 * showing the current page). */
static int do_search(DagFile *df, unsigned long *outTarget) {
    char query[64] = "";
    DagSearchResult results[SEARCH_MAX_RESULTS];
    int n;
    int fuzzy = 0;

    if (!pm_input(20, 10, 40, "Search", "Page title:", query, sizeof(query))) return 0;
    if (!query[0]) return 0;

    n = dag_search(df, query, results, SEARCH_MAX_RESULTS);
    if (n == 0) {
        n = dag_fuzzy_suggest(df, query, results, FUZZY_MAX_RESULTS);
        fuzzy = 1;
        if (n == 0) { pm_msgbox("Search", "No pages found."); return 0; }
    }

    if (n == 1 && !fuzzy) {
        *outTarget = results[0].id;
        return 1;
    }

    {
        char itemBuf[SEARCH_MAX_RESULTS][DAG_TITLE_MAX];
        const char *itemPtrs[SEARCH_MAX_RESULTS];
        int i, sel;
        for (i = 0; i < n; i++) {
            dag_get_title(df, results[i].id, itemBuf[i], sizeof(itemBuf[i]));
            itemPtrs[i] = itemBuf[i];
        }
        sel = pm_listbox(15, 8, 50, n + 4 < 18 ? n + 4 : 18,
                          fuzzy ? "Did you mean?" : "Search results", itemPtrs, n, 0);
        if (sel < 0) return 0;
        *outTarget = results[sel].id;
        return 1;
    }
}

/* Case-insensitive strstr -- not in DJGPP's libc, and this project's
 * other case-insensitive matching (dag_search) works over whole titles
 * rather than needing a "find first occurrence" primitive, so it hasn't
 * come up until now. */
static const char *stristr(const char *hay, const char *needle) {
    size_t nlen = strlen(needle);
    if (!nlen) return NULL;
    for (; *hay; hay++) {
        size_t i;
        for (i = 0; i < nlen; i++) {
            unsigned char a = (unsigned char)hay[i];
            if (!a) return NULL;
            if (tolower(a) != tolower((unsigned char)needle[i])) break;
        }
        if (i == nlen) return hay;
    }
    return NULL;
}

/* Strips REN_LINK_ON/OFF out of a rendered line so both the search and
 * the resulting hit column line up with draw_rich_line()'s own "visible
 * column" counting (which likewise skips those two bytes). Buffer size
 * matches test_city.c's own clean-line scratch size -- generous headroom
 * over REN_LINE_BUF's largest declared line buffer (render.c). */
static void clean_line(const char *s, char *out, int outsz) {
    int i, k = 0;
    for (i = 0; s[i] && k < outsz - 1; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == REN_LINK_ON || c == REN_LINK_OFF) continue;
        out[k++] = (char)c;
    }
    out[k] = 0;
}

/* Finds every case-insensitive occurrence of `query` in the page's own
 * text (searching overlapping occurrences too -- advances one character
 * past each match's start rather than past its end -- since a wiki page
 * is small enough that this costs nothing and a query like "ss" inside
 * "boss's" is a real if rare case worth not missing). Fills *fs and
 * returns the hit count. */
static int do_find(const RenPage *rp, const char *query, FindState *fs) {
    char clean[400];
    int li;
    fs->hitCount = 0;
    fs->cur = 0;
    fs->active = 0;
    fs->queryLen = (int)strlen(query);
    if (!fs->queryLen) return 0;
    for (li = 0; li < rp->lineCount && fs->hitCount < FIND_MAX_HITS; li++) {
        const char *p;
        clean_line(rp->lines[li], clean, sizeof(clean));
        p = clean;
        for (;;) {
            const char *m = stristr(p, query);
            if (!m || fs->hitCount >= FIND_MAX_HITS) break;
            fs->hits[fs->hitCount].lineIndex = li;
            fs->hits[fs->hitCount].col = (int)(m - clean);
            fs->hitCount++;
            p = m + 1;
        }
    }
    fs->active = fs->hitCount > 0;
    return fs->hitCount;
}

/* '?' key: a plain reference card of every shortcut, since the hint bar
 * itself now just says "? - Help" rather than spelling controls out (see
 * dag_pager()'s hint text below) -- there wasn't room left on one line
 * once Find/Left/Right joined Tab/Enter/Search/Back. Not built on
 * pm_msgbox (single-line only, see pmwin.c) -- just a small window drawn
 * and torn down the same way, with one pm_puts() per line. */
static void show_help(void) {
    static const char *lines[] = {
        "TAB / SHIFT+TAB    Next / previous link",
        "ENTER              Follow the focused link",
        "Up/Down/PgUp/PgDn/Home/End   Scroll the page",
        "S  or  /           Search for a page by title",
        "F                  Find text on this page",
        "Left / Right       Jump to next / previous match",
        "BACKSPACE          Go back to the previous page",
        "A                  Show this article's UESP source & license",
        "Mouse              Click a link, or the scrollbar",
        "ESC                Close this help / quit the reader",
    };
    int n = (int)(sizeof(lines) / sizeof(lines[0]));
    int w = 62, h = n + 4;
    int x = (PM_COLS - w) / 2, y = (PM_ROWS - h) / 2;
    int handle, ix, iy, iw, ih, i;

    handle = pm_win_open(x, y, w, h, "Help");
    pm_win_interior(handle, &ix, &iy, &iw, &ih);
    (void)iw; (void)ih;
    for (i = 0; i < n; i++) pm_puts(ix, iy + i, lines[i], PM_COL_BLUE_BODY);
    for (;;) {
        PMEvent ev;
        pm_poll_event(&ev);
        if (ev.type == PM_EV_KEY && (ev.key == K_Return || ev.key == K_Escape)) break;
        if (ev.type == PM_EV_CLICK) break;
    }
    pm_win_close(handle);
}

/* Breaks `s` into fixed-width chunks of at most `width` bytes appended
 * to `lines` (a plain character wrap, not a word wrap -- a URL has no
 * spaces to break on once its own spaces became underscores). Used only
 * by show_attribution() below for its two URL lines, which can run
 * longer than one popup row for a long article title. */
static void wrap_chunks(char lines[][80], int *n, int maxn, const char *s, int width) {
    int len = (int)strlen(s);
    int pos = 0;
    if (len == 0) { if (*n < maxn) { lines[*n][0] = 0; (*n)++; } return; }
    while (pos < len && *n < maxn) {
        int chunk = width;
        if (pos + chunk > len) chunk = len - pos;
        memcpy(lines[*n], s + pos, chunk);
        lines[*n][chunk] = 0;
        (*n)++;
        pos += chunk;
    }
}

/* 'A' key: UESP's own suggested BY-SA attribution notice (see
 * https://en.uesp.net/wiki/UESPWiki:Copyright_and_Ownership), reproduced
 * for whichever article is currently on screen -- the article link and
 * its history-page link (the two ways UESP says the "give attribution" /
 * "give access to the page" obligations can be met), plus the license
 * itself. Both URLs are rebuilt on the fly from `pageTitle` (this
 * corpus's own display title, namespace prefix already stripped by
 * convert.js) -- nothing extra needs to be stored per page for this,
 * since re-prepending "Daggerfall:" is always correct here (the whole
 * corpus is that one namespace) and MediaWiki's own URL convention is
 * just spaces-to-underscores, nothing fancier. Not meant to be clickable
 * (there's no live networking on this DOS machine, or in DOS text mode
 * at all) -- printing the exact address is the standard way an offline
 * reuse of BY-SA content satisfies "give access to the page" when there
 * is no real hyperlink to give; see NOTICE.md for the project-wide
 * statement this per-article popup exists alongside. */
static void show_attribution(const char *pageTitle) {
    char nsTitle[DAG_TITLE_MAX + 16];
    char urlTitle[DAG_TITLE_MAX + 16];
    char wikiUrl[DAG_TITLE_MAX + 48];
    char histUrl[DAG_TITLE_MAX + 64];
    char quoted[DAG_TITLE_MAX + 4];
    char lines[24][80];
    int n = 0, i;
    int w, h, x, y, handle, ix, iy, iw, ih;

    strcpy(nsTitle, "Daggerfall:");
    strncat(nsTitle, pageTitle, sizeof(nsTitle) - strlen(nsTitle) - 1);
    strcpy(urlTitle, nsTitle);
    for (i = 0; urlTitle[i]; i++) if (urlTitle[i] == ' ') urlTitle[i] = '_';
    sprintf(wikiUrl, "https://en.uesp.net/wiki/%s", urlTitle);
    sprintf(histUrl, "https://en.uesp.net/index.php?title=%s&action=history", urlTitle);
    sprintf(quoted, "\"%s\":", nsTitle);

    strcpy(lines[n++], "This article uses material from the UESP article");
    wrap_chunks(lines, &n, 24, quoted, 72);
    wrap_chunks(lines, &n, 24, wikiUrl, 72);
    strcpy(lines[n++], "");
    strcpy(lines[n++], "History (contributor attribution):");
    wrap_chunks(lines, &n, 24, histUrl, 72);
    strcpy(lines[n++], "");
    strcpy(lines[n++], "Licensed under the Creative Commons BY-SA 2.5 license:");
    wrap_chunks(lines, &n, 24, "https://creativecommons.org/licenses/by-sa/2.5/", 72);

    w = 78; h = n + 4;
    if (h > PM_ROWS - 2) h = PM_ROWS - 2;
    x = (PM_COLS - w) / 2; y = (PM_ROWS - h) / 2;
    handle = pm_win_open(x, y, w, h, "Attribution");
    pm_win_interior(handle, &ix, &iy, &iw, &ih);
    for (i = 0; i < n && i < ih; i++) pm_puts(ix, iy + i, lines[i], PM_COL_BLUE_BODY);
    for (;;) {
        PMEvent ev;
        pm_poll_event(&ev);
        if (ev.type == PM_EV_KEY && (ev.key == K_Return || ev.key == K_Escape)) break;
        if (ev.type == PM_EV_CLICK) break;
    }
    pm_win_close(handle);
}

/* Scrolls `*top` just enough to bring `line` on screen (centered, same
 * convention as the Tab-focus auto-scroll below) -- shared by the F
 * search jump and the Left/Right hit-cycling, which both need exactly
 * this. */
static void scroll_to_line(int *top, int line, int visible, int lineCount) {
    int maxTop = lineCount - visible;
    if (maxTop < 0) maxTop = 0;
    if (line < *top || line >= *top + visible) {
        *top = line - visible / 2;
        if (*top < 0) *top = 0;
        if (*top > maxTop) *top = maxTop;
    }
}

int dag_pager(int x, int y, int w, int h, const char *title, const char *pageTitle,
              DagFile *df, const RenPage *rp, int start_line, unsigned long *outTarget) {
    int handle, ix, iy, iw, ih;
    int top, visible, i;
    int focusLink = -1;
    unsigned char linkAttr = PM_ATTR(PM_LIGHTBLUE, PM_BLACK);
    unsigned char focusAttr = PM_ATTR(PM_BLACK, PM_LIGHTBLUE);
    unsigned char headAttr = PM_ATTR(PM_YELLOW, PM_BLACK);
    FindState fs;

    int has_scrollbar, sb_x;

    fs.active = 0;
    fs.hitCount = 0;
    fs.cur = 0;
    fs.queryLen = 0;

    handle = pm_win_open(x, y, w, h, title);
    pm_win_interior(handle, &ix, &iy, &iw, &ih);
    visible = ih;
    top = start_line;
    if (top > rp->lineCount - visible) top = rp->lineCount - visible;
    if (top < 0) top = 0;

    /* This pager runs full-screen (no spare "shadow" column outside the
     * window the way Sefer's smaller pm_textpager windows have room for)
     * -- draw the scrollbar directly on the window's own right border
     * column instead of a separate track. Shown whenever there's more
     * than one screenful, mouse or not (it's still useful as a pure
     * position indicator with a keyboard alone). */
    has_scrollbar = rp->lineCount > visible;
    sb_x = x + w - 1;

    for (;;) {
        PMEvent ev;

        for (i = 0; i < visible; i++) {
            int idx = top + i;
            pm_hline(ix, iy + i, iw, ' ', PM_COL_BLUE_BODY);
            if (idx < rp->lineCount) {
                unsigned char base = rp->lineIsHeading[idx] ? headAttr : PM_COL_BLUE_BODY;
                draw_rich_line(ix, iy + i, rp->lines[idx], base, linkAttr, focusAttr, iw, rp, idx, focusLink, &fs);
            }
        }
        if (has_scrollbar) {
            /* Same proportional thumb-size/position math as pmwin.c's
             * pm_textpager. */
            int thumb_h = (visible * visible) / rp->lineCount;
            int max_top = rp->lineCount - visible;
            int thumb_top;
            if (thumb_h < 1) thumb_h = 1;
            thumb_top = (max_top > 0) ? (top * (visible - thumb_h)) / max_top : 0;
            for (i = 0; i < visible; i++) {
                int on_thumb = (i >= thumb_top && i < thumb_top + thumb_h);
                put_clip(sb_x, iy + i, on_thumb ? 219 : 176, PM_COL_BLUE_BODY);
            }
        }
        /* Nothing in this widget is a text-entry field, so the blinking
         * hardware cursor has no useful place to be -- left unset, it
         * stays wherever a previous frame/program happened to leave it,
         * which can land it right on top of body text (looks like a
         * corrupted character, not a cursor, in a screenshot). Park it
         * bottom-right corner, off any real content. */
        ScreenSetCursor(PM_ROWS - 1, PM_COLS - 1);
        /* Full control list moved into the '?' help popup (show_help())
         * now that Find/Left/Right joined Tab/Enter/Search/Back -- no
         * longer fits spelled out on one hint line. */
        pm_hint("? - Help");

        pm_poll_event(&ev);

        if (ev.type != PM_EV_KEY) {
            /* Mouse click -- entirely additive, every keyboard path above
             * and below is untouched and still works with no mouse at all. */
            if (ev.mx >= ix && ev.mx < ix + iw && ev.my >= iy && ev.my < iy + visible) {
                int lineIdx = top + (ev.my - iy);
                int hit = link_at_column(rp, lineIdx, ev.mx - ix);
                if (hit >= 0) {
                    *outTarget = rp->links[hit].targetId;
                    pm_win_close(handle);
                    return LP_FOLLOW;
                }
                /* Clicked real content but not on a link -- nothing to do. */
            } else if (has_scrollbar && ev.mx == sb_x && ev.my >= iy && ev.my < iy + visible) {
                /* Click above/below the thumb pages up/down, same
                 * convention as pmwin.c's pm_textpager -- a real drag-to-
                 * position thumb would need drag support this toolkit
                 * doesn't have. */
                if (ev.my < iy + visible / 2) { top -= visible; if (top < 0) top = 0; }
                else { top += visible; if (top > rp->lineCount - visible) top = rp->lineCount - visible; if (top < 0) top = 0; }
            }
            continue;
        }

        if (ev.key == K_Escape) { pm_win_close(handle); return LP_CLOSED; }
        else if (ev.key == K_Return && focusLink >= 0) {
            *outTarget = rp->links[focusLink].targetId;
            pm_win_close(handle);
            return LP_FOLLOW;
        }
        else if (ev.key == '/' || ev.key == 's' || ev.key == 'S') {
            if (do_search(df, outTarget)) { pm_win_close(handle); return LP_FOLLOW; }
            /* cancelled at any step -- redraw this same page, nothing changed */
        }
        else if (ev.key == '?') { show_help(); }
        else if (ev.key == 'a' || ev.key == 'A') { show_attribution(pageTitle); }
        else if (ev.key == 'f' || ev.key == 'F') {
            char query[64] = "";
            if (pm_input(20, 10, 40, "Find", "Find on page:", query, sizeof(query)) && query[0]) {
                int n = do_find(rp, query, &fs);
                if (n == 0) pm_msgbox("Find", "No matches on this page.");
                else scroll_to_line(&top, fs.hits[fs.cur].lineIndex, visible, rp->lineCount);
            }
            /* cancelled, or query left blank -- previous find (if any) is
             * left exactly as it was, matching how a plain Esc/blank
             * behaves everywhere else in this pager. */
        }
        else if (ev.key == K_Left && fs.active) {
            fs.cur = (fs.cur <= 0) ? fs.hitCount - 1 : fs.cur - 1;
            scroll_to_line(&top, fs.hits[fs.cur].lineIndex, visible, rp->lineCount);
        }
        else if (ev.key == K_Right && fs.active) {
            fs.cur = (fs.cur + 1) % fs.hitCount;
            scroll_to_line(&top, fs.hits[fs.cur].lineIndex, visible, rp->lineCount);
        }
        else if (ev.key == 8 /* Backspace */) { pm_win_close(handle); return LP_BACK; }
        else if (ev.key == K_Up && top > 0) top--;
        else if (ev.key == K_Down && top < rp->lineCount - visible) top++;
        else if (ev.key == K_PageUp) { top -= visible; if (top < 0) top = 0; }
        else if (ev.key == K_PageDown) {
            top += visible;
            if (top > rp->lineCount - visible) top = rp->lineCount - visible;
            if (top < 0) top = 0;
        }
        else if (ev.key == K_Home) top = 0;
        else if (ev.key == K_End) { top = rp->lineCount - visible; if (top < 0) top = 0; }
        else if ((ev.key == K_Tab || ev.key == K_BackTab) && rp->linkCount > 0) {
            int focusVisible = focusLink >= 0 && rp->links[focusLink].lineIndex >= top
                                && rp->links[focusLink].lineIndex < top + visible;
            if (!focusVisible) {
                /* No link focused yet, OR one is focused but the user has
                 * since scrolled away from it with PgUp/PgDn/arrows (its
                 * line is no longer even on screen) -- either way, blindly
                 * incrementing from the stale index below would jump to
                 * wherever that old link sequence happens to continue,
                 * which can be far from the current view. Re-anchor to
                 * what's actually on screen right now instead. Tab picks
                 * the first link at/after the current view; Shift-Tab
                 * picks the last one at/before it; with no link anywhere
                 * near the view, fall back to the document's first/last
                 * link respectively. */
                int i2, pick = -1;
                if (ev.key == K_Tab) {
                    for (i2 = 0; i2 < rp->linkCount; i2++) if (rp->links[i2].lineIndex >= top) { pick = i2; break; }
                    focusLink = (pick >= 0) ? pick : 0;
                } else {
                    for (i2 = rp->linkCount - 1; i2 >= 0; i2--) if (rp->links[i2].lineIndex < top + visible) { pick = i2; break; }
                    focusLink = (pick >= 0) ? pick : rp->linkCount - 1;
                }
            } else if (ev.key == K_Tab) focusLink = (focusLink + 1) % rp->linkCount;
            else focusLink = (focusLink <= 0) ? rp->linkCount - 1 : focusLink - 1;
            /* Auto-scroll so the newly focused link's line is on screen,
             * centered when possible so cycling doesn't hug an edge. */
            {
                int line = rp->links[focusLink].lineIndex;
                if (line < top || line >= top + visible) {
                    top = line - visible / 2;
                    if (top < 0) top = 0;
                    if (top > rp->lineCount - visible) top = rp->lineCount - visible;
                    if (top < 0) top = 0;
                }
            }
        }
    }
}
