// Generates the Main Quest page's flowchart replacement text -- see
// convert.js's mainQuestTreeText() comment for why this is hand-
// transcribed rather than parsed from the {{Chart}} template, and
// tools/quest-diagram-helper.js for the layout primitives. Tiered
// Part 1/2/3 + Independent Quests structure per user request, modeled
// on a GameFAQs walkthrough's clearer sectioned style rather than one
// large indented tree (an earlier, harder-to-parse attempt).
const H = require('./quest-diagram-helper.js');
const NS = 'Daggerfall:';

// All layout/centering below is computed on PLAIN title text -- linkify()
// substitutes the wikilink wrapper into an already-finished, already-
// padded line afterward. Doing it the other way around (linkify before
// centering) silently breaks every padding calculation, since it would
// measure the long "[[Daggerfall:X|X]]" wrapper's length instead of the
// visible width (found and fixed during review, not guessed up front).
const TITLES = [
  "Morgiah's Wedding", 'Soul of a Lich', 'The Beast', 'Blackmail',
  "The Emperor's Courier", "Barenziah's Book", 'Orcish Emancipation',
  'The Mantella Revealed', "Elysana's Betrayal", 'The Missing Prince',
  'Painting the Truth', "Medora's Freedom", 'The Ancient Watcher',
  'Dust of Restful Death', "Lysandus' Revelation", "Lysandus' Revenge",
  'Totem, Totem, Who Gets the Totem?', 'Journey to Aetherius',
  "Privateer's Hold", 'Instructions from the Empire', 'Concern for Nulfaga',
  "Mynisera's Letters", "Elysana's Robe",
];
// Longest titles first, so e.g. "Lysandus' Revenge" doesn't get partially
// eaten by an earlier accidental match of a shorter unrelated substring.
TITLES.sort((a, b) => b.length - a.length);

function linkify(line) {
  // A row can hold more than one title side by side (e.g. "Soul of a
  // Lich" and "The Beast" on the same Part 1 row) -- replace every match
  // found, not just the first (real bug caught by reviewing actual
  // output: several titles were silently left unlinked because this
  // used to return after its first replacement).
  for (const title of TITLES) {
    const idx = line.indexOf(title);
    if (idx === -1) continue;
    const wrapped = `[[${NS}${title}|${title}]]`;
    line = line.slice(0, idx) + wrapped + line.slice(idx + title.length);
  }
  return line;
}

function raw(line) { return '\x07R' + linkify(line); }
function tree(depth, text) { return '\x07' + depth + linkify(text); }

function generate() {
const out = [];
const heading = (t) => out.push(`==${t}==`);

heading('Quest Flowchart');
out.push('');
out.push("The Main Quest divides into three interconnected story arcs (Parts 1-3 below), plus several independent side quests. A shared quest that more than one arc leads into is shown in full once and referenced as \"(from Part N)\"/\"(continues in Part N)\" elsewhere, rather than repeated.");
out.push('');

// ---------- Part 1 ----------
heading('Part 1');
{
  const widths = [14, 3, 21, 16];
  const c1 = H.centerOf(widths, 1);
  out.push(raw(H.centerAt("Morgiah's Wedding", c1)));
  out.push(raw(H.vbarAt(c1)));
  {
    const chars = H.blankChars(widths);
    const c0 = H.centerOf(widths, 0);
    for (let x = c0; x <= c1; x++) chars[x] = '-';
    chars[c0] = '+'; chars[c1] = '+';
    out.push(raw(H.toStr(chars)));
  }
  for (const l of H.stack(widths, [
    ['Soul of a Lich', '(to Part 3)'],
    [],
    ['The Beast'],
    ['(optional)', 'Blackmail'],
  ])) out.push(raw(l));
  out.push(raw(H.vbarRow(widths, [1, 2, 3])));
  for (const l of H.stack(widths, [
    [], [],
    ["The Emperor's Courier"],
    ['(optional)', "Barenziah's Book"],
  ])) out.push(raw(l));
  out.push(raw(H.vbarRow(widths, [1, 2, 3])));
  const m = H.mergeBridge(widths, [1, 2, 3]);
  out.push(raw(m.line));
  out.push(raw(H.vbarAt(m.dropPos)));
  out.push(raw(H.centerAt('Orcish Emancipation', m.dropPos)));
  out.push(raw(H.centerAt('(continues in Part 3)', m.dropPos)));
  out.push('');
  out.push(raw(H.vbarAt(m.dropPos)));
  out.push(raw(H.centerAt('(optional)', m.dropPos)));
  out.push(raw(H.centerAt('The Mantella Revealed', m.dropPos)));
  out.push(raw(H.vbarAt(m.dropPos)));
  out.push(raw(H.centerAt('(optional)', m.dropPos)));
  out.push(raw(H.centerAt("Elysana's Betrayal", m.dropPos)));
}
out.push('');

// ---------- Part 2 ----------
heading('Part 2');
{
  const widths = [19, 21];
  const mid = Math.round((H.centerOf(widths, 0) + H.centerOf(widths, 1)) / 2);
  out.push(raw(H.centerAt('The Missing Prince', mid)));
  out.push(raw(H.vbarAt(mid)));
  {
    const chars = H.blankChars(widths);
    const c0 = H.centerOf(widths, 0), c1 = H.centerOf(widths, 1);
    for (let x = Math.min(c0, mid); x <= Math.max(c1, mid); x++) chars[x] = '-';
    chars[c0] = '+'; chars[c1] = '+'; chars[mid] = '+';
    out.push(raw(H.toStr(chars)));
  }
  for (const l of H.stack(widths, [
    ['(optional)', 'Painting the Truth'],
    ['(optional)', "Medora's Freedom"],
  ])) out.push(raw(l));
  out.push(raw(H.vbarRow(widths, [0, 1])));
  for (const l of H.stack(widths, [
    ['(optional)', 'The Ancient Watcher'],
    ['Dust of Restful Death'],
  ])) out.push(raw(l));
  out.push(raw(H.vbarRow(widths, [0, 1])));
  const m = H.mergeBridge(widths, [0, 1]);
  out.push(raw(m.line));
  out.push(raw(H.vbarAt(m.dropPos)));
  out.push(raw(H.centerAt("Lysandus' Revelation", m.dropPos)));
  out.push(raw(H.vbarAt(m.dropPos)));
  out.push(raw(H.centerAt("Lysandus' Revenge", m.dropPos)));
  out.push(raw(H.centerAt('(continues in Part 3)', m.dropPos)));
}
out.push('');

// ---------- Part 3 ----------
heading('Part 3');
{
  const widths = [20, 22, 22];
  for (const l of H.stack(widths, [
    ["Lysandus' Revenge", '(from Part 2)'],
    ['Soul of a Lich', '(from Part 1)'],
    ['Orcish Emancipation', '(from Part 1)'],
  ])) out.push(raw(l));
  out.push(raw(H.vbarRow(widths, [0, 1, 2])));
  const m = H.mergeBridge(widths, [0, 1, 2]);
  out.push(raw(m.line));
  out.push(raw(H.vbarAt(m.dropPos)));
  out.push(raw(H.centerAt('Totem, Totem, Who Gets the Totem?', m.dropPos)));
  out.push(raw(H.vbarAt(m.dropPos)));
  out.push(raw(H.centerAt('Journey to Aetherius', m.dropPos)));
}
out.push('');

// ---------- Independent quests ----------
heading('Independent Quests');
out.push(tree(0, "Privateer's Hold"));
out.push(tree(1, '(optional) Instructions from the Empire'));
out.push('');
out.push(tree(0, 'Concern for Nulfaga'));
out.push(tree(1, "(optional) Mynisera's Letters"));
out.push('');
out.push(tree(0, "Elysana's Robe"));

return out.join('\n');
}

module.exports = { generate };

if (require.main === module) {
  const text = generate();
  let maxw = 0, worst = '';
  for (const l of text.split('\n')) {
    if (l[0] !== '\x07' || l[1] !== 'R') continue;
    const vis = l.slice(2).replace(/\[\[[^\]|]*\|([^\]]*)\]\]/g, '$1');
    if (vis.length > maxw) { maxw = vis.length; worst = vis; }
  }
  console.log('max raw-line visible width:', maxw, JSON.stringify(worst));
  for (const l of text.split('\n')) {
    let s = l;
    if (s[0] === '\x07') s = (s[1] === 'R' ? '' : '  '.repeat(Number(s[1])) + '- ') + s.slice(2);
    s = s.replace(/\[\[[^\]|]*\|([^\]]*)\]\]/g, '$1');
    console.log(s);
  }
}
