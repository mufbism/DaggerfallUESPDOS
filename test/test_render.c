/* Native test for render.c against real pages loaded via dagfile.c.
 * Pure text logic, no video calls -- runs with plain gcc like
 * test_dagfile.c. */
#include "../src/dagfile.h"
#include "../src/render.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

/* Looks for REN_LINK_ON + needle immediately following, i.e. "is `needle`
 * rendered as an actual link span" -- not a literal '{'/'}' substring
 * (dump() below substitutes those only for human-readable printing; the
 * real line bytes use the REN_LINK_ON/OFF control bytes). */
static int link_contains(const RenPage *rp, const char *needle) {
    int i;
    size_t n = strlen(needle);
    for (i = 0; i < rp->lineCount; i++) {
        const char *p = rp->lines[i];
        while ((p = strchr(p, REN_LINK_ON)) != NULL) {
            if (strncmp(p + 1, needle, n) == 0) return 1;
            p++;
        }
    }
    return 0;
}

static void dump(const RenPage *rp, int maxLines) {
    int i;
    for (i = 0; i < rp->lineCount && i < maxLines; i++) {
        char clean[400]; int j, k = 0;
        for (j = 0; rp->lines[i][j] && k < 390; j++) {
            unsigned char c = (unsigned char)rp->lines[i][j];
            if (c == REN_LINK_ON) clean[k++] = '{';
            else if (c == REN_LINK_OFF) clean[k++] = '}';
            else clean[k++] = (char)c;
        }
        clean[k] = '\0';
        printf("%3d%s: %s\n", i, rp->lineIsHeading[i] ? "*" : " ", clean);
    }
}

static void test_page(DagFile *df, const char *title, void (*extra)(const RenPage *)) {
    unsigned long id = dag_find_title(df, title);
    DagPage page;
    RenPage rp;
    printf("\n=== %s (id=%lu) ===\n", title, id);
    CHECK(id != 0, "found page");
    if (!id) return;
    CHECK(dag_load_page(df, id, &page), "loaded page");
    CHECK(ren_build(&page, &rp), "rendered page");
    printf("lineCount=%d linkCount=%d\n", rp.lineCount, rp.linkCount);
    dump(&rp, 30);
    if (extra) extra(&rp);
    {
        int i, badLine = 0, badLink = 0;
        for (i = 0; i < rp.lineCount; i++) {
            int visible = 0, j;
            for (j = 0; rp.lines[i][j]; j++) {
                unsigned char c = (unsigned char)rp.lines[i][j];
                if (c != REN_LINK_ON && c != REN_LINK_OFF) visible++;
            }
            if (visible > REN_LINE_WIDTH) badLine++;
        }
        for (i = 0; i < rp.linkCount; i++) {
            if (rp.links[i].lineIndex < 0 || rp.links[i].lineIndex >= rp.lineCount) badLink++;
            if (rp.links[i].targetId < 1 || rp.links[i].targetId > df->pageCount) badLink++;
        }
        CHECK(badLine == 0, "no line exceeds REN_LINE_WIDTH visible columns");
        CHECK(badLink == 0, "every link has a valid lineIndex and targetId");
    }
    ren_free(&rp);
    dag_free_page(&page);
}

static void check_alikr(const RenPage *rp) {
    CHECK(link_contains(rp, "Hammerfell"), "Hammerfell rendered as a link span");
    CHECK(rp->linkCount > 5, "Alik'r has several links");
}

int main(int argc, char **argv) {
    DagFile df;
    const char *path = argc > 1 ? argv[1] : "../dist/UDFP.DAT";
    CHECK(dag_open(&df, path), "dag_open");
    if (failures) return 1;

    test_page(&df, "Alik'r", check_alikr);
    test_page(&df, "Orc", NULL);
    test_page(&df, "Business with Vampires", NULL);

    dag_close(&df);
    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
