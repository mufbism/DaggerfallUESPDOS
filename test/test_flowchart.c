#include "../src/dagfile.h"
#include "../src/render.h"
#include <stdio.h>
#include <string.h>
int main(void) {
    DagFile df; DagPage page; RenPage rp;
    unsigned long id;
    int i, printing = 0;
    dag_open(&df, "../dist/UDFP.DAT");
    id = dag_find_title(&df, "Main Quest");
    dag_load_page(&df, id, &page);
    ren_build(&page, &rp);
    for (i = 0; i < rp.lineCount; i++) {
        char clean[400]; int j, k = 0;
        for (j = 0; rp.lines[i][j] && k < 390; j++) {
            unsigned char c = (unsigned char)rp.lines[i][j];
            clean[k++] = (c == REN_LINK_ON) ? '{' : (c == REN_LINK_OFF) ? '}' : (char)c;
        }
        clean[k] = 0;
        if (strstr(clean, "Quest Flowchart")) printing = 1;
        if (printing) printf("%3d: %s\n", i, clean);
        if (printing && strstr(clean, "Optional Quests")) break;
    }
    return 0;
}
