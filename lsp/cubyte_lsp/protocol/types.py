"""LSP message type stubs.

We do not depend on ``pygls``/``lsprotocol`` — they are heavy and pull
versions of the spec we don't need. The classes here are lightweight
dataclasses that match the JSON shape the spec requires; they are
constructed from raw dicts and serialised back via ``asdict``.

If a future feature needs more fields, add them here rather than
reaching for ``Any`` in feature code.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import Any, Optional


@dataclass
class Position:
    line: int  # 0-based, per LSP
    character: int  # 0-based, per LSP

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "Position":
        return cls(line=int(d["line"]), character=int(d["character"]))

    def to_dict(self) -> dict[str, Any]:
        return {"line": self.line, "character": self.character}


@dataclass
class Range:
    start: Position
    end: Position

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "Range":
        return cls(
            start=Position.from_dict(d["start"]),
            end=Position.from_dict(d["end"]),
        )

    def to_dict(self) -> dict[str, Any]:
        return {"start": self.start.to_dict(), "end": self.end.to_dict()}


@dataclass
class TextDocumentIdentifier:
    uri: str

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "TextDocumentIdentifier":
        return cls(uri=d["uri"])


@dataclass
class VersionedTextDocumentIdentifier(TextDocumentIdentifier):
    version: int

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "VersionedTextDocumentIdentifier":
        return cls(uri=d["uri"], version=int(d["version"]))


@dataclass
class TextDocumentItem:
    uri: str
    language_id: str
    version: int
    text: str

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "TextDocumentItem":
        return cls(
            uri=d["uri"],
            language_id=d.get("languageId", "cubyte"),
            version=int(d["version"]),
            text=d["text"],
        )


@dataclass
class TextDocumentContentChangeEvent:
    """One entry from a ``didChange`` notification.

    The LSP spec allows two shapes: full replacement (no ``range``) and
    incremental. We model the full one because the spec recommends it and
    the editor usually sends it for our small files.
    """

    text: str
    range: Optional[Range] = None
    range_length: Optional[int] = None

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "TextDocumentContentChangeEvent":
        return cls(
            text=d["text"],
            range=Range.from_dict(d["range"]) if "range" in d else None,
            range_length=d.get("rangeLength"),
        )


@dataclass
class Diagnostic:
    range: Range
    message: str
    severity: int = 1  # DiagnosticSeverity.Error
    source: str = "cubyte"
    code: Optional[str] = None

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class Hover:
    contents: str
    range: Optional[Range] = None

    def to_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {"contents": {"kind": "markdown", "value": self.contents}}
        if self.range is not None:
            d["range"] = self.range.to_dict()
        return d


@dataclass
class CompletionItem:
    label: str
    kind: int = 1  # Text
    detail: Optional[str] = None
    documentation: Optional[str] = None
    insert_text: Optional[str] = None

    def to_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {"label": self.label, "kind": self.kind}
        if self.detail is not None:
            d["detail"] = self.detail
        if self.documentation is not None:
            d["documentation"] = {"kind": "markdown", "value": self.documentation}
        if self.insert_text is not None:
            d["insertText"] = self.insert_text
        return d


@dataclass
class DocumentSymbol:
    name: str
    kind: int  # SymbolKind
    range: Range
    selection_range: Range
    detail: Optional[str] = None
    children: list["DocumentSymbol"] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {
            "name": self.name,
            "kind": self.kind,
            "range": self.range.to_dict(),
            "selectionRange": self.selection_range.to_dict(),
        }
        if self.detail is not None:
            d["detail"] = self.detail
        if self.children:
            d["children"] = [c.to_dict() for c in self.children]
        return d


# SymbolKind values used by this server. The full table is at
# https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#symbolKind
SYMBOL_VARIABLE = 13
SYMBOL_FUNCTION = 12
SYMBOL_KEYWORD = 14
SYMBOL_CONSTANT = 6
SYMBOL_NAMESPACE = 3

# CompletionItemKind
COMPLETION_TEXT = 1
COMPLETION_KEYWORD = 14
COMPLETION_VARIABLE = 6
COMPLETION_CONSTANT = 21
COMPLETION_FIELD = 5
