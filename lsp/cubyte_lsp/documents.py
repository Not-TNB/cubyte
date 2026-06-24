"""In-memory store of open cubyte documents.

The LSP server is single-process and shared across all open files, so a
plain :class:`dict` keyed on URI is sufficient. We also keep a simple
generation counter so an in-flight analysis for an older version of the
buffer can be discarded when a newer one arrives.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional


@dataclass
class Document:
    uri: str
    version: int = 0
    text: str = ""
    language_id: str = "cubyte"
    generation: int = 0  # bumped on every didChange
    # Last diagnostics produced by the analyzer, kept so the client can
    # be re-served them after a server restart of the editor session.
    last_diagnostics: tuple = field(default_factory=tuple)


class DocumentStore:
    def __init__(self) -> None:
        self._docs: dict[str, Document] = {}

    def open(self, uri: str, version: int, text: str, language_id: str) -> Document:
        doc = Document(uri=uri, version=version, text=text, language_id=language_id)
        self._docs[uri] = doc
        return doc

    def update(self, uri: str, version: int, text: str) -> Optional[Document]:
        doc = self._docs.get(uri)
        if doc is None:
            return None
        doc.version = version
        doc.text = text
        doc.generation += 1
        return doc

    def close(self, uri: str) -> None:
        self._docs.pop(uri, None)

    def get(self, uri: str) -> Optional[Document]:
        return self._docs.get(uri)

    def all(self) -> list[Document]:
        return list(self._docs.values())