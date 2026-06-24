"""Hover provider.

Hover is our best-developed feature. The strategy is purely lexical —
no AST — because cubyte identifiers are unambiguous: a ``cubyte_lsp``
hover looks at the word under the cursor and walks a small dispatch
table:

1. Built-in keyword (``if``, ``let``, ``solved``, …) → keyword docs.
2. Reserved ``_io`` → I/O register description.
3. Piece label (``UFL``, ``UF``, ``DBR``) → piece doc.
4. Variable name declared in the same document → user-defined type/order.
5. Falls back to a "no info" hover so the editor still pops a tooltip.
"""

from __future__ import annotations

import re
from typing import Optional

from ..knowledge import keywords as kw
from ..knowledge import pieces as pc
from ..knowledge import types as ty
from ..protocol.types import Hover, Position, Range
from ..utils import position_at

# Identifier: starts with a letter or underscore, then letters/digits/_.
_IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")


def _word_at(text: str, position: Position) -> tuple[Optional[str], tuple[int, int]]:
    """Return ``(word, (start_offset, end_offset))`` for the identifier
    under ``position``. Returns ``(None, (0, 0))`` if there is no word
    there.
    """
    offset = _offset_for(text, position)
    if offset is None:
        return None, (0, 0)
    # Expand to the enclosing identifier.
    start = offset
    while start > 0 and _IDENT_RE.match(text[start - 1]):
        start -= 1
    end = offset
    while end < len(text) and _IDENT_RE.match(text[end]):
        end += 1
    if start == end:
        return None, (0, 0)
    return text[start:end], (start, end)


def _offset_for(text: str, position: Position) -> Optional[int]:
    """Translate a ``Position`` to a char offset. Returns ``None`` if
    ``position`` is past the end of the buffer.
    """
    if position.line < 0 or position.character < 0:
        return None

    # Walk the buffer line by line. ``line_start`` is the index of the
    # first character on the requested line; ``line_end`` is one past the
    # last character (either the newline or len(text) on the last line).
    line = 0
    pos = 0
    while pos <= len(text):
        nl = text.find("\n", pos)
        line_end = nl if nl != -1 else len(text)
        if line == position.line:
            if position.character > line_end - pos:
                # Past the end of the requested line — still allow a hit
                # on the trailing character for editors that report the
                # column one past the last glyph.
                return max(pos, line_end - 1) if line_end > pos else pos
            return pos + position.character
        if nl == -1:
            return None
        line += 1
        pos = nl + 1
    return None


async def hover(params: dict, doc) -> Optional[dict]:
    position = Position.from_dict(params["position"])
    word, span = _word_at(doc.text, position)
    if word is None:
        return None

    doc_text = _hover_markdown(word, doc.text)

    # The hover range covers exactly the identifier; that gives editors
    # a clean "what's under the cursor" outline.
    start_off, end_off = span
    word_range = Range(
        start=position_at(doc.text, start_off),
        end=position_at(doc.text, end_off),
    )

    hover_obj = Hover(contents=doc_text, range=word_range)
    return hover_obj.to_dict()


def _hover_markdown(word: str, source: str) -> str:
    if (docstr := kw.doc_for(word)) is not None:
        return f"**`{word}`** (keyword)\n\n{docstr}"

    if word == ty.RESERVED_IO:
        return f"**`{word}`** (reserved)\n\n{ty.doc_for(word)}"

    if (docstr := ty.doc_for(word)) is not None:
        return f"**`{word}`** (type)\n\n{docstr}"

    if (docstr := pc.doc_for(word)) is not None:
        return f"**`{word}`** (piece)\n\n{docstr}"

    # Walk the user's source for a matching declaration so user-defined
    # variables get a hover too. Cheap regex — we don't yet do a full
    # parse.
    for m in re.finditer(r"let\s+(int|alg)\s*(?::\s*(\d+))?\s+([A-Za-z_][A-Za-z_0-9]*)\s*:=", source):
        var_kind, order, var_name = m.group(1), m.group(2), m.group(3)
        if var_name == word:
            if var_kind == "int" and order is not None:
                return f"**`{var_name}`** — `int`, order `{order}`"
            return f"**`{var_name}`** — `{var_kind}`"

    # Labels (`name:`).
    for m in re.finditer(r"^([A-Za-z_][A-Za-z_0-9]*)\s*:", source, flags=re.MULTILINE):
        if m.group(1) == word:
            return f"**`{word}`** — label"

    return f"**`{word}`**\n\nNo additional documentation available."


def _range_for_word(text: str, word: str, position: Position) -> Range:
    # Kept for callers that still use the old shape; delegates to
    # _word_at so the range matches the actual hover span.
    _, (start, end) = _word_at(text, position)
    return Range(start=position_at(text, start), end=position_at(text, end))