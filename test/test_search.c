#include "../src/dagfile.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

static int has_id(DagSearchResult *r, int n, unsigned long id) {
    int i;
    for (i = 0; i < n; i++) if (r[i].id == id) return 1;
    return 0;
}

int main(int argc, char **argv) {
    DagFile df;
    DagSearchResult results[40];
    int n;
    unsigned long hammerfellId;
    const char *path = argc > 1 ? argv[1] : "../dist/UDFP.DAT";

    CHECK(dag_open(&df, path), "dag_open");
    if (failures) return 1;

    hammerfellId = dag_find_title(&df, "Hammerfell");
    CHECK(hammerfellId != 0, "Hammerfell exists");

    n = dag_search(&df, "hammerfell", results, 40);
    printf("dag_search(\"hammerfell\") -> %d results\n", n);
    CHECK(n >= 1, "lowercase substring search finds something");
    CHECK(has_id(results, n, hammerfellId), "lowercase search finds Hammerfell itself");

    n = dag_search(&df, "HAMMERFELL", results, 40);
    CHECK(has_id(results, n, hammerfellId), "uppercase search still finds Hammerfell (case-insensitive)");

    n = dag_search(&df, "alik", results, 40);
    printf("dag_search(\"alik\") -> %d results: ", n);
    {
        int i; char buf[DAG_TITLE_MAX];
        for (i = 0; i < n && i < 10; i++) { dag_get_title(&df, results[i].id, buf, sizeof(buf)); printf("%s | ", buf); }
        printf("\n");
    }
    CHECK(n >= 2, "\"alik\" substring matches multiple pages (Alik'r, Alik'ra, ...)");

    n = dag_search(&df, "zzzzznotarealtitleatall", results, 40);
    CHECK(n == 0, "nonsense substring query finds nothing");

    n = dag_fuzzy_suggest(&df, "Hamerfell", results, 5);
    printf("dag_fuzzy_suggest(\"Hamerfell\") -> %d results: ", n);
    {
        int i; char buf[DAG_TITLE_MAX];
        for (i = 0; i < n; i++) { dag_get_title(&df, results[i].id, buf, sizeof(buf)); printf("%s(d=%d) ", buf, results[i].score); }
        printf("\n");
    }
    CHECK(n == 5, "fuzzy suggest returns requested count");
    CHECK(results[0].id == hammerfellId, "closest fuzzy match to a 1-letter-off misspelling is Hammerfell");
    CHECK(results[0].score <= 2, "edit distance for a 1-letter-missing misspelling is small");
    {
        int i;
        for (i = 1; i < n; i++) CHECK(results[i].score >= results[i - 1].score, "fuzzy results sorted ascending by distance");
    }

    dag_close(&df);
    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
