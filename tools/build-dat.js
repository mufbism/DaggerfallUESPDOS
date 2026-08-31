// Builds the single-file DOS database: UDFP.DAT
// Layout (all multi-byte ints little-endian, written field-by-field --
// no struct packing/alignment assumptions, so the C reader can just
// fread() each field in order):
//
//   HEADER
//     char[8]  magic = "UESPDF01"
//     uint32   pageCount
//     uint32   indexOffset
//     uint32   dataOffset
//     uint32   homePageId       (1-based id of the "Daggerfall" hub page)
//
//   INDEX (pageCount records, fixed 16 bytes each -- direct seek by id)
//     uint32   titleOffset      (into TITLE BLOB)
//     uint16   titleLength
//     uint32   dataOffset       (into DATA SECTION)
//     uint32   dataLength
//     (2 bytes padding to keep the record a round 16 bytes)
//
//   TITLE BLOB               raw title bytes, concatenated, no separators
//
//   DATA SECTION (pageCount records, one per page, variable length)
//     uint8    breadcrumbCount
//       { uint16 len; bytes }  x breadcrumbCount
//     uint8    hasInfobox
//     if hasInfobox:
//       uint16 headingLen; bytes heading
//       uint8  rowCount
//         { uint16 labelLen; bytes; uint16 valueLen; bytes } x rowCount
//     uint16   journalCount
//       { uint16 len; bytes } x journalCount
//     uint32   bodyLength
//       bytes body   (contains "==heading==" lines, "[[id|Display]]"
//                      links already resolved to numeric page ids, and
//                      \x03TABLE:<json>\x04 inline-table markers
//                      (0x01/0x02 deliberately left free -- reserved for
//                      pmwin.c's own PM_BOLD_ON/PM_BOLD_OFF convention)

const fs = require('fs');
const path = require('path');
const readline = require('readline');

const CONVERTED = path.join(__dirname, '..', 'converted');
const DIST_DIR = path.join(__dirname, '..', 'dist');
fs.mkdirSync(DIST_DIR, { recursive: true });

// ---------------------------------------------------------------------------
// CP437 transliteration -- DOS text mode is one byte per glyph, not Unicode.
// Map common accented Latin letters to their direct CP437 codepoint, common
// typographic punctuation to a plain-ASCII fallback, and log anything left.
// ---------------------------------------------------------------------------
const CP437_DIRECT = {
  'Ç': 0x80, 'ü': 0x81, 'é': 0x82, 'â': 0x83, 'ä': 0x84,
  'à': 0x85, 'å': 0x86, 'ç': 0x87, 'ê': 0x88, 'ë': 0x89,
  'è': 0x8a, 'ï': 0x8b, 'î': 0x8c, 'ì': 0x8d, 'Ä': 0x8e,
  'Å': 0x8f, 'É': 0x90, 'æ': 0x91, 'Æ': 0x92, 'ô': 0x93,
  'ö': 0x94, 'ò': 0x95, 'û': 0x96, 'ù': 0x97, 'ÿ': 0x98,
  'Ö': 0x99, 'Ü': 0x9a, 'ñ': 0xa4, 'Ñ': 0xa5, 'á': 0xa0,
  'í': 0xa1, 'ó': 0xa2, 'ú': 0xa3, '¿': 0xa8, '¡': 0xad,
};
const ASCII_FALLBACK = {
  '‘': "'", '’': "'", '‚': "'", '“': '"', '”': '"',
  '„': '"', '–': '-', '—': '-', '…': '...', ' ': ' ',
  '•': '*', '´': "'", '`': "'",
};
const unmappable = new Map();
function toCp437(str) {
  let out = '';
  for (const ch of str) {
    const code = ch.codePointAt(0);
    if (code < 128) { out += ch; continue; }
    if (CP437_DIRECT[ch] !== undefined) { out += String.fromCharCode(CP437_DIRECT[ch]); continue; }
    if (ASCII_FALLBACK[ch] !== undefined) { out += ASCII_FALLBACK[ch]; continue; }
    unmappable.set(ch, (unmappable.get(ch) || 0) + 1);
    out += '?';
  }
  return out;
}
function cp437Buf(str) {
  return Buffer.from(toCp437(str), 'latin1'); // latin1 = identity byte mapping for 0-255
}

// ---------------------------------------------------------------------------

async function main() {
  const { titles, redirects } = JSON.parse(fs.readFileSync(path.join(CONVERTED, 'index.json'), 'utf8'));

  const pages = [];
  const rl = readline.createInterface({ input: fs.createReadStream(path.join(CONVERTED, 'pages.jsonl')) });
  for await (const line of rl) if (line.trim()) pages.push(JSON.parse(line));
  pages.sort((a, b) => a.id - b.id);

  function resolveId(title) {
    if (titles[title]) return titles[title];
    if (redirects[title]) return redirects[title];
    return null;
  }

  // Resolve [[Title|Display]] -> [[id|Display]] in every page's body now
  // that we have the complete title->id map.
  const linkRe = /\[\[([^\]|]*)\|([^\]]*)\]\]/g;
  for (const p of pages) {
    p.body = p.body.replace(linkRe, (whole, target, display) => {
      const id = resolveId(target);
      return id ? `[[${id}|${display}]]` : display;
    });
  }

  const homeId = titles['Daggerfall'] || null;
  if (!homeId) console.warn('WARNING: no "Daggerfall" hub page found for homePageId');

  // --- Serialize each page's DATA record ---
  const dataChunks = [];
  const dataLens = [];
  for (const p of pages) {
    const parts = [];
    const u8 = (n) => parts.push(Buffer.from([n & 0xff]));
    const u16 = (n) => { const b = Buffer.alloc(2); b.writeUInt16LE(n); parts.push(b); };
    const u32 = (n) => { const b = Buffer.alloc(4); b.writeUInt32LE(n); parts.push(b); };
    const str16 = (s) => { const b = cp437Buf(s); u16(b.length); parts.push(b); };

    u8(Math.min(p.breadcrumb.length, 255));
    for (const b of p.breadcrumb.slice(0, 255)) str16(b);

    if (p.infobox && p.infobox.rows.length) {
      u8(1);
      str16(p.infobox.heading);
      u8(Math.min(p.infobox.rows.length, 255));
      for (const [label, value] of p.infobox.rows.slice(0, 255)) { str16(label); str16(value); }
    } else {
      u8(0);
    }

    const journal = p.journal || [];
    u16(Math.min(journal.length, 65535));
    for (const j of journal.slice(0, 65535)) str16(j);

    const bodyBuf = cp437Buf(p.body);
    u32(bodyBuf.length);
    parts.push(bodyBuf);

    const chunk = Buffer.concat(parts);
    dataChunks.push(chunk);
    dataLens.push(chunk.length);
  }

  // --- Build TITLE BLOB + INDEX ---
  const titleBufs = pages.map((p) => cp437Buf(p.title));
  let titleOffsetCursor = 0;
  const titleOffsets = titleBufs.map((b) => { const off = titleOffsetCursor; titleOffsetCursor += b.length; return off; });
  const titleBlob = Buffer.concat(titleBufs);

  let dataOffsetCursor = 0;
  const dataOffsets = dataLens.map((len) => { const off = dataOffsetCursor; dataOffsetCursor += len; return off; });

  const HEADER_SIZE = 8 + 4 + 4 + 4 + 4; // magic + pageCount + indexOffset + dataOffset + homePageId
  const INDEX_RECORD_SIZE = 16;
  const indexOffset = HEADER_SIZE;
  const indexSize = pages.length * INDEX_RECORD_SIZE;
  const titleBlobOffset = indexOffset + indexSize;
  const dataSectionOffset = titleBlobOffset + titleBlob.length;

  const indexBuf = Buffer.alloc(indexSize);
  for (let i = 0; i < pages.length; i++) {
    const off = i * INDEX_RECORD_SIZE;
    indexBuf.writeUInt32LE(titleOffsets[i], off + 0);
    indexBuf.writeUInt16LE(titleBufs[i].length, off + 4);
    indexBuf.writeUInt32LE(dataOffsets[i], off + 6);
    indexBuf.writeUInt32LE(dataLens[i], off + 10);
    indexBuf.writeUInt16LE(0, off + 14); // padding
  }

  const header = Buffer.alloc(HEADER_SIZE);
  header.write('UESPDF01', 0, 'ascii');
  header.writeUInt32LE(pages.length, 8);
  header.writeUInt32LE(indexOffset, 12);
  header.writeUInt32LE(dataSectionOffset, 16);
  header.writeUInt32LE(homeId || 0, 20);

  const outPath = path.join(DIST_DIR, 'UDFP.DAT');
  const out = fs.createWriteStream(outPath);
  out.write(header);
  out.write(indexBuf);
  out.write(titleBlob);
  for (const c of dataChunks) out.write(c);
  out.end();

  await new Promise((res) => out.on('finish', res));

  const stat = fs.statSync(outPath);
  console.log(`Wrote ${outPath}`);
  console.log(`  ${pages.length} pages, ${(stat.size / 1024 / 1024).toFixed(2)} MB total`);
  console.log(`  homePageId = ${homeId}`);
  console.log(`  indexOffset=${indexOffset} titleBlobOffset=${titleBlobOffset} dataSectionOffset=${dataSectionOffset}`);
  if (unmappable.size) {
    console.log(`  ${[...unmappable.values()].reduce((a, b) => a + b, 0)} non-CP437 characters replaced with '?' across ${unmappable.size} distinct chars:`);
    for (const [ch, count] of [...unmappable.entries()].sort((a, b) => b[1] - a[1]).slice(0, 20)) {
      console.log(`    ${count}x  U+${ch.codePointAt(0).toString(16).padStart(4, '0')} (${JSON.stringify(ch)})`);
    }
  }
}

main().catch((e) => { console.error(e); process.exit(1); });
