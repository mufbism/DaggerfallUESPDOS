# Content license and attribution

DAGREAD's own code (the DOS reader, the wikitext-to-database build
tooling, the tests) is MIT-licensed -- see [LICENSE](LICENSE).

**The wiki content it displays is a different matter.** Every article in
`dist/UDFP.DAT` is derived from the `Daggerfall:` namespace of
[UESP, The Unofficial Elder Scrolls Pages](https://en.uesp.net/), whose
own terms require reuse under the same license, with attribution and a
link back to both the source article and its edit history:
<https://en.uesp.net/wiki/UESPWiki:Copyright_and_Ownership>

Accordingly:

- **All Daggerfall content in this repository -- the converted text
  baked into `dist/UDFP.DAT`, and any intermediate wikitext/JSON under
  `raw/` or `converted/` if present in a given checkout -- is licensed
  under [CC BY-SA 2.5](https://creativecommons.org/licenses/by-sa/2.5/),
  the same license UESP itself publishes under.** Redistributing that
  content, converted or not, carries the same obligations UESP asks of
  any reuse: share-alike, attribution, and a link back to the source.
- **Per-article attribution, in the app itself.** Since screen space in
  an 80x25 DOS text display is precious and this corpus has 1,435
  articles, DAGREAD does not print a source line on every page.
  Instead, pressing **A** on any article opens a popup with exactly the
  notice UESP's own copyright page suggests for that specific article:
  its title, a link to the live article, a link to its edit-history
  page (UESP's suggested way to satisfy attribution to individual
  contributors), and the CC BY-SA 2.5 license link. Both URLs are
  rebuilt on the fly from the article's own title -- nothing else is
  stored per page for this. DOS text mode has no clickable links or
  networking; printing the exact address is the standard way an offline
  reuse of BY-SA content satisfies "give access to the page" when there
  is no live hyperlink available.
- Example, for the article titled "Alik'r" (from `src/linkpager.c`'s `A`
  key, reproduced here for one concrete case):

  > This article uses material from the UESP article
  > "Daggerfall:Alik'r": <https://en.uesp.net/wiki/Daggerfall:Alik%27r>.
  > History: <https://en.uesp.net/index.php?title=Daggerfall:Alik%27r&action=history>.
  > Licensed under the [Creative Commons BY-SA 2.5 license](https://creativecommons.org/licenses/by-sa/2.5/).

This project is an unofficial, personal-use offline reader. It is not
affiliated with or endorsed by UESP. See <https://en.uesp.net/> for the
live, always-current wiki.
