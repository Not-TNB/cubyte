"""Completion provider.

Returns three kinds of items:

* Keywords, with a snippet form for the common shapes
  (``let int : 4 $1 := $2;`` etc.).
* Type names (``int``, ``alg``).
* Piece labels (the 20 corner/edge names).
* Identifiers visible in the current document — declared variables and
  labels. We use a regex pass for now; a proper parser would let us
  scope these to the right block. cubyte's scoping rule is purely
  textual (a name is in scope only after the line that declares it), so
  we filter the document-symbols pass by the cursor's character offset
  and drop any declaration that comes at or after the cursor.

The order is: keywords, types, then user identifiers, then pieces. The
detail field shows the kind so editors can render it in a column.
"""

from __future__ import annotations

import re
from typing import Iterable, Optional

from ..knowledge import keywords as kw
from ..knowledge import pieces as pc
from ..knowledge import types as ty
from ..protocol.types import (
    COMPLETION_CONSTANT,
    COMPLETION_KEYWORD,
    COMPLETION_TEXT,
    COMPLETION_VARIABLE,
    CompletionItem,
    Position,
)


# Snippets for keywords. ``$1``, ``$2`` are tab stops the editor fills in.
_SNIPPETS: dict[str, str] = {
    "let":   "let int : 4 $1 := $2;",
    "if":    "if not ($1 = 0) {\n\t$2\n}",
    "while": "while not ($1 = 0) {\n\t$2\n}",
    "goto":  "goto $1;",
    "input": "input \"$1\";",
    "output":"output;",
    "apply": "apply $1;",
}


async def completion(params: dict, doc) -> list[dict]:
    items: list[CompletionItem] = []

    for k in kw.KEYWORDS:
        items.append(CompletionItem(
            label=k.name,
            kind=COMPLETION_KEYWORD,
            detail="keyword",
            documentation=k.doc,
            insert_text=_SNIPPETS.get(k.name, k.name),
        ))

    for t in ty.TYPES:
        items.append(CompletionItem(
            label=t.name,
            kind=COMPLETION_KEYWORD,
            detail="type",
            documentation=t.doc,
        ))

    for piece in pc.PIECES:
        items.append(CompletionItem(
            label=piece.name,
            kind=COMPLETION_CONSTANT,
            detail="piece",
            documentation=piece.doc,
        ))

    # Document-level symbols are scoped to the cursor: cubyte's scoping
    # rule is purely textual, so a declaration at or after the cursor
    # is not yet in scope and must not be suggested. If the client omits
    # the position we fall back to offering everything.
    cursor_offset = _cursor_offset(params, doc.text)
    items.extend(_document_symbols(doc.text, cursor_offset))

    return [it.to_dict() for it in items]


def _cursor_offset(params: dict, text: str) -> Optional[int]:
    """Translate the completion request's ``position`` to a char offset.

    Returns ``None`` if the position is missing or invalid, in which case
    the caller should treat the buffer as having no scoping filter
    (offer every declared symbol).
    """
    raw = params.get("position")
    if not isinstance(raw, dict):
        return None
    try:
        position = Position.from_dict(raw)
    except (KeyError, TypeError, ValueError):
        return None
    if position.line < 0 or position.character < 0:
        return None
    return _offset_for_line_col(text, position)


def _offset_for_line_col(text: str, position: Position) -> int:
    """Convert an LSP ``Position`` to a 0-based char offset.

    Clamps a column past the end of a line to that line's end (editors
    may report the column one past the last glyph when triggering
    completion on a trailing newline). Returns ``len(text)`` for
    positions past the end of the buffer.
    """
    line = 0
    pos = 0
    while pos <= len(text):
        nl = text.find("\n", pos)
        line_end = nl if nl != -1 else len(text)
        if line == position.line:
            col = min(position.character, line_end - pos)
            return pos + col
        if nl == -1:
            return len(text)
        line += 1
        pos = nl + 1
    return len(text)


def _document_symbols(text: str, cursor_offset: Optional[int] = None) -> Iterable[CompletionItem]:
    seen: set[str] = set()
    # Variable declarations: `let (int|alg) [ : <order> ] name := ...`.
    # cubyte scoping is textual — a `let` is in scope only from the line
    # *after* its declaration. So when the cursor is on or before the
    # declaration, the name must not be suggested (using it would be a
    # typecheck error).
    for m in re.finditer(r"let\s+(int|alg)\s*(?::\s*\d+\s+)?([A-Za-z_][A-Za-z_0-9]*)\s*:=", text):
        if cursor_offset is not None and m.start() >= cursor_offset:
            continue
        name = m.group(2)
        if name in seen:
            continue
        seen.add(name)
        kind = "variable"
        if m.group(1) == "alg":
            kind = "algorithm"
        yield CompletionItem(label=name, kind=COMPLETION_VARIABLE, detail=kind)
    # Labels: `name:` at the start of a line (no `=` after). Labels in
    # cubyte are valid ``goto`` targets from anywhere in the program
    # (the typechecker collects all of them in a pre-pass), so we
    # always offer them, regardless of cursor position.
    for m in re.finditer(r"^([A-Za-z_][A-Za-z_0-9]*)\s*:\s*(?!=)", text, flags=re.MULTILINE):
        name = m.group(1)
        if name in seen:
            continue
        seen.add(name)
        yield CompletionItem(label=name, kind=COMPLETION_TEXT, detail="label")