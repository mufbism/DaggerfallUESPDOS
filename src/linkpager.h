/* linkpager.h -- a fork of pmwin.c's pm_textpager specialized for
 * RenPage: same modal scrolling window, plus Tab/Shift-Tab cycling
 * through the page's links (auto-scrolling to keep the focused one on
 * screen) and Enter to follow the focused link. A genuine fork, not an
 * extension of pm_textpager itself -- kept separate so the original stays
 * untouched for any future reuse, same as this project copying pmwin.c
 * wholesale rather than sharing Sefer's copy by reference. */
#ifndef LINKPAGER_H
#define LINKPAGER_H

#include "dagfile.h"
#include "render.h"

#define LP_CLOSED 0  /* Esc -- caller should quit the app */
#define LP_FOLLOW 1  /* Enter on a focused link, or a chosen search result -- *outTarget is the page id to load next */
#define LP_BACK   2  /* Backspace -- caller should pop its history and reopen the previous page */

/* Opens at (x,y,w,h), title in the border, scrolled to start_line (0 =
 * top). `df` is needed for the in-pager search feature ('/' key) --
 * search operates over the whole title database, not just the page
 * currently on screen. `pageTitle` is the currently-displayed page's own
 * title (DagPage.title, namespace prefix already stripped) -- needed by
 * the 'A' (attribution) popup to reconstruct the real uesp.net URL and
 * history-page URL for this specific article on the fly, with nothing
 * extra stored in the .DAT for it (every page in this corpus is the
 * Daggerfall: namespace, so re-prepending that prefix is always
 * correct). Returns one of the LP_* codes above; *outTarget is only
 * meaningful when the return is LP_FOLLOW. */
int dag_pager(int x, int y, int w, int h, const char *title, const char *pageTitle,
              DagFile *df, const RenPage *rp, int start_line, unsigned long *outTarget);

#endif
