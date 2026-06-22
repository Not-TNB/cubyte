// CuByte syntax highlighter.
//
// Fetches the TextMate grammar (cubyte.tmLanguage.json) at startup, then
// provides highlightCuByte(text) that converts a source string into HTML
// spans tagged with CSS classes derived from the TextMate scope names.
//
// Scope name -> CSS classes: split the dotted scope on '.', drop the
// trailing ".cbyte" language tag, then emit one class per prefix joined
// with '-'. So
//   comment.line.double-slash.cbyte  ->  cbyte-comment cbyte-comment-line
//                                        cbyte-comment-line-double-slash
//   keyword.control.directive.define.cbyte
//                                     ->  cbyte-keyword cbyte-keyword-control
//                                        cbyte-keyword-control-directive
//                                        cbyte-keyword-control-directive-define
// Emitting every prefix lets a single span match CSS rules at any level
// of granularity: a generic catch-all like `.cbyte-comment` styles every
// comment, while a specific rule like `.cbyte-keyword-control-directive-define`
// wins by source order for the few scopes that have one.
//
// The grammar supports `begin`/`end` regions (e.g. strings, #define bodies)
// and the `beginCaptures` / `endCaptures` maps that assign scopes to
// sub-ranges of those delimiters. We walk the input character by
// character, advancing through a stack of open regions; within each
// region we try the top-level patterns in order and consume the longest
// match. This is a faithful subset of how Oniguruma-driven engines work
// and is enough for our small grammar.

// ---------------------------------------------------------------------------
// Grammar loader
// ---------------------------------------------------------------------------

// Resolves a `#name` reference to the corresponding rule in the grammar.
// Throws on a missing rule so we fail fast during development if the
// grammar and the runtime drift apart.
function resolveRule(grammar, name) {
  if (!name.startsWith('#')) {
    throw new Error(`Expected repository reference, got: ${name}`);
  }
  const rule = grammar.repository[name.slice(1)];
  if (!rule) {
    throw new Error(`Unknown rule reference: ${name}`);
  }
  return rule;
}

// Returns the list of `match`/`begin` rules a given rule expands to.
// Rule types in the grammar:
//   { include: '#x' }                    -> recurse into another rule
//   { match: '...', name: '...' }        -> a single token
//   { begin: '...', end: '...', patterns: [...] } -> a region
//   { patterns: [...] }                   -> a group, expand children
// Top-level rules in `grammar.patterns` are also group rules.
function expandRule(grammar, rule) {
  if (rule.include) {
    return expandRule(grammar, resolveRule(grammar, rule.include));
  }
  if (rule.match || rule.begin) {
    return [rule];
  }
  if (Array.isArray(rule.patterns)) {
    return rule.patterns.flatMap((r) => expandRule(grammar, r));
  }
  return [];
}

// Returns the CSS class names for a dotted scope: every prefix of the
// scope (after dropping the trailing ".cbyte" language tag) joined with
// '-', each prefixed with "cbyte-". See the file header for examples
// and rationale.
function scopeToClasses(scope) {
  const parts = scope.split('.');
  if (parts.length > 0 && parts[parts.length - 1] === 'cbyte') parts.pop();
  const classes = [];
  for (let i = 1; i <= parts.length; i++) {
    classes.push('cbyte-' + parts.slice(0, i).join('-'));
  }
  return classes;
}

// Extracts the scope name from a capture entry. Capture entries in
// TextMate can be either a bare string (`"0": "scope.name"`) or an
// object with a `name` field (`"0": { "name": "scope.name" }`). Returns
// null if the entry is missing or has no name.
function captureScope(entry) {
  if (!entry) return null;
  if (typeof entry === 'string') return entry;
  if (typeof entry === 'object' && entry.name) return entry.name;
  return null;
}

// Builds a RegExp from a string pattern. The patterns in our grammar
// are written for Oniguruma, but the subset we use (character classes,
// alternation, word boundaries, lookbehinds) is identical in
// JavaScript. The `m` flag is essential: TextMate's `$` and `^` match
// line boundaries by default, whereas JavaScript only does that with
// the `m` flag set. The `d` flag enables match.indices, which we need
// to map capture groups to character ranges in the matched text.
function compile(pattern) {
  return new RegExp(pattern, 'gmd');
}

// ---------------------------------------------------------------------------
// Highlighter
// ---------------------------------------------------------------------------

// Highlights `text` according to the given TextMate grammar and returns
// the inner HTML for a <pre> element. The only HTML in the output is
// from our own <span> tags; user text is HTML-escaped character by
// character so positions in the original text remain valid for the
// regexes.
function highlightWithGrammar(grammar, text) {
  // Pre-expand the top-level patterns into a flat list of match/region
  // rules. The grammar's repository references (`#x`) and group rules
  // (`{ patterns: [...] }`) are resolved here so the main loop can
  // iterate over a flat array.
  const topRules = grammar.patterns.flatMap((r) => expandRule(grammar, r));

  // The region stack. Each frame is one of:
  //   { isTop: true }                       -- the synthetic top level
  //   { scope, endRegex, children, scopeOpen } -- an open region
  // `scopeOpen` is true between the begin delimiter and the end
  // delimiter: the region's wrapper <span> is on the open-spans stack
  // during that window, so any text emitted from inside the region
  // (literals or child-rule matches) is automatically wrapped in it.
  const topFrame = { isTop: true, rules: topRules };
  const stack = [topFrame];

  let pos = 0;
  const out = [];
  // Stack of closing </span> tags, paired 1:1 with unclosed <span>
  // opens. push when we open, pop when we close. The main loop never
  // has to think about the structural stack — it just opens and
  // closes in the natural order of the text.
  const openSpans = [];

  // Try every rule at the current position; return the longest match
  // (ties broken by rule order, which is the natural priority of the
  // grammar). Picking the longest match is important for things like
  // `:=` vs `:` — `:=` must beat `:`.
  function tryMatch(rules) {
    let best = null;
    for (const rule of rules) {
      if (rule.begin) {
        rule._beginRe = rule._beginRe || compile(rule.begin);
        rule._beginRe.lastIndex = pos;
        const m = rule._beginRe.exec(text);
        if (m && m.index === pos) {
          if (!best || m[0].length > best.match[0].length) {
            best = { rule, match: m, isRegion: true };
          }
        }
      } else if (rule.match) {
        rule._matchRe = rule._matchRe || compile(rule.match);
        rule._matchRe.lastIndex = pos;
        const m = rule._matchRe.exec(text);
        if (m && m.index === pos) {
          if (!best || m[0].length > best.match[0].length) {
            best = { rule, match: m, isRegion: false };
          }
        }
      }
    }
    return best;
  }

  // HTML-escape a string. Called per matched slice so we can keep
  // positions in the original text valid for the regexes (escaping the
  // whole input up-front would shift every position past the first `<`).
  function escapeSlice(s) {
    let escaped = '';
    for (let i = 0; i < s.length; i++) {
      const c = s[i];
      if (c === '&') escaped += '&amp;';
      else if (c === '<') escaped += '&lt;';
      else if (c === '>') escaped += '&gt;';
      else escaped += c;
    }
    return escaped;
  }

  // Push a literal slice of text (HTML-escaped) without changing scope.
  function emitLiteral(end) {
    if (end <= pos) return;
    out.push(escapeSlice(text.slice(pos, end)));
    pos = end;
  }

  function openScope(scope) {
    out.push(`<span class="${scopeToClasses(scope).join(' ')}">`);
    openSpans.push('</span>');
  }

  function closeScope() {
    out.push(openSpans.pop());
  }

  // Emits the text of a `begin` or `end` match honouring its captures
  // map. The region's wrapper span (if any) is already open on the
  // openSpans stack; we open narrower capture spans inside it for any
  // sub-range with an assigned scope, and emit unhighlighted text for
  // the gaps.
  //
  // Capture group 0 in TextMate means "the whole match", so a
  // captures map like { "0": { name: "..." } } styles the entire
  // delimiter. Numeric keys map to capture group indices (1-based).
  // We use match.indices (enabled by the `d` flag in `compile`) to
  // locate each group in absolute text positions and convert them to
  // offsets relative to the match.
  function emitCapturedDelimiter(match, captures) {
    if (!captures || match[0].length === 0) {
      out.push(escapeSlice(text.slice(pos, pos + match[0].length)));
      pos += match[0].length;
      return;
    }
    // Collect the assigned capture ranges as offsets within the match.
    const ranges = [];
    for (let g = 0; g < match.length; g++) {
      const entry = captures[g] !== undefined ? captures[g] : captures[String(g)];
      const scope = captureScope(entry);
      if (!scope) continue;
      let from, to;
      if (g === 0) {
        from = 0;
        to = match[0].length;
      } else if (match[g] !== undefined && match.indices) {
        const [a, b] = match.indices[g];
        from = a - match.index;
        to = b - match.index;
      } else {
        continue;
      }
      ranges.push({ from, to, scope });
    }
    // Walk the match in left-to-right order. Gaps between (or before)
    // captures are emitted as plain text inside whatever wrapper span
    // is already open.
    ranges.sort((a, b) => a.from - b.from);
    let cur = 0;
    for (const r of ranges) {
      if (r.from < cur) continue; // overlapping capture, skip the later one
      if (r.from > cur) {
        out.push(escapeSlice(text.slice(pos + cur, pos + r.from)));
      }
      openScope(r.scope);
      out.push(escapeSlice(text.slice(pos + r.from, pos + r.to)));
      closeScope();
      cur = r.to;
    }
    if (cur < match[0].length) {
      out.push(escapeSlice(text.slice(pos + cur, pos + match[0].length)));
    }
    pos += match[0].length;
  }

  // The main loop. Advances `pos` past matched text, emits it (possibly
  // wrapped in spans), and updates the stack. Loops until end of input.
  while (pos < text.length) {
    const top = stack[stack.length - 1];

    // If we're inside a region, the region's end pattern takes priority
    // over child rules: it can match even at a position where a child
    // rule would also match.
    if (!top.isTop && top.endRegex) {
      top.endRegex.lastIndex = pos;
      const m = top.endRegex.exec(text);
      if (m && m.index === pos) {
        // The region's wrapper <span> is currently on the open-spans
        // stack. Any child-rule spans inside the body are above it;
        // close those first so we don't emit `</span></span>` for
        // nothing.
        while (openSpans.length > top.scopeOpenDepth) closeScope();
        // Emit the end delimiter text, honouring endCaptures if any.
        emitCapturedDelimiter(m, top.endCaptures);
        // Now close the region's wrapper span and pop the frame.
        closeScope();
        stack.pop();
        continue;
      }
    }

    // Try the active rule set.
    const rules = top.isTop ? top.rules : top.children;
    const hit = tryMatch(rules);
    if (!hit) {
      // No rule matches: emit one character as a literal. The user
      // always sees their input even if a future grammar extension
      // introduces tokens we don't know about.
      emitLiteral(pos + 1);
      continue;
    }

    // A zero-length match would spin the main loop forever. None of
    // the patterns in our grammar can produce one, but defend against
    // a future grammar extension that introduces one.
    if (hit.match[0].length === 0) {
      emitLiteral(pos + 1);
      continue;
    }

    if (hit.isRegion) {
      // Open the region's wrapper span. We deliberately do NOT close
      // it after the begin delimiter: the span must stay open for the
      // entire body so that literals and child-rule matches inside the
      // body are automatically wrapped in it.
      openScope(hit.rule.name);
      // Remember the open-spans stack depth at the moment we entered
      // the region. The end-pattern handler uses this to know how
      // many spans above the region wrapper to close before closing
      // the region itself.
      const regionScopeOpenDepth = openSpans.length;
      // Emit the begin delimiter text, honouring beginCaptures if any.
      // The begin captures nest inside the region's wrapper span.
      emitCapturedDelimiter(hit.match, hit.rule.beginCaptures);
      stack.push({
        isTop: false,
        scope: hit.rule.name,
        endRegex: hit.rule._endRe || (hit.rule._endRe = compile(hit.rule.end)),
        endCaptures: hit.rule.endCaptures,
        children: (hit.rule.patterns || []).flatMap((r) => expandRule(grammar, r)),
        scopeOpenDepth: regionScopeOpenDepth,
      });
    } else {
      // Single token. Wrap the matched text in its own scope. The
      // span is on top of the open-spans stack; any subsequent text
      // emission reverts to the parent scope naturally because we
      // close it right after.
      openScope(hit.rule.name);
      out.push(escapeSlice(text.slice(pos, pos + hit.match[0].length)));
      pos += hit.match[0].length;
      closeScope();
    }
  }

  // Defensive: any spans still open at end of input are an unbalanced
  // region. The grammar is well-formed so this should never happen, but
  // a stray `</span>` is harmless; a missing one would visually bleed
  // the style past EOF.
  while (openSpans.length > 0) closeScope();

  return out.join('');
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Loads the grammar once and caches the compiled form. Subsequent calls
// reuse the same grammar. Resolves to a `highlight(text)` function.
//
// If the fetch fails (e.g. the user opened index.html via file://, where
// the browser blocks fetch for local files), resolves to `null` and the
// caller should treat the highlighter as unavailable. We never throw so
// the rest of the visualiser can keep working without colours.
let _cachedHighlighter = null;

export async function loadCuByteHighlighter() {
  if (_cachedHighlighter) return _cachedHighlighter;
  try {
    const resp = await fetch('./cubyte.tmLanguage.json');
    if (!resp.ok) return null;
    const grammar = await resp.json();
    _cachedHighlighter = (text) => highlightWithGrammar(grammar, text);
    return _cachedHighlighter;
  } catch (_) {
    // file:// blocks fetch, or the JSON is malformed. Either way,
    // the caller should fall back to no highlighting.
    return null;
  }
}
