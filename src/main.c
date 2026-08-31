/* main.c -- UESP Daggerfall reader entry point. Opens UDFP.DAT, loads
 * pages on demand via dagfile.c, renders each into wrapped lines via
 * render.c, and displays/navigates them via linkpager.c's link-aware
 * pager. History is a simple stack of page ids -- Backspace pops it and
 * reopens that page from the top (not its exact prior scroll position;
 * a deliberate v1 simplification, see linkpager.h). */
#include <pc.h>
#include <stdio.h>
#include "pmwin.h"
#include "dagfile.h"
#include "render.h"
#include "linkpager.h"

#define DAT_PATH "UDFP.DAT"
#define MAX_HISTORY 256

static int show_page(DagFile *df, unsigned long id, int startLine, unsigned long *outFollow) {
    DagPage page;
    RenPage rp;
    int result;

    if (!dag_load_page(df, id, &page)) {
        pm_msgbox("Error", "Could not load that page.");
        return LP_BACK;
    }
    if (!ren_build(&page, &rp)) {
        dag_free_page(&page);
        pm_msgbox("Error", "Out of memory rendering that page.");
        return LP_BACK;
    }

    result = dag_pager(0, 0, 80, 25, "UESP:Daggerfall", page.title, df, &rp, startLine, outFollow);

    ren_free(&rp);
    dag_free_page(&page);
    return result;
}

int main(void) {
    DagFile df;
    unsigned long history[MAX_HISTORY];
    int historyCount = 0;
    unsigned long current;
    int running = 1;

    if (!dag_open(&df, DAT_PATH)) {
        printf("Could not open %s -- make sure it's in the current directory.\n", DAT_PATH);
        return 1;
    }
    if (df.homePageId == 0) {
        printf("%s has no home page recorded.\n", DAT_PATH);
        dag_close(&df);
        return 1;
    }
    current = df.homePageId;

    pm_video_init();
    pm_event_init();
    pm_screen_clear_desktop();

    while (running) {
        unsigned long follow = 0;
        int result = show_page(&df, current, 0, &follow);

        switch (result) {
        case LP_CLOSED:
            running = 0;
            break;
        case LP_FOLLOW:
            if (historyCount < MAX_HISTORY) history[historyCount++] = current;
            current = follow;
            break;
        case LP_BACK:
            if (historyCount > 0) current = history[--historyCount];
            /* else: already at the start of history -- Backspace on the
             * home page just redisplays it, which the loop does anyway. */
            break;
        }
    }

    pm_event_shutdown();
    pm_video_shutdown();
    dag_close(&df);
    return 0;
}
