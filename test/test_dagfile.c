/* Native (non-DOS) test for dagfile.c against the real UDFP.DAT --
 * dagfile.c is plain standard C (stdio/stdlib/string only, no DOS-specific
 * headers), so it compiles and runs natively with no DJGPP cross-compiler
 * and no shim headers needed, same idea as Sefer's catalog_find_verse_line
 * unit tests but even simpler since there's no video/keyboard involved. */
#include "../src/dagfile.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

int main(int argc, char **argv) {
    DagFile df;
    DagPage page;
    unsigned long id;
    const char *path = argc > 1 ? argv[1] : "../dist/UDFP.DAT";

    CHECK(dag_open(&df, path), "dag_open succeeds");
    if (failures) { printf("Cannot continue without an open file.\n"); return 1; }

    printf("pageCount=%lu homePageId=%lu\n", df.pageCount, df.homePageId);
    CHECK(df.pageCount == 1435, "pageCount is 1435");
    CHECK(df.homePageId > 0, "homePageId resolved");

    /* Home page should be titled exactly "Daggerfall". */
    CHECK(dag_load_page(&df, df.homePageId, &page), "load home page");
    printf("home page title = \"%s\"\n", page.title);
    CHECK(strcmp(page.title, "Daggerfall") == 0, "home page title is \"Daggerfall\"");
    dag_free_page(&page);

    /* dag_find_title round-trip. */
    id = dag_find_title(&df, "Alik'r");
    printf("dag_find_title(\"Alik'r\") = %lu\n", id);
    CHECK(id != 0, "found Alik'r by title");

    CHECK(dag_load_page(&df, id, &page), "load Alik'r page");
    printf("title=\"%s\" breadcrumbCount=%d hasInfobox=%d infoboxRowCount=%d bodyLength=%lu\n",
           page.title, page.breadcrumbCount, page.hasInfobox, page.infoboxRowCount, page.bodyLength);
    CHECK(strcmp(page.title, "Alik'r") == 0, "Alik'r title round-trips");
    CHECK(page.hasInfobox == 1, "Alik'r has an infobox");
    CHECK(page.infoboxRowCount == 14, "Alik'r infobox has 14 rows");
    {
        int found = 0, i;
        for (i = 0; i < page.infoboxRowCount; i++) {
            if (strcmp(page.infoboxRows[i].label, "capital") == 0 && strcmp(page.infoboxRows[i].value, "Alik'ra") == 0) found = 1;
        }
        CHECK(found, "infobox row capital=Alik'ra present");
    }
    CHECK(strstr(page.body, "Hammerfell") != NULL, "body mentions Hammerfell");
    CHECK(strchr(page.body, '\x03') != NULL, "body contains an inline table marker");
    dag_free_page(&page);

    /* Journal entries round-trip (previously a real bug: always empty). */
    id = dag_find_title(&df, "Instructions from the Empire");
    CHECK(id != 0, "found Instructions from the Empire");
    CHECK(dag_load_page(&df, id, &page), "load Instructions from the Empire");
    printf("journalCount=%d\n", page.journalCount);
    CHECK(page.journalCount == 2, "Instructions from the Empire has 2 journal entries");
    if (page.journalCount > 0) {
        CHECK(strstr(page.journal[0], "Lysandus") != NULL, "journal entry 0 mentions Lysandus");
    }
    dag_free_page(&page);

    /* dag_get_title cheap-path matches dag_load_page's title for the same id. */
    {
        char buf[DAG_TITLE_MAX];
        CHECK(dag_get_title(&df, id, buf, sizeof(buf)), "dag_get_title succeeds");
        CHECK(strcmp(buf, "Instructions from the Empire") == 0, "dag_get_title matches");
    }

    /* Out-of-range ids must fail cleanly, not crash. */
    CHECK(dag_load_page(&df, 0, &page) == 0, "id 0 rejected");
    CHECK(dag_load_page(&df, df.pageCount + 1, &page) == 0, "id past pageCount rejected");
    CHECK(dag_find_title(&df, "Definitely Not A Real Page Title") == 0, "unknown title returns 0");

    dag_close(&df);

    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
