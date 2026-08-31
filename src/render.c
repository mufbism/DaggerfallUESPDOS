#include "render.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define REN_CELL_MAX 512
#define REN_MAX_TABLE_COLS 24 /* real corpus max is 20 (Totambu's service-locations-style table) */
#define REN_LINE_BUF (REN_LINE_WIDTH * 2 + 32) /* headroom for REN_LINK_ON/OFF bytes */

/* ---------------------------------------------------------------- setup */

static int ensure_line_capacity(RenPage *rp, int need) {
    if (need <= rp->lineCapacity) return 1;
    {
        int newCap = rp->lineCapacity ? rp->lineCapacity * 2 : 256;
        char **newLines;
        unsigned char *newFlags;
        while (newCap < need) newCap *= 2;
        newLines = (char **)realloc(rp->lines, newCap * sizeof(char *));
        if (!newLines) return 0;
        rp->lines = newLines;
        newFlags = (unsigned char *)realloc(rp->lineIsHeading, newCap);
        if (!newFlags) return 0;
        rp->lineIsHeading = newFlags;
        rp->lineCapacity = newCap;
    }
    return 1;
}

static int ensure_link_capacity(RenPage *rp, int need) {
    if (need <= rp->linkCapacity) return 1;
    {
        int newCap = rp->linkCapacity ? rp->linkCapacity * 2 : 64;
        RenLink *newLinks;
        while (newCap < need) newCap *= 2;
        newLinks = (RenLink *)realloc(rp->links, newCap * sizeof(RenLink));
        if (!newLinks) return 0;
        rp->links = newLinks;
        rp->linkCapacity = newCap;
    }
    return 1;
}

static int add_line(RenPage *rp, const char *text, int isHeading) {
    char *copy;
    if (!ensure_line_capacity(rp, rp->lineCount + 1)) return 0;
    copy = (char *)malloc(strlen(text) + 1);
    if (!copy) return 0;
    strcpy(copy, text);
    rp->lines[rp->lineCount] = copy;
    rp->lineIsHeading[rp->lineCount] = (unsigned char)isHeading;
    rp->lineCount++;
    return 1;
}

static int add_link(RenPage *rp, unsigned long targetId, int lineIndex) {
    if (!ensure_link_capacity(rp, rp->linkCount + 1)) return 0;
    rp->links[rp->linkCount].targetId = targetId;
    rp->links[rp->linkCount].lineIndex = lineIndex;
    rp->linkCount++;
    return 1;
}

/* --------------------------------------------------------- word-wrapping */

/* One token from a paragraph: either a plain word, or an atomic
 * [[id|display]] link (never split across a line-wrap boundary, same
 * treatment as a single long word). */
typedef struct {
    const char *text;   /* not nul-terminated -- use len */
    int len;
    int isLink;
    unsigned long targetId;
    int noSpaceBefore;  /* true if no whitespace separated this token from
                          * the previous one in the source (e.g. a comma or
                          * period glued directly onto a preceding link) --
                          * without this, every token boundary would get an
                          * inserted space regardless of the source, turning
                          * "[[Bergama|Bergama]]." into "Bergama ." */
} Token;

/* Tokenizes `text`, filling tokens[] (caller-sized) up to maxTokens.
 * Returns the number of tokens found. */
static int tokenize(const char *text, Token *tokens, int maxTokens) {
    int n = 0;
    const char *p = text;
    while (*p && n < maxTokens) {
        const char *beforeSpace = p;
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (!*p) break;
        tokens[n].noSpaceBefore = (n > 0 && p == beforeSpace);
        if (p[0] == '[' && p[1] == '[') {
            const char *start = p + 2;
            const char *bar = strchr(start, '|');
            const char *end = strstr(start, "]]");
            if (bar && end && bar < end) {
                tokens[n].isLink = 1;
                tokens[n].targetId = (unsigned long)strtoul(start, NULL, 10);
                tokens[n].text = bar + 1;
                tokens[n].len = (int)(end - (bar + 1));
                n++;
                p = end + 2;
                continue;
            }
            /* malformed -- fall through and treat "[[" as literal text */
        }
        {
            const char *start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
            tokens[n].isLink = 0;
            tokens[n].text = start;
            tokens[n].len = (int)(p - start);
            n++;
        }
    }
    return n;
}

/* Wraps one paragraph's worth of `text` (no "\n\n" inside it -- see
 * wrap_paragraph() below, which splits on that first) to REN_LINE_WIDTH
 * visible columns, emitting finished lines via add_line() and recording
 * each link's line index via add_link(). */
static int wrap_single_paragraph(RenPage *rp, const char *text) {
    Token tokens[600];
    int ntok = tokenize(text, tokens, 600);
    int i;
    char buf[REN_LINE_BUF];
    int col = 0, blen = 0;
    unsigned long pendingLinks[64];
    int npending = 0;

    buf[0] = '\0';

    for (i = 0; i < ntok; i++) {
        int wordWidth = tokens[i].len;
        int needSpace = (col > 0 && !tokens[i].noSpaceBefore);
        int spaceWidth = needSpace ? 1 : 0;
        if (col > 0 && col + spaceWidth + wordWidth > REN_LINE_WIDTH) {
            int k;
            if (!add_line(rp, buf, 0)) return 0;
            for (k = 0; k < npending; k++) if (!add_link(rp, pendingLinks[k], rp->lineCount - 1)) return 0;
            npending = 0;
            col = 0; blen = 0; buf[0] = '\0'; needSpace = 0;
        }
        if (needSpace) { buf[blen++] = ' '; col++; }
        if (tokens[i].isLink) {
            if (npending < 64) pendingLinks[npending++] = tokens[i].targetId;
            buf[blen++] = REN_LINK_ON;
            memcpy(buf + blen, tokens[i].text, (size_t)tokens[i].len);
            blen += tokens[i].len;
            buf[blen++] = REN_LINK_OFF;
        } else {
            memcpy(buf + blen, tokens[i].text, (size_t)tokens[i].len);
            blen += tokens[i].len;
        }
        col += wordWidth;
        buf[blen] = '\0';
    }
    if (blen > 0) {
        int k;
        if (!add_line(rp, buf, 0)) return 0;
        for (k = 0; k < npending; k++) if (!add_link(rp, pendingLinks[k], rp->lineCount - 1)) return 0;
    }
    return 1;
}

/* Renders one hand-authored, already-fits-the-screen ASCII-art line
 * verbatim -- no word-wrapping at all (unlike every other renderer in
 * this file), since wrapping would destroy manually aligned connector
 * lines/columns. [[id|display]] markers still become real clickable
 * link spans, just placed inline exactly where written rather than
 * flowed. Caller is responsible for making sure the line actually fits
 * within REN_LINE_WIDTH visible columns -- this does not check. */
static int render_raw_line(RenPage *rp, const char *text) {
    char buf[REN_LINE_BUF + 128];
    size_t blen = 0;
    unsigned long pendingLinks[64];
    int npending = 0;
    const char *p = text;

    while (*p && blen + 4 < sizeof(buf)) {
        if (p[0] == '[' && p[1] == '[') {
            const char *start = p + 2;
            const char *bar = strchr(start, '|');
            const char *end = strstr(start, "]]");
            if (bar && end && bar < end) {
                size_t dlen = (size_t)(end - (bar + 1));
                if (dlen > sizeof(buf) - blen - 4) dlen = sizeof(buf) - blen - 4;
                if (npending < 64) pendingLinks[npending++] = (unsigned long)strtoul(start, NULL, 10);
                buf[blen++] = REN_LINK_ON;
                memcpy(buf + blen, bar + 1, dlen);
                blen += dlen;
                buf[blen++] = REN_LINK_OFF;
                p = end + 2;
                continue;
            }
        }
        buf[blen++] = *p++;
    }
    buf[blen] = '\0';

    if (!add_line(rp, buf, 0)) return 0;
    {
        int k;
        for (k = 0; k < npending; k++) if (!add_link(rp, pendingLinks[k], rp->lineCount - 1)) return 0;
    }
    return 1;
}

/* Renders one line of an indented tree/outline (used for the Main Quest
 * flowchart's DAG, hand-transcribed from the real rendered diagram since
 * the {{Chart}} template's own connector-glyph column alignment isn't
 * reliably reconstructable from wikitext alone -- see tools/convert.js).
 * `depth` (0 = root) becomes a real hanging indent -- wrap_paragraph's
 * tokenizer strips leading whitespace, so plain spaces in the text
 * itself wouldn't survive; this builds the indent as a literal prefix
 * outside the tokenizer instead, same technique render_infobox() already
 * uses for a long value's continuation lines. */
static int render_tree_line(RenPage *rp, int depth, const char *text) {
    char prefix[64];
    int prefixLen, avail;
    Token tokens[200];
    int ntok, i;
    char buf[REN_LINE_BUF];
    int col, blen;
    unsigned long pendingLinks[64];
    int npending = 0;
    int firstLine = 1;

    if (depth > 20) depth = 20; /* defensive cap, real tree here is ~6 deep */
    prefixLen = depth * 2;
    memset(prefix, ' ', (size_t)prefixLen);
    prefix[prefixLen] = '\0';
    avail = REN_LINE_WIDTH - prefixLen - 2; /* "- " marker */
    if (avail < 10) avail = 10;

    ntok = tokenize(text, tokens, 200);
    col = 0; blen = 0; buf[0] = '\0';

    for (i = 0; i < ntok; i++) {
        int wordWidth = tokens[i].len;
        int needSpace = (col > 0 && !tokens[i].noSpaceBefore);
        int spaceWidth = needSpace ? 1 : 0;
        if (col > 0 && col + spaceWidth + wordWidth > avail) {
            char line[REN_LINE_BUF + 128]; /* prefix (<=40) + marker (2) + buf (<=REN_LINE_BUF) must all fit */
            int k;
            sprintf(line, "%s%s %s", prefix, firstLine ? "-" : " ", buf);
            if (!add_line(rp, line, 0)) return 0;
            for (k = 0; k < npending; k++) if (!add_link(rp, pendingLinks[k], rp->lineCount - 1)) return 0;
            npending = 0;
            col = 0; blen = 0; buf[0] = '\0'; needSpace = 0; firstLine = 0;
        }
        if (needSpace) { buf[blen++] = ' '; col++; }
        if (tokens[i].isLink) {
            if (npending < 64) pendingLinks[npending++] = tokens[i].targetId;
            buf[blen++] = REN_LINK_ON;
            memcpy(buf + blen, tokens[i].text, (size_t)tokens[i].len);
            blen += tokens[i].len;
            buf[blen++] = REN_LINK_OFF;
        } else {
            memcpy(buf + blen, tokens[i].text, (size_t)tokens[i].len);
            blen += tokens[i].len;
        }
        col += wordWidth;
        buf[blen] = '\0';
    }
    {
        char line[REN_LINE_BUF + 128];
        int k;
        sprintf(line, "%s%s %s", prefix, firstLine ? "-" : " ", buf);
        if (!add_line(rp, line, 0)) return 0;
        for (k = 0; k < npending; k++) if (!add_link(rp, pendingLinks[k], rp->lineCount - 1)) return 0;
    }
    return 1;
}

/* Wraps `text` (plain prose, possibly containing [[id|display]] markers
 * and, from a multi-line table cell -- e.g. an in-game letter shown in a
 * bordered quote box -- "\n\n" paragraph breaks) to REN_LINE_WIDTH
 * columns. Splits on "\n\n" first and wraps each paragraph separately
 * with a blank line between, so a letter's greeting/body/signature keep
 * their real breaks instead of running together as one block of text. */
static int wrap_paragraph(RenPage *rp, const char *text) {
    const char *p = text;
    int first = 1;
    while (*p) {
        const char *brk = strstr(p, "\n\n");
        size_t len = brk ? (size_t)(brk - p) : strlen(p);
        if (len > 0) {
            char chunk[8192];
            size_t n = len < sizeof(chunk) - 1 ? len : sizeof(chunk) - 1;
            memcpy(chunk, p, n);
            chunk[n] = '\0';
            if (!first && !add_line(rp, "", 0)) return 0;
            if (!wrap_single_paragraph(rp, chunk)) return 0;
            first = 0;
        }
        if (!brk) break;
        p = brk + 2;
    }
    return 1;
}

/* Same wrapping, but every produced line is flagged as a heading line
 * (for a distinct draw color) instead of going through add_link at all
 * -- headings in this corpus never contain link markers. */
static int add_heading(RenPage *rp, const char *text) {
    /* Reuse the tokenizer/wrap loop's line-splitting, but mark isHeading. */
    Token tokens[64];
    int ntok = tokenize(text, tokens, 64);
    int i;
    char buf[REN_LINE_BUF];
    int col = 0, blen = 0;
    buf[0] = '\0';
    for (i = 0; i < ntok; i++) {
        int wordWidth = tokens[i].len;
        int needSpace = (col > 0 && !tokens[i].noSpaceBefore);
        int spaceWidth = needSpace ? 1 : 0;
        if (col > 0 && col + spaceWidth + wordWidth > REN_LINE_WIDTH) {
            if (!add_line(rp, buf, 1)) return 0;
            col = 0; blen = 0; buf[0] = '\0'; needSpace = 0;
        }
        if (needSpace) { buf[blen++] = ' '; col++; }
        memcpy(buf + blen, tokens[i].text, (size_t)tokens[i].len);
        blen += tokens[i].len;
        col += wordWidth;
        buf[blen] = '\0';
    }
    if (blen > 0) { if (!add_line(rp, buf, 1)) return 0; }
    else { if (!add_line(rp, "", 1)) return 0; }
    return 1;
}

/* --------------------------------------------------------------- tables */

/* Any [[id|display]] link inside a table cell renders as plain display
 * text -- boxes/stacked blocks are plain text only, no in-box navigation. */
static void plain_text(const char *src, char *dst, size_t dstsize) {
    size_t di = 0;
    const char *p = src;
    while (*p && di + 1 < dstsize) {
        if (p[0] == '[' && p[1] == '[') {
            const char *bar = strchr(p + 2, '|');
            const char *end = strstr(p + 2, "]]");
            if (bar && end && bar < end) {
                size_t n = (size_t)(end - (bar + 1));
                if (di + n >= dstsize) n = dstsize - 1 - di;
                memcpy(dst + di, bar + 1, n);
                di += n;
                p = end + 2;
                continue;
            }
        }
        dst[di++] = *p++;
    }
    dst[di] = '\0';
}

static int add_labelvalue(RenPage *rp, const char *label, const char *value) {
    char plain[REN_CELL_MAX];
    char buf[REN_CELL_MAX + 64];
    size_t labelLen = strlen(label);
    plain_text(value, plain, sizeof(plain));
    /* Some source labels already end in ':' (e.g. Orc's stat table cells
     * are literally "Level:", "HP:", ...) -- don't double it up. */
    if (labelLen && label[labelLen - 1] == ':') sprintf(buf, "%s %s", label, plain);
    else if (label[0]) sprintf(buf, "%s: %s", label, plain);
    else sprintf(buf, "%s", plain);
    return wrap_paragraph(rp, buf);
}

/* ---------------------------------------------------- table row buffer */

#define REN_MAX_TABLE_ROWS 150

typedef struct {
    int isHeader;
    int ncells;
    char cells[REN_MAX_TABLE_COLS][REN_CELL_MAX]; /* already plain_text()'d */
} TRow;

/* Parses one \x03...\x04 table-marker line (see tools/convert.js's
 * serializeTable) into a flat row buffer, resolving any [[id|display]]
 * link inside a cell to plain text as it goes (boxes are plain text only,
 * no in-box navigation). */
static int parse_table_rows(const char *line, TRow *rows, int *outCount) {
    const char *p = line + 1; /* skip leading 0x03 */
    int n = 0;
    while (*p && *p != '\x04' && n < REN_MAX_TABLE_ROWS) {
        int isHeader = (*p == 'H');
        int ncells = 0;
        p++; /* skip H/D flag */
        while (*p && *p != '\x1e' && *p != '\x04') {
            const char *start = p;
            char raw[REN_CELL_MAX];
            size_t rn;
            while (*p && *p != '\x1f' && *p != '\x1e' && *p != '\x04') p++;
            rn = (size_t)(p - start);
            if (rn >= REN_CELL_MAX) rn = REN_CELL_MAX - 1;
            memcpy(raw, start, rn);
            raw[rn] = '\0';
            if (ncells < REN_MAX_TABLE_COLS) {
                plain_text(raw, rows[n].cells[ncells], REN_CELL_MAX);
                ncells++;
            }
            if (*p == '\x1f') p++;
        }
        if (*p == '\x1e') p++;
        if (ncells == 0) continue;
        rows[n].isHeader = isHeader;
        rows[n].ncells = ncells;
        n++;
    }
    *outCount = n;
    return 1;
}

/* Draws rows[start..end) as a real bordered grid of `ncols` columns --
 * column widths sized to content (capped so no single column can blow
 * the budget), an optional bold header row from `header` (NULL = no
 * header row, e.g. independent label:value pairs where each row's own
 * first cell already IS its label). Returns 0 if it wouldn't fit within
 * REN_LINE_WIDTH at even the minimum reasonable widths -- caller should
 * fall back to stacked rendering instead; 1 if drawn successfully. */
#define REN_COL_MAX_WIDTH 22
static int render_grid(RenPage *rp, TRow *rows, int start, int end, int ncols,
                        char header[REN_MAX_TABLE_COLS][REN_CELL_MAX]) {
    int colw[REN_MAX_TABLE_COLS];
    int c, r, total;
    char line[REN_LINE_BUF];
    char border[REN_LINE_WIDTH + 8];

    for (c = 0; c < ncols; c++) {
        size_t w = header ? strlen(header[c]) : 0;
        for (r = start; r < end; r++) {
            size_t cw = strlen(rows[r].cells[c]);
            if (cw > w) w = cw;
        }
        if (w > REN_COL_MAX_WIDTH) w = REN_COL_MAX_WIDTH;
        colw[c] = (int)w;
    }
    total = 1;
    for (c = 0; c < ncols; c++) total += colw[c] + 3;
    if (total > REN_LINE_WIDTH) return 0;

    {
        int pos = 0;
        border[pos++] = '+';
        for (c = 0; c < ncols; c++) {
            int i;
            for (i = 0; i < colw[c] + 2; i++) border[pos++] = '-';
            border[pos++] = '+';
        }
        border[pos] = '\0';
    }

    if (!add_line(rp, border, 0)) return -1;
    if (header) {
        int pos = 0;
        line[pos++] = '|';
        for (c = 0; c < ncols; c++) {
            pos += sprintf(line + pos, " %-*.*s |", colw[c], colw[c], header[c]);
        }
        if (!add_line(rp, line, 1)) return -1;
        if (!add_line(rp, border, 0)) return -1;
    }
    for (r = start; r < end; r++) {
        int pos = 0;
        line[pos++] = '|';
        for (c = 0; c < ncols; c++) {
            pos += sprintf(line + pos, " %-*.*s |", colw[c], colw[c], rows[r].cells[c]);
        }
        if (!add_line(rp, line, 0)) return -1;
    }
    if (!add_line(rp, border, 0)) return -1;
    if (!add_line(rp, "", 0)) return -1;
    return 1;
}

/* Fallback for a segment that doesn't fit as a grid (too many/too-wide
 * columns for REN_LINE_WIDTH) -- each row collapses to stacked
 * label:value lines using `header` as column labels when given, same
 * idea as a "responsive" table collapsing to key/value pairs on a
 * narrow screen. */
static int render_stacked(RenPage *rp, TRow *rows, int start, int end, int ncols,
                           char header[REN_MAX_TABLE_COLS][REN_CELL_MAX]) {
    int r, c;
    for (r = start; r < end; r++) {
        if (header) {
            for (c = 0; c < ncols; c++) if (!add_labelvalue(rp, header[c], rows[r].cells[c])) return 0;
        } else {
            char buf[REN_CELL_MAX * 4];
            buf[0] = '\0';
            for (c = 0; c < ncols; c++) { if (c) strcat(buf, "  |  "); strcat(buf, rows[r].cells[c]); }
            if (!wrap_paragraph(rp, buf)) return 0;
        }
        if (!add_line(rp, "", 0)) return 0;
    }
    return 1;
}

/* Splits each row of an even, no-header ncols-wide segment (e.g. Orc's
 * Attributes table: "Strength:"/"90"/"Intelligence:"/"40" all on one row)
 * into ncols/2 independent label:value pairs, then grids THOSE as a
 * plain 2-column box -- reads more naturally than one wide row mixing
 * several unrelated label:value pairs side by side. */
static int render_packed_pairs(RenPage *rp, TRow *rows, int start, int end, int ncols) {
    int pairsPerRow = ncols / 2;
    int total = (end - start) * pairsPerRow;
    TRow *flat;
    int r, c, n = 0;
    int result;

    if (total > REN_MAX_TABLE_ROWS) total = REN_MAX_TABLE_ROWS;
    flat = (TRow *)malloc((size_t)total * sizeof(TRow));
    if (!flat) return 0;

    for (r = start; r < end && n < total; r++) {
        for (c = 0; c + 1 < ncols && n < total; c += 2) {
            flat[n].isHeader = 0;
            flat[n].ncells = 2;
            strcpy(flat[n].cells[0], rows[r].cells[c]);
            strcpy(flat[n].cells[1], rows[r].cells[c + 1]);
            n++;
        }
    }
    result = render_grid(rp, flat, 0, n, 2, NULL);
    if (result == 0) result = render_stacked(rp, flat, 0, n, 2, NULL);
    free(flat);
    return result >= 0;
}

/* Renders one shape-uniform run of data rows (a "segment") the most
 * readable way for its column count/header situation, falling back to
 * stacked rows whenever a grid genuinely wouldn't fit. */
/* True only when every even-indexed cell (0, 2, 4, ...) across the whole
 * segment looks like a label ("Strength:", "HP:", ...) -- the real signal
 * that a row packs several independent label:value pairs together (Orc's
 * Attributes table), as opposed to a wide row of plain multi-column data
 * with no labels at all (e.g. one raw number per shop type) that merely
 * happens to have an even cell count. Without this check the latter gets
 * confidently split into meaningless "pairs" instead of falling back to
 * an honest unlabeled join. */
static int looks_like_packed_pairs(TRow *rows, int start, int end, int ncols) {
    int r, c;
    for (r = start; r < end; r++) {
        for (c = 0; c < ncols; c += 2) {
            size_t len = strlen(rows[r].cells[c]);
            if (len == 0 || rows[r].cells[c][len - 1] != ':') return 0;
        }
    }
    return 1;
}

static int render_segment(RenPage *rp, TRow *rows, int start, int end, int ncols,
                           char header[REN_MAX_TABLE_COLS][REN_CELL_MAX], int haveHeader) {
    if (ncols >= 4 && ncols % 2 == 0 && !haveHeader && looks_like_packed_pairs(rows, start, end, ncols)) {
        return render_packed_pairs(rp, rows, start, end, ncols);
    }
    {
        int fit = render_grid(rp, rows, start, end, ncols, haveHeader ? header : NULL);
        if (fit == 1) return 1;
        if (fit < 0) return 0; /* real allocation/line failure */
        return render_stacked(rp, rows, start, end, ncols, haveHeader ? header : NULL);
    }
}

static int render_table(RenPage *rp, const char *line) {
    TRow *rows = (TRow *)malloc(REN_MAX_TABLE_ROWS * sizeof(TRow));
    /* Two-deep header stack: hPrev is the broad "group span" header row
     * (e.g. "Location Name"/"Guild Locations"/"Merchant Locations"), hCur
     * is the fine-grained one right before the data (e.g. "Dark
     * Brotherhood"/"Fighters Guild"/...). A single header row before data
     * only ever populates hCur. */
    char hPrev[REN_MAX_TABLE_COLS][REN_CELL_MAX]; int hPrevNcols = 0;
    char hCur[REN_MAX_TABLE_COLS][REN_CELL_MAX]; int hCurNcols = 0;
    char header[REN_MAX_TABLE_COLS][REN_CELL_MAX];
    int n = 0, i;
    int ok = 1;

    if (!rows) return 0;
    parse_table_rows(line, rows, &n);

    i = 0;
    while (i < n && ok) {
        if (rows[i].isHeader && rows[i].ncells > 1) {
            int c, nc = rows[i].ncells < REN_MAX_TABLE_COLS ? rows[i].ncells : REN_MAX_TABLE_COLS;
            memcpy(hPrev, hCur, sizeof(hPrev)); hPrevNcols = hCurNcols;
            for (c = 0; c < nc; c++) strcpy(hCur[c], rows[i].cells[c]);
            hCurNcols = nc;
            i++;
            continue;
        }
        if (rows[i].isHeader && rows[i].ncells == 1) {
            ok = add_heading(rp, rows[i].cells[0]);
            i++;
            continue;
        }
        if (!rows[i].isHeader && rows[i].ncells == 1) {
            ok = wrap_paragraph(rp, rows[i].cells[0]);
            i++;
            continue;
        }
        {
            int ncols = rows[i].ncells;
            int segStart = i, segEnd = i;
            int useHeader = 0;
            while (segEnd < n && !rows[segEnd].isHeader && rows[segEnd].ncells == ncols) segEnd++;

            if (hCurNcols == ncols) {
                /* Single header row, directly matching -- the common case. */
                memcpy(header, hCur, sizeof(header));
                useHeader = 1;
            } else if (hPrevNcols >= 1 && hCurNcols + 1 == ncols && hCurNcols < REN_MAX_TABLE_COLS) {
                /* Two consecutive multi-cell header rows, and the data
                 * has exactly one more column than the closer one --
                 * a broad group-span header followed by its fine-grained
                 * sub-columns, with a rowspan'd leading column (e.g. a
                 * town/location name) that only the FAR header names.
                 * Real MediaWiki convention: that leading column's own
                 * label lives in hPrev's first cell; hPrev's other cells
                 * are colspan group headers that don't carry forward as
                 * columns of their own. Confirmed against real data
                 * before trusting this generally (Alik'r's Service
                 * Locations table: hPrev=["Location Name","Guild
                 * Locations",...], hCur has 18 shop-type labels, data
                 * rows have 19 cells = town name + 18 stats). */
                strcpy(header[0], hPrev[0]);
                memcpy(header + 1, hCur, sizeof(hCur[0]) * (size_t)hCurNcols);
                useHeader = 1;
            }
            /* Anything else (mismatched counts with no clean merge) stays
             * unlabeled rather than risk a confidently wrong pairing. */

            ok = render_segment(rp, rows, segStart, segEnd, ncols, header, useHeader);
            hPrevNcols = 0; hCurNcols = 0; /* headers apply to exactly the one segment right after them */
            i = segEnd;
        }
    }

    free(rows);
    return ok;
}

/* -------------------------------------------------------------- infobox */

static int render_infobox(RenPage *rp, const DagPage *page) {
    char border[REN_LINE_WIDTH + 8];
    char line[REN_LINE_BUF];
    int contentWidth = REN_LINE_WIDTH - 4; /* "+ " ... " +" */
    int i;

    memset(border, '-', (size_t)contentWidth + 2);
    border[contentWidth + 2] = '\0';
    sprintf(line, "+%s+", border);
    if (!add_line(rp, line, 0)) return 0;

    sprintf(line, "| %-*.*s |", contentWidth, contentWidth, page->infoboxHeading);
    if (!add_line(rp, line, 1)) return 0;
    sprintf(line, "+%s+", border);
    if (!add_line(rp, line, 0)) return 0;

    for (i = 0; i < page->infoboxRowCount; i++) {
        char plain[REN_CELL_MAX];
        char content[REN_CELL_MAX * 2]; /* must hold "pre" (<=24 bytes) + rowbuf (declared up to REN_CELL_MAX+32) safely */
        plain_text(page->infoboxRows[i].value, plain, sizeof(plain));
        sprintf(content, "%-16.16s %s", page->infoboxRows[i].label, plain);
        if ((int)strlen(content) <= contentWidth) {
            sprintf(line, "| %-*.*s |", contentWidth, contentWidth, content);
            if (!add_line(rp, line, 0)) return 0;
        } else {
            /* wrap long values with a hanging indent under the value column */
            Token tokens[300];
            char valbuf[REN_CELL_MAX + 32];
            int ntok, t, col = 0;
            char rowbuf[REN_CELL_MAX + 64];
            int first = 1;
            sprintf(valbuf, "%s", plain);
            ntok = tokenize(valbuf, tokens, 300);
            rowbuf[0] = '\0'; col = 0;
            for (t = 0; t < ntok; t++) {
                int w = tokens[t].len;
                int avail = contentWidth - 17;
                if (col > 0 && col + 1 + w > avail) {
                    char pre[24];
                    sprintf(pre, "%-16.16s ", first ? page->infoboxRows[i].label : "");
                    sprintf(content, "%s%s", pre, rowbuf);
                    sprintf(line, "| %-*.*s |", contentWidth, contentWidth, content);
                    if (!add_line(rp, line, 0)) return 0;
                    first = 0; rowbuf[0] = '\0'; col = 0;
                }
                if (col > 0) { strcat(rowbuf, " "); col++; }
                strncat(rowbuf, tokens[t].text, (size_t)tokens[t].len);
                col += w;
            }
            if (col > 0) {
                char pre[24];
                sprintf(pre, "%-16.16s ", first ? page->infoboxRows[i].label : "");
                sprintf(content, "%s%s", pre, rowbuf);
                sprintf(line, "| %-*.*s |", contentWidth, contentWidth, content);
                if (!add_line(rp, line, 0)) return 0;
            }
        }
    }

    sprintf(line, "+%s+", border);
    if (!add_line(rp, line, 0)) return 0;
    if (!add_line(rp, "", 0)) return 0;
    return 1;
}

/* ---------------------------------------------------------------- body */

static int is_heading_line(const char *lineText, char *out, size_t outsize) {
    size_t len = strlen(lineText);
    size_t i = 0, j = len;
    if (len < 4) return 0;
    while (i < len && lineText[i] == '=') i++;
    if (i < 2) return 0;
    while (j > i && lineText[j - 1] == '=') j--;
    if (j <= i) return 0;
    while (i < j && lineText[i] == ' ') i++;
    while (j > i && lineText[j - 1] == ' ') j--;
    if (j <= i) return 0;
    {
        size_t n = j - i;
        if (n >= outsize) n = outsize - 1;
        memcpy(out, lineText + i, n);
        out[n] = '\0';
    }
    return 1;
}

static int render_body(RenPage *rp, const char *body) {
    char *para = NULL;
    size_t paraCap = 0, paraLen = 0;
    const char *p = body;

    while (1) {
        const char *lineStart = p;
        const char *nl = strchr(p, '\n');
        size_t linelen = nl ? (size_t)(nl - lineStart) : strlen(lineStart);
        char linebuf[4096];
        size_t copyLen = linelen < sizeof(linebuf) - 1 ? linelen : sizeof(linebuf) - 1;
        char headingText[512];

        memcpy(linebuf, lineStart, copyLen);
        linebuf[copyLen] = '\0';

        if (copyLen >= 2 && linebuf[0] == '\x07' && linebuf[1] == 'R') {
            /* Hand-authored raw ASCII-art line: \x07R<text> -- see
             * render_raw_line()'s comment. */
            if (paraLen > 0) { if (!wrap_paragraph(rp, para)) { free(para); return 0; } paraLen = 0; para[0] = '\0'; }
            if (!render_raw_line(rp, linebuf + 2)) { free(para); return 0; }
        } else if (copyLen >= 2 && linebuf[0] == '\x07') {
            /* Hand-authored tree/outline line: \x07<depth digit><text> --
             * see render_tree_line()'s comment. */
            if (paraLen > 0) { if (!wrap_paragraph(rp, para)) { free(para); return 0; } paraLen = 0; para[0] = '\0'; }
            if (!render_tree_line(rp, linebuf[1] - '0', linebuf + 2)) { free(para); return 0; }
        } else if (copyLen == 0) {
            if (paraLen > 0) { if (!wrap_paragraph(rp, para)) { free(para); return 0; } paraLen = 0; para[0] = '\0'; }
            if (!add_line(rp, "", 0)) { free(para); return 0; }
        } else if (linebuf[0] == '\x03') {
            if (paraLen > 0) { if (!wrap_paragraph(rp, para)) { free(para); return 0; } paraLen = 0; para[0] = '\0'; }
            if (!render_table(rp, linebuf)) { free(para); return 0; }
            if (!add_line(rp, "", 0)) { free(para); return 0; }
        } else if (is_heading_line(linebuf, headingText, sizeof(headingText))) {
            if (paraLen > 0) { if (!wrap_paragraph(rp, para)) { free(para); return 0; } paraLen = 0; para[0] = '\0'; }
            if (!add_line(rp, "", 0)) { free(para); return 0; }
            if (!add_heading(rp, headingText)) { free(para); return 0; }
        } else if (linebuf[0] == '-' && linebuf[1] == ' ') {
            if (paraLen > 0) { if (!wrap_paragraph(rp, para)) { free(para); return 0; } paraLen = 0; para[0] = '\0'; }
            if (!wrap_paragraph(rp, linebuf)) { free(para); return 0; }
        } else {
            size_t addLen = copyLen + (paraLen > 0 ? 1 : 0);
            if (paraLen + addLen + 1 > paraCap) {
                size_t newCap = paraCap ? paraCap * 2 : 1024;
                char *np;
                while (newCap < paraLen + addLen + 1) newCap *= 2;
                np = (char *)realloc(para, newCap);
                if (!np) { free(para); return 0; }
                para = np; paraCap = newCap;
            }
            if (paraLen > 0) para[paraLen++] = ' ';
            memcpy(para + paraLen, linebuf, copyLen);
            paraLen += copyLen;
            para[paraLen] = '\0';
        }

        if (!nl) break;
        p = nl + 1;
    }
    if (paraLen > 0) { if (!wrap_paragraph(rp, para)) { free(para); return 0; } }
    free(para);
    return 1;
}

/* --------------------------------------------------------------- entry */

int ren_build(const DagPage *page, RenPage *rp) {
    memset(rp, 0, sizeof(*rp));

    if (page->breadcrumbCount > 0) {
        char bc[DAG_FIELD_MAX * DAG_MAX_BREADCRUMB];
        int i;
        bc[0] = '\0';
        for (i = 0; i < page->breadcrumbCount; i++) {
            if (i) strcat(bc, " > ");
            strcat(bc, page->breadcrumb[i]);
        }
        if (!wrap_paragraph(rp, bc)) return 0;
        if (!add_line(rp, "", 0)) return 0;
    }

    if (!add_heading(rp, page->title)) return 0;
    {
        char border[REN_LINE_WIDTH + 1];
        memset(border, '-', REN_LINE_WIDTH);
        border[REN_LINE_WIDTH] = '\0';
        if (!add_line(rp, border, 0)) return 0;
    }
    if (!add_line(rp, "", 0)) return 0;

    if (page->hasInfobox) {
        if (!render_infobox(rp, page)) return 0;
    }

    if (page->journalCount > 0) {
        int i;
        if (!add_heading(rp, "Journal Entries")) return 0;
        if (!add_line(rp, "", 0)) return 0;
        for (i = 0; i < page->journalCount; i++) {
            if (!wrap_paragraph(rp, page->journal[i])) return 0;
            if (!add_line(rp, "", 0)) return 0;
        }
    }

    if (!render_body(rp, page->body)) return 0;

    return 1;
}

void ren_free(RenPage *rp) {
    int i;
    if (!rp) return;
    for (i = 0; i < rp->lineCount; i++) free(rp->lines[i]);
    free(rp->lines);
    free(rp->lineIsHeading);
    free(rp->links);
    memset(rp, 0, sizeof(*rp));
}
