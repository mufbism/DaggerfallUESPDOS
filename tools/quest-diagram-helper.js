// Helper for hand-authoring the Main Quest tiered ASCII flowchart with
// exact column alignment computed instead of hand-counted. Not part of
// the regular build pipeline -- run standalone to generate/verify text,
// then paste the reviewed result into convert.js's mainQuestTreeText().
const GAP = 3; // spaces between columns

function totalWidth(widths) { return widths.reduce((a, b) => a + b, 0) + GAP * (widths.length - 1); }

function centerOf(widths, i) {
  let pos = 0;
  for (let j = 0; j < i; j++) pos += widths[j] + GAP;
  return pos + Math.floor(widths[i] / 2);
}

function blankChars(widths) { return new Array(totalWidth(widths)).fill(' '); }
function toStr(chars) { return chars.join('').replace(/\s+$/, ''); }

// One row of column labels (text=null -> blank cell, just spacing).
function labelRow(widths, texts) {
  return toStr(widths.map((w, i) => {
    const t = texts[i] || '';
    const pad = w - t.length;
    const left = Math.floor(pad / 2), right = pad - left;
    return ' '.repeat(Math.max(0, left)) + t + ' '.repeat(Math.max(0, right));
  }).join(' '.repeat(GAP)).split(''));
}

// Vertical bar under each column index in `cols`.
function vbarRow(widths, cols) {
  const chars = blankChars(widths);
  for (const i of cols) chars[centerOf(widths, i)] = '|';
  return toStr(chars);
}

// Merge: horizontal bridge spanning column indices in `cols`, with a
// single vertical drop continuing below at the midpoint (or at a given
// target column's center if `dropAtCol` given -- lets the merge land
// under a specific one of the participating columns instead of the
// geometric middle).
function mergeBridge(widths, cols, dropAtCol) {
  const chars = blankChars(widths);
  const positions = cols.map((i) => centerOf(widths, i));
  const lo = Math.min(...positions), hi = Math.max(...positions);
  for (let x = lo; x <= hi; x++) chars[x] = '-';
  for (const p of positions) chars[p] = '+';
  // Default drop point: the middle column's own center when there's an
  // odd number of columns (exact, no rounding) -- using the geometric
  // midpoint of the outer two instead can land 1 character off from the
  // middle column's real center, producing a stray adjacent "++".
  let dropPos;
  if (dropAtCol !== undefined) dropPos = centerOf(widths, dropAtCol);
  else if (cols.length % 2 === 1) dropPos = positions[(cols.length - 1) / 2];
  else dropPos = Math.round((lo + hi) / 2);
  chars[dropPos] = '+';
  return { line: toStr(chars), dropPos };
}

// Fork: mirror of mergeBridge -- one incoming column branches into `cols`.
function forkBridge(widths, fromCol, cols) {
  const chars = blankChars(widths);
  const fromPos = centerOf(widths, fromCol);
  const positions = cols.map((i) => centerOf(widths, i));
  const lo = Math.min(...positions, fromPos), hi = Math.max(...positions, fromPos);
  for (let x = lo; x <= hi; x++) chars[x] = '-';
  for (const p of positions) chars[p] = '+';
  chars[fromPos] = '+';
  return toStr(chars);
}

// A line of text centered under a specific absolute column position
// (used after a merge/fork drop, where the target isn't one of the
// regular fixed-width columns).
function centerAt(text, col) {
  const half = Math.floor(text.length / 2);
  return ' '.repeat(Math.max(0, col - half)) + text;
}
function vbarAt(col) { return ' '.repeat(col) + '|'; }

// Renders several columns' worth of stacked lines side by side as one
// block of rows. `columns` is an array (one per column) of arrays of
// lines for that column, top to bottom -- shorter columns are padded
// with blank lines. Each column's lines are centered within its width.
function stack(widths, columns) {
  const maxRows = Math.max(...columns.map((c) => c.length));
  const out = [];
  for (let r = 0; r < maxRows; r++) {
    out.push(labelRow(widths, columns.map((c) => c[r] || '')));
  }
  return out;
}

module.exports = { totalWidth, centerOf, labelRow, vbarRow, mergeBridge, forkBridge, centerAt, vbarAt, blankChars, toStr, stack, GAP };
