"""Go-to-definition.

Stub implementation: walks the document for the identifier under the
cursor and jumps to its declaration site (variable ``let`` or label).
The position reported is the *start* of the declaration so the editor
centres on the keyword.
"""

from __future__ import annotations

import re

from ..protocol.types import Position
from ..utils import position_at


_VAR_RE = re.compile(r"let\s+(int|alg)\s*(?::\s*\d+\s+)?([A-Za-z_][A-Za-z_0-9]*)\s*:=")
_LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z_0-9]*)\s*:\s*(?!=)", re.MULTILINE)
_IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")


async def definition(params: dict, doc) -> list[dict] | None:
    position = Position.from_dict(params["position"])
    word = _word_at(doc.text, position)
    if word is None:
        return None

    # Prefer a variable declaration, fall back to a label, fall back to None.
    for pattern in (_VAR_RE, _LABEL_RE):
        for m in pattern.finditer(doc.text):
            name = m.group(2) if pattern is _VAR_RE else m.group(1)
            if name != word:
                continue
            line = doc.text.count("\n", 0, m.start())
            return [{
                "uri": doc.uri,
                "range": {
                    "start": {"line": line, "character": 0},
                    "end": {"line": line, "character": m.end() - m.start()},
                },
            }]
    return None


def _word_at(text: str, position: Position) -> str | None:
    # Approximate the same algorithm as in hover.py. We do not import
    # it to keep this module independent; the spec only requires we
    # return the right identifier, not be clever about column edges.
    offset = _offset_for(text, position)
    if offset is None:
        return None
    start = offset
    while start > 0 and _IDENT_RE.match(text[start - 1]):
        start -= 1
    end = offset
    while end < len(text) and _IDENT_RE.match(text[end]):
        end += 1
    if start == end:
        return None
    return text[start:end]


def _offset_for(text: str, position: Position) -> int | None:
    line = 0
    offset = 0
    while offset <= len(text):
        line_start = text.rfind("\n", 0, offset) + 1
        line_end = text.find("\n", offset)
        if line_end == -1:
            line_end = len(text)
        if line == position.line:
            col = position.character - (offset - line_start)
            if 0 <= col <= line_end - line_start:
                return line_start + col
            return None
        offset = line_end + 1
        line += 1
    return None