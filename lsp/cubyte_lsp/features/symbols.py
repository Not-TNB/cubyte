"""Document symbols.

Walks the buffer with regexes and produces one
:class:`~cubyte_lsp.protocol.types.DocumentSymbol` per declaration and
label. The output nests by block — declarations go into the enclosing
``{ }`` block if there is one, otherwise the top level.
"""

from __future__ import annotations

import re
from dataclasses import dataclass

from ..protocol.types import (
    SYMBOL_KEYWORD,
    SYMBOL_VARIABLE,
    DocumentSymbol,
    Position,
    Range,
)


_VAR_RE = re.compile(r"let\s+(int|alg)\s*(?::\s*(\d+))?\s+([A-Za-z_][A-Za-z_0-9]*)\s*:=([^\n;]*)")
_LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z_0-9]*)\s*:\s*(?!=)", re.MULTILINE)


async def document_symbols(params: dict, doc) -> list[dict]:
    """Return the flat list of top-level symbols.

    Real block-nesting requires parsing; the regex walker is enough for
    the outline view.
    """
    out: list[DocumentSymbol] = []
    seen: set[tuple[int, int, str]] = set()

    for m in _VAR_RE.finditer(doc.text):
        line = doc.text.count("\n", 0, m.start())
        # Range spans from the ``let`` keyword to the end of the line.
        end_line = doc.text.count("\n", 0, m.end())
        key = (line, end_line, m.group(3))
        if key in seen:
            continue
        seen.add(key)
        kind = SYMBOL_KEYWORD if m.group(1) == "alg" else SYMBOL_VARIABLE
        detail = None
        if m.group(2) is not None:
            detail = f"int : {m.group(2)}"
        else:
            detail = m.group(1)
        out.append(DocumentSymbol(
            name=m.group(3),
            kind=kind,
            range=Range(
                start=Position(line=line, character=0),
                end=Position(line=end_line, character=0),
            ),
            selection_range=Range(
                start=Position(line=line, character=m.start(3) - m.start()),
                end=Position(line=line, character=m.end(3) - m.start()),
            ),
            detail=detail,
        ))

    for m in _LABEL_RE.finditer(doc.text):
        line = doc.text.count("\n", 0, m.start())
        key = (line, line, m.group(1))
        if key in seen:
            continue
        seen.add(key)
        out.append(DocumentSymbol(
            name=m.group(1),
            kind=14,  # SymbolKind.Keyword
            range=Range(
                start=Position(line=line, character=0),
                end=Position(line=line, character=m.end() - m.start()),
            ),
            selection_range=Range(
                start=Position(line=line, character=0),
                end=Position(line=line, character=m.end(1) - m.start()),
            ),
            detail="label",
        ))

    return [s.to_dict() for s in out]