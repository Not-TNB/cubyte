"""Completion provider.

Returns four kinds of items:

* Keywords, with a snippet form for the common shapes
  (``let int : 4 $1 := $2;`` etc.).
* Type names (``int``, ``alg``).
* Piece labels (the 20 corner/edge names).
* Identifiers visible at the cursor: declared variables scoped to the
  enclosing block, labels (valid goto targets everywhere), and the
  built-in ``_io`` I/O register which is always in scope.

Scoping rules:
- ``_io`` is always in scope (it is a reserved register, never declared).
- Labels are always in scope (the typechecker pre-collects them for goto).
- ``let`` variables obey two rules simultaneously:
    1. Textual order: a name is not in scope on or before its own
       declaration line.
    2. Block scope: a variable declared inside ``{…}`` is only in scope
       within that block. Variables at the top level have no enclosing
       block and are in scope everywhere after their declaration.

The order is: keywords, types, ``_io``, user identifiers, pieces. The
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

_IO_DOC = (
    "Reserved: refers to the I/O register (R0). `input` and `output` "
    "operate on it. It behaves like a regular `int` but cannot be "
    "redeclared. Order is 9."
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

    # _io is always in scope — it is a reserved register, never declared.
    items.append(CompletionItem(
        label="_io",
        kind=COMPLETION_VARIABLE,
        detail="I/O register",
        documentation=_IO_DOC,
    ))

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


def _build_scope_ranges(text: str) -> list[tuple[int, int]]:
    """Return (open, close) offset pairs for every ``{…}`` block.

    Braces inside string literals are ignored so algorithm literals like
    ``"R {U}"`` (hypothetical) do not create phantom scopes.
    """
    ranges: list[tuple[int, int]] = []
    stack: list[int] = []
    in_string = False
    for i, c in enumerate(text):
        if c == '"':
            in_string = not in_string
        elif not in_string:
            if c == '{':
                stack.append(i)
            elif c == '}' and stack:
                ranges.append((stack.pop(), i))
    return ranges


def _innermost_scope(pos: int, scope_ranges: list[tuple[int, int]]) -> tuple[int, int] | None:
    """Return the innermost scope range whose open/close offsets surround *pos*.

    "Innermost" means the one whose opening brace is closest (and before)
    *pos*. Returns ``None`` when *pos* is at the top level.
    """
    result: tuple[int, int] | None = None
    for s, e in scope_ranges:
        if s < pos < e:
            if result is None or s > result[0]:
                result = (s, e)
    return result


def _document_symbols(text: str, cursor_offset: Optional[int] = None) -> Iterable[CompletionItem]:
    seen: set[str] = set()
    scope_ranges = _build_scope_ranges(text)

    # Variable declarations: `let (int|alg) [ : <order> ] name := ...`.
    # Two scoping rules apply simultaneously:
    #   1. Textual order — a name is not in scope at or before its own
    #      declaration offset.
    #   2. Block scope — a variable declared inside ``{…}`` is only
    #      visible within that block. Top-level variables (no enclosing
    #      brace) are visible everywhere after their declaration.
    for m in re.finditer(r"let\s+(int|alg)\s*(?::\s*\d+\s+)?([A-Za-z_][A-Za-z_0-9]*)\s*:=", text):
        if cursor_offset is not None and m.start() >= cursor_offset:
            continue

        decl_scope = _innermost_scope(m.start(), scope_ranges)
        if cursor_offset is not None and decl_scope is not None:
            # Variable is block-scoped; cursor must lie inside that block.
            s, e = decl_scope
            if not (s < cursor_offset < e):
                continue

        name = m.group(2)
        if name in seen:
            continue
        seen.add(name)
        kind = "algorithm" if m.group(1) == "alg" else "variable"
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