/* render.h -- turns a loaded DagPage into an array of ready-to-draw text
 * lines (word-wrapped to REN_LINE_WIDTH) plus a table of link positions,
 * for the DOS-side link-aware pager to display and navigate. Pure text
 * processing, no video/keyboard calls -- natively testable like dagfile.c.
 *
 * Two control-byte pairs may appear in a finished output line:
 *   PM_BOLD_ON/PM_BOLD_OFF (0x01/0x02) -- same convention pmwin.c's
 *     pm_textpager already uses; this reader's own pager honors it too.
 *   REN_LINK_ON/REN_LINK_OFF (0x05/0x06) -- new for this reader. Both
 *     pairs are zero-width: not drawn, not counted toward column
 *     position, by whatever code walks a finished line to render it. */
#ifndef RENDER_H
#define RENDER_H

#include "dagfile.h"

#define REN_LINK_ON  '\x05'
#define REN_LINK_OFF '\x06'

#define REN_LINE_WIDTH 74   /* visible columns per wrapped line */

typedef struct {
    unsigned long targetId;
    int lineIndex;      /* which output line this link's REN_LINK_ON falls on */
} RenLink;

typedef struct {
    char **lines;        /* lineCount malloc'd strings, each REN_LINE_WIDTH+headroom bytes */
    unsigned char *lineIsHeading; /* parallel array: 1 = draw this line with the heading attribute */
    int lineCount;
    int lineCapacity;

    RenLink *links;
    int linkCount;
    int linkCapacity;
} RenPage;

/* Builds rp from page: breadcrumb line, title, infobox (if any) as a
 * boxed fact-sheet block, journal entries (if any), then the body --
 * headings, inline tables (rendered as stacked label/value blocks
 * regardless of original column count -- a wide multi-column table
 * doesn't fit 74 columns as a grid, so each data row instead renders as
 * its own small stack using the header row as labels), and wrapped
 * prose with [[id|display]] markers turned into REN_LINK_ON/OFF spans.
 * Returns 1 on success, 0 on allocation failure (rp is safe to
 * ren_free() either way). */
int ren_build(const DagPage *page, RenPage *rp);
void ren_free(RenPage *rp);

#endif
