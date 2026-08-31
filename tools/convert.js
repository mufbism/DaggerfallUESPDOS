// Converts raw/daggerfall-pages.jsonl (MediaWiki wikitext) into a simplified,
// renderer-friendly intermediate format:
//   - breadcrumb (from {{Trail}}/{{Trail2}})
//   - infobox: {heading, rows:[[label,value]]}  (from allow-listed templates
//     and from raw wikitables)
//   - journal: array of narrative strings (from {{Journal Entries}})
//   - body: plain text with `==heading==` markers kept and wikilinks
//     normalized to `[[pageId|Display Text]]` (id = internal slug, only for
//     links that resolve to a page we actually have; everything else
//     becomes plain display text)
//
// Output: converted/pages.jsonl (one JSON object per line) + converted/index.json
// (title -> id map, plus redirect -> id map, for the DOS-side build step later).

const fs = require('fs');
const path = require('path');
const readline = require('readline');

const RAW_DIR = path.join(__dirname, '..', 'raw');
const OUT_DIR = path.join(__dirname, '..', 'converted');
fs.mkdirSync(OUT_DIR, { recursive: true });

const NS_PREFIX = 'Daggerfall:';

// ---------------------------------------------------------------------------
// Template classification
// ---------------------------------------------------------------------------

// Rendered as a boxed fact-sheet (label/value rows).
const INFOBOX_TEMPLATES = new Set([
  'quest header', 'npc summary', 'daggerfall region summary',
  'daggerfall city summary', 'game book', 'quest footer', 'artifact summary',
  'daggerfall services summary',
]);

// Fields we don't bother showing even inside an allow-listed infobox --
// wiki-internal bookkeeping (image filenames, template ids) with no value
// to a reader who has no images anyway.
const INFOBOX_SKIP_FIELDS = new Set([
  'image', 'imgdesc', 'id', 'icon', 'lorelink', 'notrail',
]);

// Friendlier labels for a few raw param names.
const FIELD_LABELS = {
  reqrep: 'req. reputation', reqlevel: 'req. level', rep: 'reputation',
  sgroup: 'social group', ggroup: 'guild group', parentfaction: 'faction',
  loc: 'location', open: 'hours',
};

let unknownTemplateCounts = {}; // for a post-run report

// Inline-table wire format: \x03, then per row a 1-byte 'H'/'D' flag
// followed by cells joined/terminated with \x1f, each row ending in \x1e,
// then \x04. Plain byte-scanning in C, no JSON parser needed on the DOS
// side. `rows` is an array of {header: bool, cells: string[]}.
function serializeTable(rows) {
  let out = '\x03';
  for (const r of rows) {
    out += r.header ? 'H' : 'D';
    for (const c of r.cells) out += String(c).replace(/[\x1e\x1f\x03\x04]/g, ' ') + '\x1f';
    out += '\x1e';
  }
  return out + '\x04';
}

// ---------------------------------------------------------------------------
// Small wikitext-scanning helpers
// ---------------------------------------------------------------------------

// Find the index just past the matching '}}' for a '{{' at text[start].
// Tracks {{ }} nesting depth. Returns -1 if unterminated.
function findTemplateEnd(text, start) {
  let depth = 0;
  for (let i = start; i < text.length - 1; i++) {
    if (text[i] === '{' && text[i + 1] === '{') { depth++; i++; }
    else if (text[i] === '}' && text[i + 1] === '}') { depth--; i++; if (depth === 0) return i + 1; }
  }
  return -1;
}

// Split a template's inner content into [name, params[]] where each param is
// either {pos:n} or {key, value}. Splits on top-level '|' only (depth-aware
// over {{ }} and [[ ]]).
function parseTemplateInner(inner) {
  const parts = [];
  let depth = 0, cur = '', i = 0;
  for (; i < inner.length; i++) {
    const two = inner.slice(i, i + 2);
    if (two === '{{' || two === '[[') { depth++; cur += two; i++; continue; }
    if (two === '}}' || two === ']]') { depth--; cur += two; i++; continue; }
    if (inner[i] === '|' && depth === 0) { parts.push(cur); cur = ''; continue; }
    cur += inner[i];
  }
  parts.push(cur);
  const name = parts.shift().trim();
  let posIdx = 1;
  const params = parts.map((p) => {
    const eq = findTopLevelEquals(p);
    if (eq === -1) return { pos: posIdx++, value: p.trim() };
    return { key: p.slice(0, eq).trim().toLowerCase(), value: p.slice(eq + 1).trim() };
  });
  return { name, params };
}

function findTopLevelEquals(str) {
  let depth = 0;
  for (let i = 0; i < str.length; i++) {
    const two = str.slice(i, i + 2);
    if (two === '{{' || two === '[[') { depth++; i++; continue; }
    if (two === '}}' || two === ']]') { depth--; i++; continue; }
    if (str[i] === '=' && depth === 0) return i;
  }
  return -1;
}

// Strip {{{param|default}}} / {{{param}}} placeholders (transclusion
// params we have no real value for) -> default, or empty.
function stripTripleBrace(text) {
  for (let pass = 0; pass < 3; pass++) {
    const before = text;
    text = text.replace(/\{\{\{([^{}|]*)\|([^{}]*)\}\}\}/g, '$2');
    text = text.replace(/\{\{\{([^{}]*)\}\}\}/g, '');
    if (text === before) break;
  }
  return text;
}

// Split `str` on every top-level occurrence of `sep` (a 2-char sequence
// like '||' or '!!'), never splitting inside [[ ]] or {{ }} nesting -- so a
// wikilink's own internal '|' can't be mistaken for a cell separator.
function splitTopLevel(str, sep) {
  const parts = [];
  let depth = 0, cur = '', i = 0;
  while (i < str.length) {
    const two = str.slice(i, i + 2);
    if (two === '{{' || two === '[[') { depth++; cur += two; i += 2; continue; }
    if (two === '}}' || two === ']]') { depth--; cur += two; i += 2; continue; }
    if (depth === 0 && str.slice(i, i + sep.length) === sep) { parts.push(cur); cur = ''; i += sep.length; continue; }
    cur += str[i]; i++;
  }
  parts.push(cur);
  return parts;
}

// A wikitable cell: strip a leading "attr |" prefix (only when that '|' is
// a genuine attribute separator, i.e. an '=' appears before it), strip
// image embeds and wikilinks (depth-safe, so "text ]] leftovers" can't
// happen), collapse bold/italic markup, and drop empty results.
function cleanTableCell(raw) {
  let line = raw;
  const attrBar = splitTopLevel(line, '|');
  if (attrBar.length > 1 && findTopLevelEquals(attrBar[0]) !== -1) {
    line = attrBar.slice(1).join('|');
  } else if (line[0] === '|') {
    // A bare leading '|' with nothing (or nothing but a now-stripped
    // template, e.g. "!{{AL|L}}|") before it -- an empty attribute
    // segment, not real content. Strip just the one leading pipe rather
    // than requiring a real "key=value" match.
    line = line.slice(1);
  }
  line = line.replace(/\[\[File:[^\]]*\]\]/gi, '').replace(/\[\[Image:[^\]]*\]\]/gi, '');
  line = line.replace(/\[\[([^\]|]*)\|?([^\]]*)\]\]/g, (_, target, display) => display || target);
  line = line.replace(/<br\s*\/?>/gi, ' ');
  line = cleanValue(line);
  return line || null;
}

function cleanValue(v) {
  return v.replace(/'''/g, '').replace(/''/g, '').trim();
}

function fieldLabel(key) {
  if (FIELD_LABELS[key]) return FIELD_LABELS[key];
  return key.replace(/_/g, ' ');
}

// ---------------------------------------------------------------------------
// Main per-page conversion
// ---------------------------------------------------------------------------

// The Main Quest page's {{Chart}} flowchart is a real DAG (several quests
// have 2-3 prerequisite paths) drawn via a grid of connector glyphs whose
// column-alignment to the box rows above/below isn't reliably
// reconstructable from the raw wikitext alone (confirmed empirically: the
// documented "each box is 3 tiles wide" rule holds for one pair of
// adjacent rows in this exact page but not the next, meaning the amount
// of blank padding an editor chose to write isn't a fixed formula). Rather
// than guess and risk drawing a line to the wrong box, this is hand-
// transcribed from the real rendered diagram (user-supplied screenshot of
// https://en.uesp.net/wiki/Daggerfall:Main_Quest) instead -- exact same
// quests and prerequisite relationships, verified node-for-node and
// edge-for-edge against the image. Generated by tools/main-quest-diagram.js
// as a tiered Part 1/2/3 + Independent Quests layout (GameFAQs-walkthrough
// style, user-requested after the first attempt below read as "too big a
// tree to parse"). "(optional)" marks a dashed line in the original, per
// its own Key legend; a node with more than one parent is shown in full
// once and referenced as "(from Part N)"/"(continues in Part N)" elsewhere.
function mainQuestTreeText() {
  return require('./main-quest-diagram.js').generate();
}

// Kept for reference/fallback only, not called -- the very first attempt,
// one big indented DAG-as-tree instead of tiered sections. See comment
// above.
function mainQuestTreeTextFallback() {
  const L = (title, extra) => `[[${NS_PREFIX}${title}|${title}]]${extra || ''}`;
  const line = (depth, text) => `\x07${depth}${text}`;
  return [
    line(0, L('Privateer\'s Hold')),
    line(1, `(optional) ${L('Instructions from the Empire')}`),
    '',
    line(0, L('The Missing Prince')),
    line(1, `(optional) ${L('Medora\'s Freedom')}`),
    line(2, L('Dust of Restful Death')),
    line(3, L('Lysandus\' Revelation')),
    line(4, L('Lysandus\' Revenge')),
    line(5, L('Totem, Totem, Who Gets the Totem?')),
    line(6, L('Journey to Aetherius')),
    line(1, `(optional) ${L('Painting the Truth')}`),
    line(2, `(optional) ${L('The Ancient Watcher')}`),
    line(3, L('Lysandus\' Revelation', ' (see above)')),
    '',
    line(0, L('Morgiah\'s Wedding')),
    line(1, L('Soul of a Lich')),
    line(2, L('Totem, Totem, Who Gets the Totem?', ' (see above)')),
    line(1, L('Orcish Emancipation')),
    line(2, L('Totem, Totem, Who Gets the Totem?', ' (see above)')),
    line(2, `(optional) ${L('The Mantella Revealed')}`),
    line(3, `(optional) ${L('Elysana\'s Betrayal')}`),
    '',
    line(0, L('The Beast')),
    line(1, L('The Emperor\'s Courier')),
    line(2, L('Orcish Emancipation', ' (see above)')),
    '',
    line(0, L('Blackmail')),
    line(1, `(optional) ${L('Barenziah\'s Book')}`),
    line(2, L('Orcish Emancipation', ' (see above)')),
    '',
    line(0, L('Concern for Nulfaga')),
    line(1, `(optional) ${L('Mynisera\'s Letters')}`),
    '',
    line(0, L('Elysana\'s Robe')),
  ].join('\n');
}

function convertPage(title, rawText, resolveLink, rawByTitle) {
  let text = rawText;
  const cleanTitle = title.startsWith(NS_PREFIX) ? title.slice(NS_PREFIX.length) : title;

  if (cleanTitle === 'Main Quest') text = text.replace(/\{\{Chart\/start\}\}[\s\S]*?\{\{Chart\/end\}\}/i, mainQuestTreeText());

  text = stripTripleBrace(text);

  let breadcrumb = [];
  let infobox = null;
  let journal = null;
  const expandedTransclusions = new Set(); // guards against transclusion loops

  // Repeatedly scan for top-level templates so we can special-case some and
  // strip the rest, innermost calls first would be ideal but in this corpus
  // the templates we care about are always flat/top-level, so a single
  // left-to-right sweep with re-scan after each removal is sufficient.
  let guard = 0;
  while (guard++ < 4000) {
    const start = text.indexOf('{{');
    if (start === -1) break;
    const end = findTemplateEnd(text, start);
    if (end === -1) { text = text.slice(0, start) + text.slice(start + 2); continue; } // unterminated, drop the brace and retry
    const inner = text.slice(start + 2, end - 2);
    const { name, params } = parseTemplateInner(inner);
    const lname = name.toLowerCase();
    let replacement = '';

    if (lname === 'trail' || lname === 'trail2') {
      for (const p of params) if (p.pos !== undefined) breadcrumb.push(cleanValue(p.value));
    } else if (lname === 'pagename') {
      replacement = cleanTitle;
    } else if (lname === 'namespace') {
      // Every page in this corpus is in the Daggerfall: namespace, so
      // this magic word always evaluates to the same literal string --
      // matters because it sometimes builds a wikilink target, e.g.
      // "[[{{NAMESPACE}}:X|X]]"; dropping it left ":X" (no namespace
      // prefix), which resolveLink() correctly declines to follow but
      // meant a real in-corpus link rendered as inert plain text instead.
      replacement = 'Daggerfall';
    } else if (lname === '#if:') {
      // {{#if:{{{failonly|}}}||real content}}: this corpus's transclusion
      // (see rawByTitle splicing above) never supplies named-parameter
      // overrides, so by the time this is reached {{{failonly|}}} has
      // already resolved to "" via stripTripleBrace -- the condition is
      // always empty/false in every real case here, so the "else" branch
      // (2nd positional) is always the semantically correct pick, same as
      // real #if: semantics for a false condition. Verified across the
      // real corpus: 3 of 4 sampled usages are exactly this "Reputation
      // Gain/Loss" pattern -- unlocking real reputation-effect wikitables
      // that were previously vanishing whole on every quest page that
      // transcludes one of the "Daggerfall:Reputation X" pages. A false
      // #if: with no else branch at all correctly falls through to "" here.
      const pos = params.filter((p) => p.pos !== undefined);
      replacement = pos.length >= 2 ? cleanValue(pos[1].value) : '';
    } else if (lname === 'intnote') {
      // {{intnote|nb1|[nb1]}}: an inline "see footnote nb1" marker whose
      // actual text is defined elsewhere on the same page via a matching
      // {{LNote|nb1|...|actual text}} (confirmed: same page, same id, in
      // the corpus) -- rendering the marker itself (2nd positional) keeps
      // the "there's a note here" signal even though we don't have a real
      // jump-to-footnote mechanism.
      const pos = params.filter((p) => p.pos !== undefined);
      replacement = pos.length >= 2 ? cleanValue(pos[1].value) : '';
    } else if (lname === 'mod header') {
      // {{Mod Header|CompUSA Special Edition}}: flags that the quest
      // content immediately following is specific to a particular release.
      const pos = params.filter((p) => p.pos !== undefined);
      if (pos.length) replacement = `(${cleanValue(pos[0].value)} version)`;
    } else if (lname === 'quest link') {
      // {{Quest Link|quest=X}} (keyed) and {{Quest Link|X}} (positional)
      // are BOTH real, real usages -- the positional form turned out to
      // be the dominant one (494 uses across 65 pages vs. 29 keyed),
      // missed originally because the one sample checked happened to be
      // keyed. Every "Related Quests" bullet list on a location page
      // uses the positional form, so this was silently blanking every
      // quest name in every one of those lists.
      const q = params.find((p) => p.key === 'quest') || params.find((p) => p.pos !== undefined);
      if (q) replacement = `[[${NS_PREFIX}${q.value}|${q.value}]]`;
    } else if (lname === 'journal entries') {
      journal = journal || [];
      // Wikitext shape is "|0||entry text|1||entry text|...": with no '='
      // anywhere, every one of these is a plain MediaWiki *positional*
      // param (not a keyed "0=" param as the number might suggest), in
      // groups of 3 -- an entry index, a deliberately-blank field, then
      // the actual journal text as the 3rd of each group.
      const pos = params.filter((p) => p.pos !== undefined);
      for (let i = 2; i < pos.length; i += 3) journal.push(cleanValue(pos[i].value));
    } else if (INFOBOX_TEMPLATES.has(lname)) {
      const rows = [];
      for (const p of params) {
        if (p.key === undefined) continue;
        if (INFOBOX_SKIP_FIELDS.has(p.key)) continue;
        const val = cleanValue(p.value);
        if (!val) continue;
        rows.push([fieldLabel(p.key), val]);
      }
      // Only the page's FIRST infobox template becomes its lead
      // fact-sheet. A page that lists several items each with their own
      // infobox (e.g. "Artifacts", listing dozens of {{Artifact Summary}}
      // blocks; or a compendium hub transcluding many full articles, each
      // with its own Quest Header/NPC Summary) would otherwise have every
      // one of those merge into a single nonsensical mega-table -- later
      // matches instead become their own inline box, right where they
      // appear in the body, same as a raw wikitable would.
      if (rows.length) {
        if (!infobox) {
          infobox = { heading: cleanTitle, rows };
        } else {
          replacement = serializeTable(rows.map((r) => ({ header: false, cells: r })));
        }
      }
    } else if (lname === 'bullet link') {
      const pos = params.filter((p) => p.pos !== undefined);
      if (pos.length) {
        const linkTarget = pos[0].value.trim();
        const blurb = pos[1] ? cleanValue(pos[1].value) : '';
        const resolved = resolveLink(`${NS_PREFIX}${linkTarget}`);
        const shown = cleanValue(linkTarget);
        replacement = resolved ? `[[${resolved}|${shown}]]` : shown;
        if (blurb) replacement += ` - ${blurb}`;
      }
    } else if (lname === 'old link' || lname === 'book normal' || lname === 'year' || lname === 'note') {
      // {{Year|1E 808|three thousand years ago}}, {{Note|Residence||text}}:
      // in each case the real display text is whichever positional arg
      // comes last (Note pads a deliberately-blank middle slot, same
      // "index, blank, text" shape as Journal Entries, just non-repeating).
      const pos = params.filter((p) => p.pos !== undefined);
      replacement = pos.length ? cleanValue(pos[pos.length - 1].value) : '';
    } else if (lname === 'lnote') {
      // {{LNote|nb1|^1|the actual footnote text}}: id, display symbol,
      // text -- keep the symbol as a lead-in (it's the marker a reader
      // would look for) plus the real text, not just the text alone.
      const pos = params.filter((p) => p.pos !== undefined);
      if (pos.length >= 3) replacement = `${cleanValue(pos[1].value)} ${cleanValue(pos[2].value)}`;
      else if (pos.length) replacement = cleanValue(pos[pos.length - 1].value);
    } else if (lname === 'anchor') {
      // {{Anchor|Energy Leech}}, {{Anchor|Strength}}<br>STR, {{Anchor|*|TempleNote}},
      // {{Anchor||FixSave}}{{Old Link|...}}: consistently "first arg is
      // what a reader sees (a word, or a footnote symbol like * or dagger,
      // or nothing at all), every later arg is purely an internal anchor
      // id that never displays" -- opposite convention from Old
      // Link/Book Normal/Year/Note (which use the LAST arg) because an
      // anchor point's own name is metadata, not content. This template
      // is used as an entire table cell's content often enough (e.g. the
      // spell-name column of Destruction Spells) that dropping it
      // entirely -- the previous behavior -- blanked out the cell.
      const pos = params.filter((p) => p.pos !== undefined);
      replacement = pos.length ? cleanValue(pos[0].value) : '';
    } else if (lname === 'chart/start' || lname === 'chart/end') {
      // Container markers for a {{Chart}} flowchart -- no content of
      // their own.
    } else if (lname === 'chart') {
      // {{Chart}} draws an actual ASCII-art flowchart from a grid of
      // 2-letter box codes and connector glyphs (|, :, ~, !, `, -, ., F,
      // J, ...) plus CODE=[[link|text]] definitions for each box, one
      // {{Chart|...}} call per grid row. Real branching reconstruction
      // (mapping every connector glyph to the right box-drawing line) is
      // a small diagramming engine in its own right -- disproportionate
      // for the one page (Main Quest) that uses it. Instead, extract
      // what actually matters: each row's box codes, in left-to-right
      // reading order, resolved to their real link text -- preserves
      // *what quests exist and how they're grouped* even without the
      // connecting lines, rather than showing nothing at all.
      const kv = {};
      for (const p of params) if (p.key !== undefined) kv[p.key] = p.value;
      const seen = new Set();
      const boxes = [];
      for (const p of params) {
        if (p.pos === undefined) continue;
        // parseTemplateInner() lowercases every keyed param's name (kv is
        // keyed that way too), but a positional box-code cell keeps its
        // original case ("PH", not "ph") -- lowercase it here to match.
        const code = p.value.trim().toLowerCase();
        if (code && kv[code] !== undefined && !seen.has(code)) {
          seen.add(code);
          boxes.push(cleanValue(kv[code]).replace(/<br\s*\/?>/gi, ' '));
        }
      }
      if (boxes.length) replacement = boxes.join(', ');
    } else if (lname === 'sic') {
      // {{sic|whereever|wherever}}: preserve the verbatim (mis)quoted
      // original text plus the annotation, not silently "correct" it --
      // this is inside a direct in-game quote, not our own prose.
      const pos = params.filter((p) => p.pos !== undefined);
      if (pos.length) replacement = `${cleanValue(pos[0].value)} [sic]`;
    } else if (lname === 'quote box') {
      const pos = params.filter((p) => p.pos !== undefined);
      if (pos.length) {
        replacement = `"${cleanValue(pos[0].value)}"`;
        if (pos[1]) replacement += ` -- ${cleanValue(pos[1].value)}`;
      }
    } else if (name.slice(0, NS_PREFIX.length).toLowerCase() === NS_PREFIX.toLowerCase() && !params.length) {
      // Explicit cross-page transclusion, e.g. {{Daggerfall:Reputation Mages Guild}}
      // -- splice in that page's own raw wikitext so its content (usually a
      // shared data table) gets parsed inline, same as real MediaWiki would render it.
      const targetTitle = name.slice(NS_PREFIX.length).trim();
      if (!expandedTransclusions.has(targetTitle) && rawByTitle.has(targetTitle)) {
        expandedTransclusions.add(targetTitle);
        replacement = stripTripleBrace(rawByTitle.get(targetTitle));
      } else {
        unknownTemplateCounts[name] = (unknownTemplateCounts[name] || 0) + 1;
      }
    } else {
      unknownTemplateCounts[name] = (unknownTemplateCounts[name] || 0) + 1;
      // drop silently -- layout/navigation noise (Trail-adjacent templates,
      // TOC markers, stub/bug/sic annotations, bare category-footer
      // templates like {{Daggerfall Creatures}}, etc.)
    }

    text = text.slice(0, start) + replacement + text.slice(end);
  }

  // Transclusion-control tags: keep inner text, drop the tags themselves.
  // NOTE: <br> is deliberately NOT converted to a newline yet -- that has
  // to wait until after wikitable extraction below, otherwise a <br> inside
  // a cell's display text (e.g. "Dark<br>Brotherhood") splits that cell
  // across two physical lines and the closing ]] silently gets dropped.
  text = text.replace(/<\/?(onlyinclude|includeonly|cleantable)\s*>/gi, '');
  text = text.replace(/<noinclude>[\s\S]*?<\/noinclude>/gi, '');
  // <gallery>File:a.png\nFile:b.png</gallery> -- bare-filename image list,
  // no visual images on a DOS text screen either way.
  text = text.replace(/<gallery[^>]*>[\s\S]*?<\/gallery>/gi, '');
  text = text.replace(/<!--[\s\S]*?-->/g, '');
  text = text.replace(/<ref[^>]*\/>/gi, '');
  text = text.replace(/<ref[^>]*>[\s\S]*?<\/ref>/gi, '');
  text = text.replace(/<\/?(div|small|span|center)[^>]*>/gi, '');

  // Raw wikitables (not a named infobox template) become their own inline
  // table rendered where they appear in the body -- never merged into the
  // page's lead infobox, and correctly split on same-line '||'/'!!'
  // multi-cell syntax (depth-aware, so a wikilink's own internal '|'
  // doesn't get mistaken for a cell separator).
  text = text.replace(/\{\|[^\n]*\n([\s\S]*?)\n\|\}/g, (_, body) => {
    const rawRowChunks = body.split(/\n(?=\|-)/);
    const rows = [];
    for (const rr of rawRowChunks) {
      // NOTE: blank lines are deliberately KEPT here (not filtered out) --
      // inside a multi-line cell's continuation text (see below) a blank
      // line is a real paragraph break, e.g. between an in-game letter's
      // greeting and its body. Only genuine row-separator/caption lines
      // are excluded.
      const lines = rr.split('\n').filter((l) => !/^\|-/.test(l.trim()) && !/^\|\+/.test(l.trim()));
      let rawPieces = []; // uncleaned strings, one per cell -- continuation lines get appended before cleaning
      let bangCount = 0, barCount = 0;
      for (const rawLine of lines) {
        const line = rawLine.trim();
        if (line[0] !== '|' && line[0] !== '!') {
          // A cell's content can continue on lines that don't start with
          // '|'/'!' at all -- common for a single-cell table used purely
          // to draw a bordered quote box around prose (e.g. an in-game
          // letter), where the actual text uses ':'/'::' wiki-indent
          // styling per line instead of new cell markers. Previously
          // these lines were just skipped, silently dropping the entire
          // letter/message -- append as a continuation of the last cell
          // instead (stripping the indent markers, which don't carry
          // meaningful structure worth preserving on an 80-column screen).
          if (rawPieces.length > 0) {
            const stripped = line.replace(/^:+/, '').trim();
            const last = rawPieces[rawPieces.length - 1];
            // A blank continuation line is a real paragraph break (e.g.
            // the blank line between a letter's greeting and its body) --
            // preserve it as "\n\n" so render.c's wrap_paragraph can
            // treat it as one; a non-blank continuation line is just the
            // source wrapping one long paragraph across several lines
            // for editing convenience, joined with a space instead.
            if (stripped === '') {
              if (last && !last.endsWith('\n\n')) rawPieces[rawPieces.length - 1] = last + '\n\n';
            } else {
              const sep = last && !last.endsWith('\n\n') ? ' ' : '';
              rawPieces[rawPieces.length - 1] = last + sep + stripped;
            }
          }
          continue;
        }
        const header = line[0] === '!';
        if (header) bangCount++; else barCount++;
        // MediaWiki allows both '!!' and '||' as the same-line cell
        // separator on a header row; only '||' is valid on a data row.
        let pieces = splitTopLevel(line.slice(1), header ? '!!' : '||');
        if (header) pieces = pieces.flatMap((p) => splitTopLevel(p, '||'));
        for (const part of pieces) rawPieces.push(part);
      }
      const rawCells = rawPieces.map((raw) => cleanTableCell(raw));
      // A row counts as a true header only if EVERY cell used '!' --
      // real MediaWiki convention also lets a data row bold just its own
      // lead cell with '!' while the rest use plain '|' (e.g.
      // `! style=... |Agogurana` followed by `|15||15||17...` for that
      // town's own stat values) -- a row is data, not a header, whenever
      // any '|' cell appears alongside the '!' one.
      const isHeader = bangCount > 0 && barCount === 0;
      // Keep a genuinely-empty cell as "" rather than dropping it -- in a
      // wide table (e.g. one column per shop type) an empty cell is real
      // data ("no such shop in this town"), and dropping it instead of
      // keeping its place shifts every cell after it into the wrong
      // column. Only a row with NO real content at all (e.g. a
      // decorative image-only row) disappears entirely.
      if (rawCells.some((c) => c !== null)) {
        const cells = rawCells.map((c) => (c === null ? '' : c));
        rows.push({ header: isHeader, cells });
      }
    }
    if (!rows.length) return '';
    return serializeTable(rows);
  });

  // Now safe to turn remaining <br> into real newlines (prose/journal
  // letters use these for deliberate line breaks; all table cells have
  // already been extracted and had their own <br>s flattened to spaces).
  text = text.replace(/<br\s*\/?>/gi, '\n');

  // Images -- no visual images on a DOS text screen; drop.
  text = text.replace(/\[\[(File|Image):[^\]]*\]\]/gi, '');
  text = text.replace(/\[\[Category:[^\]]*\]\]/gi, '');
  // Interlanguage links, e.g. [[fr:Daggerfall:Daggerfall]] -- wiki-internal
  // cross-reference to a foreign-language sister site, no display text of
  // its own, never meant to render inline (real MediaWiki renders these as
  // a sidebar link, not inline prose).
  text = text.replace(/\[\[[a-z]{2,3}(-[a-z]{2,4})?:[^\]|]*\]\]/gi, '');

  // Wikilinks -> internal [[id|Display]] or plain display text.
  text = text.replace(/\[\[([^\]|#]*)(#[^\]|]*)?(\|([^\]]*))?\]\]/g, (_, target, _anchor, _p, display) => {
    const shown = display !== undefined ? display : target;
    const cleanShown = cleanValue(shown) || target;
    if (!target) return cleanShown; // pure anchor link, e.g. [[#Section|text]]
    const resolved = resolveLink(target.trim());
    if (resolved) return `[[${resolved}|${cleanShown}]]`;
    return cleanShown;
  });

  text = text.replace(/__[A-Z]+__/g, '');
  text = text.replace(/'''/g, '').replace(/''/g, '');
  text = text.replace(/^:+/gm, '');
  text = text.replace(/^[#*]+\s?/gm, (m) => '- ');
  text = text.replace(/\n{3,}/g, '\n\n');
  text = text.trim();

  // A handful of compendium hub pages (e.g. "Skills") transclude dozens of
  // full individual articles, each carrying its own {{Trail}} -- dedupe
  // rather than showing the same breadcrumb entry repeated N times.
  breadcrumb = [...new Set(breadcrumb)];

  return { title: cleanTitle, breadcrumb, infobox, journal, body: text };
}

// ---------------------------------------------------------------------------
// Load pages, build redirect/content maps, resolve links, convert, write out
// ---------------------------------------------------------------------------

async function main() {
  const pages = [];
  const rl = readline.createInterface({ input: fs.createReadStream(path.join(RAW_DIR, 'daggerfall-pages.jsonl')) });
  for await (const line of rl) if (line.trim()) pages.push(JSON.parse(line));

  const redirectMap = new Map(); // stripped-title -> stripped-target (1 hop)
  const contentTitles = new Set(); // stripped titles that have real content
  const rawByTitle = new Map(); // stripped-title -> raw wikitext, for transclusion splicing
  for (const p of pages) {
    const t = p.title.startsWith(NS_PREFIX) ? p.title.slice(NS_PREFIX.length) : p.title;
    if (p.redirect) {
      const target = p.redirect.startsWith(NS_PREFIX) ? p.redirect.slice(NS_PREFIX.length) : p.redirect;
      redirectMap.set(t, target);
    } else {
      contentTitles.add(t);
      rawByTitle.set(t, p.text);
    }
  }

  function firstLetterVariants(s) {
    if (!s) return [s];
    return [s, s[0].toUpperCase() + s.slice(1), s[0].toLowerCase() + s.slice(1)];
  }

  function resolveLink(rawTarget) {
    if (!rawTarget.startsWith(NS_PREFIX)) return null; // out-of-corpus namespace (Lore:, Books:, Daggerfall Mod:, bare word, etc.)
    let t = rawTarget.slice(NS_PREFIX.length).trim();
    for (let hop = 0; hop < 5; hop++) {
      let found = null;
      for (const v of firstLetterVariants(t)) if (contentTitles.has(v)) { found = v; break; }
      if (found) return found;
      let redirected = null;
      for (const v of firstLetterVariants(t)) if (redirectMap.has(v)) { redirected = redirectMap.get(v); break; }
      if (!redirected || redirected === t) return null;
      t = redirected;
    }
    return null;
  }

  const outStream = fs.createWriteStream(path.join(OUT_DIR, 'pages.jsonl'));
  const index = {}; // title -> id
  let id = 0;
  let infoboxCount = 0, journalCount = 0;
  for (const p of pages) {
    if (p.redirect) continue;
    const rawTitle = p.title;
    const converted = convertPage(rawTitle, p.text, resolveLink, rawByTitle);
    id++;
    converted.id = id;
    index[converted.title] = id;
    if (converted.infobox) infoboxCount++;
    if (converted.journal) journalCount++;
    outStream.write(JSON.stringify(converted) + '\n');
  }
  outStream.end();

  const redirectIndex = {};
  for (const [from, to] of redirectMap.entries()) {
    if (contentTitles.has(to) && index[to]) redirectIndex[from] = index[to];
  }
  fs.writeFileSync(path.join(OUT_DIR, 'index.json'), JSON.stringify({ titles: index, redirects: redirectIndex }, null, 1));

  const topUnknown = Object.entries(unknownTemplateCounts).sort((a, b) => b[1] - a[1]).slice(0, 40);
  fs.writeFileSync(path.join(OUT_DIR, 'unknown-templates.txt'), topUnknown.map(([n, c]) => `${c}\t${n}`).join('\n'));

  console.log(`Converted ${id} pages. ${infoboxCount} have an infobox/fact-sheet, ${journalCount} have journal entries.`);
  console.log(`Output: ${OUT_DIR}`);
  console.log(`Top dropped/unrecognized templates logged to unknown-templates.txt (${topUnknown.length} distinct names).`);
}

main().catch((e) => { console.error(e); process.exit(1); });
