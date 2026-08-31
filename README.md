# DAGREAD

An offline reader for [UESP](https://en.uesp.net/)'s Daggerfall wiki
content, for real DOS/FreeDOS machines. No installation, no
configuration, no network access ever needed or used on the DOS side --
the entire `Daggerfall:` namespace (quests, lore, NPCs, items, spells,
bestiary, and more -- 1,435 articles) is pre-converted into a single
data file and browsed with a keyboard- and mouse-driven text-mode UI:
Wikipedia-style pages with breadcrumbs, boxed infobox fact-sheets,
tables, and clickable/Tab-cyclable internal links.

Sibling project to Sefer (a similar offline DOS reader for Judaic texts)
-- same native DJGPP C toolkit approach, built independently for this
content.

## Status

Fully built and working, verified both natively (the platform-independent
parts of the pipeline have their own test suites, run with plain `gcc`)
and live in DOSBox and on real hardware.

## Controls

Press **?** at any time inside the reader for the full, current list.
Quick reference:

| Key | Action |
|---|---|
| Tab / Shift+Tab | Move to the next/previous link on the page |
| Enter | Follow the focused link |
| Up/Down/PgUp/PgDn/Home/End | Scroll |
| S or / | Search for a page by title (substring, with "did you mean" fallback) |
| F | Find text on the current page |
| Left / Right | Jump to the next/previous match after an F search |
| A | Show this article's UESP source link, history link, and license |
| Backspace | Go back to the previous page |
| Esc | Quit |
| Mouse (optional) | Click a link to follow it; click the scrollbar to page up/down |

## Content license and attribution

The Daggerfall content this reader displays comes from UESP and is
licensed CC BY-SA 2.5, separately from this repository's own MIT-licensed
code. **See [NOTICE.md](NOTICE.md)** for the full explanation and how the
in-app **A** key satisfies UESP's attribution and link-back requirements
per article.

## Files

```
src/            DJGPP C source for DAGREAD.EXE
  dagfile.c/h     loads pages on demand from dist/UDFP.DAT (pure C, natively testable)
  render.c/h      wikitext-derived page -> word-wrapped display lines + link table (pure C, natively testable)
  linkpager.c/h   the link-aware scrolling pager (search, find-on-page, attribution popup, help)
  pmwin.c/h       text-mode window toolkit (forked from Sefer's)
  mouse.c/h       INT 33h mouse driver support
  main.c          entry point / page history

tools/          Node.js build pipeline (not needed to just run DAGREAD)
  extract-daggerfall.js   filters the UESP XML dump down to the Daggerfall: namespace
  convert.js              wikitext -> structured JSON per page
  build-dat.js            structured JSON -> the single dist/UDFP.DAT file
  main-quest-diagram.js, quest-diagram-helper.js   the Main Quest ASCII flowchart generator

test/           Native (plain gcc) test harnesses for dagfile.c/render.c -- no DJGPP needed
dist/UDFP.DAT   The built database (committed -- see below)
Makefile        Cross-compiles DAGREAD.EXE via DJGPP
```

## Building the real DOS executable

Requires a DJGPP cross-toolchain (`i586-pc-msdosdjgpp-gcc`, e.g. via WSL):

```bash
make
```

Produces `DAGREAD.EXE`. To run it, it needs `dist/UDFP.DAT` (already
committed) and `CWSDPMI.EXE` (a third-party DPMI host, not included here
-- get it from https://sandmann.dosdude1.com/cwsdpmi/ or any DOS
software archive) in the same directory.

## Testing

`dagfile.c` and `render.c` have zero DOS-specific dependencies and can be
tested with a plain native compiler:

```bash
gcc -o test_dagfile test/test_dagfile.c src/dagfile.c -Isrc && ./test_dagfile
gcc -o test_render  test/test_render.c  src/dagfile.c src/render.c -Isrc && ./test_render
gcc -o test_search  test/test_search.c  src/dagfile.c -Isrc && ./test_search
```

## Reproducing the corpus from scratch

`dist/UDFP.DAT` is committed, so this is only needed to rebuild it (e.g.
after a UESP content update):

1. Get a current UESP XML dump (UESP blocks automated/bot fetches of
   their dump pages -- download it yourself in a real browser).
2. `node tools/extract-daggerfall.js` -- filters it down to the
   `Daggerfall:` namespace (`raw/`).
3. `node tools/convert.js` -- wikitext -> structured JSON (`converted/`).
4. `node tools/build-dat.js` -- structured JSON -> `dist/UDFP.DAT`.

## License

DAGREAD's own code is MIT-licensed -- see [LICENSE](LICENSE). The
Daggerfall wiki content it displays is separately licensed -- see
[NOTICE.md](NOTICE.md).
