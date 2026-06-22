// Verify the highlighter works against the real grammar JSON.
import { readFileSync } from 'node:fs';

// Patch the highlighter source to expose the internal function.
const src = readFileSync(new URL('./cubyte-highlight.js', import.meta.url), 'utf8');
const augmented = src.replace(
  'export async function loadCuByteHighlighter()',
  'globalThis.__hwg = highlightWithGrammar;\nexport async function loadCuByteHighlighter()',
);
const dataUrl = 'data:text/javascript;base64,' + Buffer.from(augmented).toString('base64');
await import(dataUrl);
const hwg = globalThis.__hwg;

const grammar = JSON.parse(
  readFileSync(new URL('./cubyte.tmLanguage.json', import.meta.url), 'utf8'),
);

// Test a multi-line cubyte source that exercises every grammar feature.
const sample = `// Compute N^2.
input "N: ";

#define K 10
let int:8 x := K;

let alg alg_a := "R U";
output alg_a;
`;

const html = hwg(grammar, sample);

// Walk the HTML and assert each expected class appears at least once.
const expected = [
  'cbyte-comment',                                  // comment-line-double-slash
  'cbyte-keyword-control-directive-define',         // beginCaptures
  'cbyte-entity-name-function-preprocessor',        // preprocessor entity
  'cbyte-string',                                   // string
  'cbyte-keyword-other',                            // input / output
  'cbyte-storage-type',                             // int / alg
  'cbyte-storage-modifier',                         // let
  'cbyte-keyword-operator-assignment',              // :=
  'cbyte-constant-numeric',                         // 10
  'cbyte-variable-other',                           // x / K / alg_a
  'cbyte-punctuation-terminator-statement',         // ;
];

let pass = 0, fail = 0;
for (const cls of expected) {
  if (html.includes(`class="${cls}"`) || html.includes(`"${cls} `) || html.includes(` ${cls}"`)) {
    pass++;
    console.log(`  ok: ${cls}`);
  } else {
    fail++;
    console.log(`MISS: ${cls}`);
  }
}
console.log(`\n${pass}/${expected.length} class checks passed.`);

// Also assert the directive span contains the literal `#define` text.
if (/cbyte-keyword-control-directive-define">#define<\/span>/.test(html)) {
  console.log('  ok: #define is wrapped in the directive class');
} else {
  console.log('MISS: #define is not wrapped in the directive class');
  console.log('first 200 chars:', html.slice(0, 200));
}
