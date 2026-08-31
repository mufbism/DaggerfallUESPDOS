/* dagfile.h -- loads pages on demand from the single-file UDFP.DAT
 * database (header + fixed-size index fully loaded into RAM at open
 * time; each page's variable-length record read from disk on demand
 * via dag_load_page(), never all at once -- the corpus is several MB,
 * far past what fits in conventional memory alongside everything else).
 * UDFP.DAT is generated once, offline, by tools/build-dat.js from the
 * scraped UESP wiki dump -- no network access happens here or ever on
 * the DOS machine. See tools/build-dat.js for the exact byte layout;
 * this file's field-by-field fread() calls must stay in lockstep with it. */
#ifndef DAGFILE_H
#define DAGFILE_H

#include <stdio.h>

/* Real high-water marks across the actual 1,435-page corpus (computed by
 * converted/pages.jsonl stats, not guessed), each with real margin above
 * the observed maximum -- same convention as Sefer's catalog.h sizing. */
#define DAG_TITLE_MAX      96   /* real max title: 57 bytes */
#define DAG_MAX_BREADCRUMB  6   /* real max: 3, after hub-page dedup */
#define DAG_FIELD_MAX     512   /* real max infobox label/value: 405 bytes */
#define DAG_MAX_INFOBOX_ROWS 40 /* real max: 26 rows ("Order of Arkay") */
#define DAG_MAX_JOURNAL    24   /* real max: 18 entries */
#define DAG_JOURNAL_MAX   896   /* real max journal entry: 650 bytes */

typedef struct {
    unsigned long titleOffset;
    unsigned short titleLength;
    unsigned long dataOffset;
    unsigned long dataLength;
} DagIndexEntry;

typedef struct {
    char label[DAG_FIELD_MAX];
    char value[DAG_FIELD_MAX];
} DagInfoRow;

typedef struct {
    char title[DAG_TITLE_MAX];

    int breadcrumbCount;
    char breadcrumb[DAG_MAX_BREADCRUMB][DAG_FIELD_MAX];

    int hasInfobox;
    char infoboxHeading[DAG_FIELD_MAX];
    int infoboxRowCount;
    DagInfoRow infoboxRows[DAG_MAX_INFOBOX_ROWS];

    int journalCount;
    char journal[DAG_MAX_JOURNAL][DAG_JOURNAL_MAX];

    /* Heap-allocated: bodies range from a few hundred bytes to ~448KB
     * (e.g. "TEXT.RSC"), far past anything sane to reserve per-page as a
     * fixed array. Caller must dag_free_page() when done with a page. */
    char *body;
    unsigned long bodyLength;
} DagPage;

typedef struct {
    FILE *fp;
    unsigned long pageCount;
    unsigned long homePageId;      /* 1-based id of the "Daggerfall" hub page */
    DagIndexEntry *index;          /* pageCount entries, loaded fully into RAM */
    char *titleBlob;               /* all titles concatenated, loaded fully into RAM */
    unsigned long titleBlobSize;
} DagFile;

/* Opens path, loads the header+index+title blob into RAM. Returns 1 on
 * success, 0 on failure (bad magic, truncated file, out of memory, etc). */
int dag_open(DagFile *df, const char *path);
void dag_close(DagFile *df);

/* id is 1-based (1..df->pageCount). Reads and parses just that one page's
 * record from disk. Returns 1 on success, 0 on a bad id or read error --
 * on failure *page is left zeroed, safe to dag_free_page() regardless. */
int dag_load_page(DagFile *df, unsigned long id, DagPage *page);
void dag_free_page(DagPage *page);

/* Copies the id'th page's title into buf (>= DAG_TITLE_MAX bytes) without
 * reading its full body -- for building menus/link labels cheaply.
 * Returns 1 on success, 0 on a bad id. */
int dag_get_title(DagFile *df, unsigned long id, char *buf, size_t bufsize);

/* Case-sensitive-after-first-letter title lookup against the in-RAM title
 * blob (MediaWiki's own case="first-letter" convention), tried both ways.
 * Returns the 1-based page id, or 0 if no page has that title. Linear
 * scan -- fine for the occasional home-page/search lookup, not meant to
 * be called per link (links are pre-resolved to numeric ids at build
 * time specifically so this isn't needed on the hot path). */
unsigned long dag_find_title(DagFile *df, const char *title);

/* ---------------------------------------------------------------- search */

typedef struct {
    unsigned long id;
    int score; /* lower is better; meaning differs between the two functions below */
} DagSearchResult;

/* Case-insensitive substring search across every title (a one-time,
 * user-triggered linear scan over ~1,400 short strings -- cheap even on
 * real period hardware). `score` is the match position (0 = query is a
 * prefix, ranks best) with title length as a tiebreaker baked in, so
 * shorter/earlier matches sort first. Returns the number of results
 * found, up to maxResults, sorted best-first. */
int dag_search(DagFile *df, const char *query, DagSearchResult *results, int maxResults);

/* Levenshtein-distance "did you mean" suggestions for when dag_search
 * finds nothing -- `score` is the edit distance, sorted ascending
 * (closest match first). Returns the number of results found, up to
 * maxResults. */
int dag_fuzzy_suggest(DagFile *df, const char *query, DagSearchResult *results, int maxResults);

#endif
