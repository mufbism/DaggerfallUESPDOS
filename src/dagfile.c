#include "dagfile.h"
#include <stdlib.h>
#include <string.h>

#define DAG_HEADER_SIZE 24
#define DAG_INDEX_RECORD_SIZE 16

/* Little-endian field readers -- deliberately explicit byte-by-byte rather
 * than casting a buffer to a struct pointer, so this has no dependency on
 * the compiler's struct packing/alignment (the on-disk layout at offset 6
 * within an index record isn't 4-byte aligned, by design -- see
 * tools/build-dat.js). Safe and portable either way. */
static unsigned long rd_u32(const unsigned char *p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}
static unsigned int rd_u16(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static int fread_u32(FILE *fp, unsigned long *out) {
    unsigned char b[4];
    if (fread(b, 1, 4, fp) != 4) return 0;
    *out = rd_u32(b);
    return 1;
}
static int fread_u16(FILE *fp, unsigned int *out) {
    unsigned char b[2];
    if (fread(b, 1, 2, fp) != 2) return 0;
    *out = rd_u16(b);
    return 1;
}
static int fread_u8(FILE *fp, unsigned int *out) {
    int c = fgetc(fp);
    if (c == EOF) return 0;
    *out = (unsigned int)c;
    return 1;
}

/* Reads a length-prefixed string field into a fixed-size buffer, safely
 * truncating (and still consuming the full on-disk field via fseek) if
 * the stored string is longer than bufsize -- should never happen given
 * tools/build-dat.js's sizing, but a corrupt/foreign file must not
 * overflow a caller's buffer. */
static int fread_str16(FILE *fp, char *buf, size_t bufsize) {
    unsigned int len;
    size_t toRead;
    if (!fread_u16(fp, &len)) return 0;
    toRead = (len < bufsize - 1) ? len : bufsize - 1;
    if (toRead > 0 && fread(buf, 1, toRead, fp) != toRead) return 0;
    buf[toRead] = '\0';
    if (toRead < len) fseek(fp, (long)(len - toRead), SEEK_CUR); /* skip truncated remainder */
    return 1;
}

int dag_open(DagFile *df, const char *path) {
    unsigned char header[DAG_HEADER_SIZE];
    unsigned char *rawIndex;
    unsigned long indexOffset, dataOffset, titleBlobOffset;
    unsigned long i;

    memset(df, 0, sizeof(*df));

    df->fp = fopen(path, "rb");
    if (!df->fp) return 0;

    if (fread(header, 1, DAG_HEADER_SIZE, df->fp) != DAG_HEADER_SIZE) { fclose(df->fp); df->fp = NULL; return 0; }
    if (memcmp(header, "UESPDF01", 8) != 0) { fclose(df->fp); df->fp = NULL; return 0; }

    df->pageCount = rd_u32(header + 8);
    indexOffset   = rd_u32(header + 12);
    dataOffset    = rd_u32(header + 16);
    df->homePageId = rd_u32(header + 20);
    titleBlobOffset = indexOffset + df->pageCount * DAG_INDEX_RECORD_SIZE;

    if (df->pageCount == 0) { fclose(df->fp); df->fp = NULL; return 0; }

    df->index = (DagIndexEntry *)malloc(df->pageCount * sizeof(DagIndexEntry));
    if (!df->index) { fclose(df->fp); df->fp = NULL; return 0; }

    rawIndex = (unsigned char *)malloc(df->pageCount * DAG_INDEX_RECORD_SIZE);
    if (!rawIndex) { free(df->index); df->index = NULL; fclose(df->fp); df->fp = NULL; return 0; }

    if (fseek(df->fp, (long)indexOffset, SEEK_SET) != 0 ||
        fread(rawIndex, 1, df->pageCount * DAG_INDEX_RECORD_SIZE, df->fp) != df->pageCount * DAG_INDEX_RECORD_SIZE) {
        free(rawIndex); free(df->index); df->index = NULL; fclose(df->fp); df->fp = NULL; return 0;
    }

    for (i = 0; i < df->pageCount; i++) {
        const unsigned char *rec = rawIndex + i * DAG_INDEX_RECORD_SIZE;
        /* Stored offsets are relative to the title blob / data section
         * respectively (see tools/build-dat.js); resolve to absolute
         * file positions once here so dag_load_page() and dag_get_title()
         * never have to re-derive titleBlobOffset/dataOffset themselves. */
        df->index[i].titleOffset = titleBlobOffset + rd_u32(rec + 0);
        df->index[i].titleLength = (unsigned short)rd_u16(rec + 4);
        df->index[i].dataOffset  = dataOffset + rd_u32(rec + 6);
        df->index[i].dataLength  = rd_u32(rec + 10);
    }
    free(rawIndex);

    /* Title blob's on-disk size is whatever sits between its start and the
     * data section's start -- load it whole into RAM in one read. */
    df->titleBlobSize = dataOffset - titleBlobOffset;
    df->titleBlob = (char *)malloc(df->titleBlobSize ? df->titleBlobSize : 1);
    if (!df->titleBlob) { free(df->index); df->index = NULL; fclose(df->fp); df->fp = NULL; return 0; }
    if (df->titleBlobSize > 0) {
        if (fseek(df->fp, (long)titleBlobOffset, SEEK_SET) != 0 ||
            fread(df->titleBlob, 1, df->titleBlobSize, df->fp) != df->titleBlobSize) {
            free(df->titleBlob); df->titleBlob = NULL;
            free(df->index); df->index = NULL;
            fclose(df->fp); df->fp = NULL;
            return 0;
        }
    }

    return 1;
}

void dag_close(DagFile *df) {
    if (!df) return;
    if (df->fp) fclose(df->fp);
    free(df->index);
    free(df->titleBlob);
    memset(df, 0, sizeof(*df));
}

int dag_get_title(DagFile *df, unsigned long id, char *buf, size_t bufsize) {
    const DagIndexEntry *e;
    unsigned long relOffset;
    size_t len;
    if (id < 1 || id > df->pageCount || bufsize == 0) return 0;
    e = &df->index[id - 1];
    relOffset = e->titleOffset - df->index[0].titleOffset;
    len = e->titleLength;
    if (len > bufsize - 1) len = bufsize - 1;
    memcpy(buf, df->titleBlob + relOffset, len);
    buf[len] = '\0';
    return 1;
}

void dag_free_page(DagPage *page) {
    if (!page) return;
    free(page->body);
    memset(page, 0, sizeof(*page));
}

int dag_load_page(DagFile *df, unsigned long id, DagPage *page) {
    const DagIndexEntry *e;
    unsigned int breadcrumbCount, hasInfobox, rowCount, journalCount;
    unsigned int i;
    unsigned long titleAbsBase;

    memset(page, 0, sizeof(*page));
    if (id < 1 || id > df->pageCount) return 0;
    e = &df->index[id - 1];

    /* Title: copy straight out of the in-RAM blob, no disk read needed. */
    titleAbsBase = df->index[0].titleOffset; /* absolute offset the blob's byte 0 corresponds to */
    {
        unsigned long relOffset = e->titleOffset - titleAbsBase;
        size_t len = e->titleLength;
        if (len > sizeof(page->title) - 1) len = sizeof(page->title) - 1;
        memcpy(page->title, df->titleBlob + relOffset, len);
        page->title[len] = '\0';
    }

    if (fseek(df->fp, (long)e->dataOffset, SEEK_SET) != 0) return 0;

    if (!fread_u8(df->fp, &breadcrumbCount)) return 0;
    page->breadcrumbCount = (int)(breadcrumbCount < DAG_MAX_BREADCRUMB ? breadcrumbCount : DAG_MAX_BREADCRUMB);
    for (i = 0; i < breadcrumbCount; i++) {
        if (i < (unsigned int)DAG_MAX_BREADCRUMB) {
            if (!fread_str16(df->fp, page->breadcrumb[i], DAG_FIELD_MAX)) return 0;
        } else {
            unsigned int skiplen;
            if (!fread_u16(df->fp, &skiplen)) return 0;
            fseek(df->fp, (long)skiplen, SEEK_CUR);
        }
    }

    if (!fread_u8(df->fp, &hasInfobox)) return 0;
    page->hasInfobox = (int)hasInfobox;
    if (hasInfobox) {
        if (!fread_str16(df->fp, page->infoboxHeading, DAG_FIELD_MAX)) return 0;
        if (!fread_u8(df->fp, &rowCount)) return 0;
        page->infoboxRowCount = (int)(rowCount < DAG_MAX_INFOBOX_ROWS ? rowCount : DAG_MAX_INFOBOX_ROWS);
        for (i = 0; i < rowCount; i++) {
            if (i < (unsigned int)DAG_MAX_INFOBOX_ROWS) {
                if (!fread_str16(df->fp, page->infoboxRows[i].label, DAG_FIELD_MAX)) return 0;
                if (!fread_str16(df->fp, page->infoboxRows[i].value, DAG_FIELD_MAX)) return 0;
            } else {
                unsigned int skiplen;
                if (!fread_u16(df->fp, &skiplen)) return 0;
                fseek(df->fp, (long)skiplen, SEEK_CUR);
                if (!fread_u16(df->fp, &skiplen)) return 0;
                fseek(df->fp, (long)skiplen, SEEK_CUR);
            }
        }
    }

    if (!fread_u16(df->fp, &journalCount)) return 0;
    page->journalCount = (int)(journalCount < DAG_MAX_JOURNAL ? journalCount : DAG_MAX_JOURNAL);
    for (i = 0; i < journalCount; i++) {
        if (i < (unsigned int)DAG_MAX_JOURNAL) {
            if (!fread_str16(df->fp, page->journal[i], DAG_JOURNAL_MAX)) return 0;
        } else {
            unsigned int skiplen;
            if (!fread_u16(df->fp, &skiplen)) return 0;
            fseek(df->fp, (long)skiplen, SEEK_CUR);
        }
    }

    if (!fread_u32(df->fp, &page->bodyLength)) return 0;
    page->body = (char *)malloc(page->bodyLength + 1);
    if (!page->body) return 0;
    if (page->bodyLength > 0 && fread(page->body, 1, page->bodyLength, df->fp) != page->bodyLength) {
        free(page->body); page->body = NULL;
        return 0;
    }
    page->body[page->bodyLength] = '\0';

    return 1;
}

/* ---------------------------------------------------------------- search */

static void title_lower(DagFile *df, unsigned long idx, char *buf, size_t bufsize) {
    const DagIndexEntry *e = &df->index[idx];
    unsigned long relOffset = e->titleOffset - df->index[0].titleOffset;
    size_t len = e->titleLength;
    size_t i;
    if (len > bufsize - 1) len = bufsize - 1;
    memcpy(buf, df->titleBlob + relOffset, len);
    buf[len] = '\0';
    for (i = 0; i < len; i++) if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] = (char)(buf[i] + 32);
}

/* Case-insensitive strstr, returns the match offset via *outPos. */
static int ci_contains(const char *hay, const char *needle, int *outPos) {
    size_t hlen = strlen(hay), nlen = strlen(needle);
    size_t i;
    if (nlen == 0 || nlen > hlen) return 0;
    for (i = 0; i + nlen <= hlen; i++) {
        if (strncmp(hay + i, needle, nlen) == 0) { *outPos = (int)i; return 1; }
    }
    return 0;
}

static void sort_results(DagSearchResult *results, int n) {
    int i, j;
    for (i = 1; i < n; i++) {
        DagSearchResult key = results[i];
        j = i - 1;
        while (j >= 0 && results[j].score > key.score) { results[j + 1] = results[j]; j--; }
        results[j + 1] = key;
    }
}

int dag_search(DagFile *df, const char *query, DagSearchResult *results, int maxResults) {
    char qlower[DAG_TITLE_MAX];
    char tlower[DAG_TITLE_MAX];
    unsigned long i;
    int n = 0;
    size_t qi;

    strncpy(qlower, query, sizeof(qlower) - 1);
    qlower[sizeof(qlower) - 1] = '\0';
    for (qi = 0; qlower[qi]; qi++) if (qlower[qi] >= 'A' && qlower[qi] <= 'Z') qlower[qi] = (char)(qlower[qi] + 32);
    if (!qlower[0]) return 0;

    for (i = 0; i < df->pageCount && n < maxResults; i++) {
        int pos;
        title_lower(df, i, tlower, sizeof(tlower));
        if (ci_contains(tlower, qlower, &pos)) {
            results[n].id = i + 1;
            results[n].score = pos * 1000 + (int)strlen(tlower);
            n++;
        }
    }
    sort_results(results, n);
    return n;
}

/* Classic space-optimized Levenshtein distance (two rolling rows, O(min
 * row length) extra memory) -- cheap enough to run against every title
 * for one user-triggered search, even on period hardware. */
static int levenshtein(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    int prev[DAG_TITLE_MAX + 1];
    int cur[DAG_TITLE_MAX + 1];
    size_t i, j;
    if (lb > DAG_TITLE_MAX) lb = DAG_TITLE_MAX;
    for (j = 0; j <= lb; j++) prev[j] = (int)j;
    for (i = 1; i <= la; i++) {
        cur[0] = (int)i;
        for (j = 1; j <= lb; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int del = prev[j] + 1;
            int ins = cur[j - 1] + 1;
            int sub = prev[j - 1] + cost;
            int m = del < ins ? del : ins;
            cur[j] = m < sub ? m : sub;
        }
        memcpy(prev, cur, (lb + 1) * sizeof(int));
    }
    return prev[lb];
}

int dag_fuzzy_suggest(DagFile *df, const char *query, DagSearchResult *results, int maxResults) {
    char qlower[DAG_TITLE_MAX];
    char tlower[DAG_TITLE_MAX];
    unsigned long i;
    int n = 0, worst = -1, worstIdx = -1, k;
    size_t qi;

    strncpy(qlower, query, sizeof(qlower) - 1);
    qlower[sizeof(qlower) - 1] = '\0';
    for (qi = 0; qlower[qi]; qi++) if (qlower[qi] >= 'A' && qlower[qi] <= 'Z') qlower[qi] = (char)(qlower[qi] + 32);
    if (!qlower[0]) return 0;

    for (i = 0; i < df->pageCount; i++) {
        int dist;
        title_lower(df, i, tlower, sizeof(tlower));
        dist = levenshtein(qlower, tlower);
        if (n < maxResults) {
            results[n].id = i + 1;
            results[n].score = dist;
            n++;
            if (n == maxResults) {
                worst = results[0].score; worstIdx = 0;
                for (k = 1; k < n; k++) if (results[k].score > worst) { worst = results[k].score; worstIdx = k; }
            }
        } else if (dist < worst) {
            results[worstIdx].id = i + 1;
            results[worstIdx].score = dist;
            worst = results[0].score; worstIdx = 0;
            for (k = 1; k < n; k++) if (results[k].score > worst) { worst = results[k].score; worstIdx = k; }
        }
    }
    sort_results(results, n);
    return n;
}

unsigned long dag_find_title(DagFile *df, const char *title) {
    unsigned long i;
    char altFirst[DAG_TITLE_MAX];
    size_t tlen = strlen(title);

    if (tlen == 0 || tlen >= sizeof(altFirst)) return 0;
    strcpy(altFirst, title);
    if (altFirst[0] >= 'a' && altFirst[0] <= 'z') altFirst[0] = (char)(altFirst[0] - 32);
    else if (altFirst[0] >= 'A' && altFirst[0] <= 'Z') altFirst[0] = (char)(altFirst[0] + 32);

    for (i = 0; i < df->pageCount; i++) {
        const DagIndexEntry *e = &df->index[i];
        if (e->titleLength != tlen) continue;
        {
            unsigned long relOffset = e->titleOffset - df->index[0].titleOffset;
            const char *t = df->titleBlob + relOffset;
            if (memcmp(t, title, tlen) == 0) return i + 1;
            if (memcmp(t, altFirst, tlen) == 0) return i + 1;
        }
    }
    return 0;
}
