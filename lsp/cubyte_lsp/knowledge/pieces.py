"""Static metadata for cubyte's piece labels.

The full list lives in ``include/piece.h`` as the ``PieceLabel`` enum.
We re-declare it here so the LSP can be self-contained for hover/completion
on the ``solved [...]`` expression without having to load the C header.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Piece:
    name: str
    kind: str  # "corner" or "edge"
    faces: tuple[str, ...]
    doc: str


# Order matches the C enum so the indices stay meaningful if we ever want
# to round-trip with the compiler.
PIECES: tuple[Piece, ...] = (
    Piece("UFL", "corner", ("U", "F", "L"), "Upper-Front-Left corner."),
    Piece("UF",  "edge",   ("U", "F"),      "Upper-Front edge."),
    Piece("UFR", "corner", ("U", "F", "R"), "Upper-Front-Right corner."),
    Piece("UL",  "edge",   ("U", "L"),      "Upper-Left edge."),
    Piece("UR",  "edge",   ("U", "R"),      "Upper-Right edge."),
    Piece("UBL", "corner", ("U", "B", "L"), "Upper-Back-Left corner."),
    Piece("UB",  "edge",   ("U", "B"),      "Upper-Back edge."),
    Piece("UBR", "corner", ("U", "B", "R"), "Upper-Back-Right corner."),
    Piece("DFL", "corner", ("D", "F", "L"), "Down-Front-Left corner."),
    Piece("DF",  "edge",   ("D", "F"),      "Down-Front edge."),
    Piece("DFR", "corner", ("D", "F", "R"), "Down-Front-Right corner."),
    Piece("DL",  "edge",   ("D", "L"),      "Down-Left edge."),
    Piece("DR",  "edge",   ("D", "R"),      "Down-Right edge."),
    Piece("DBL", "corner", ("D", "B", "L"), "Down-Back-Left corner."),
    Piece("DB",  "edge",   ("D", "B"),      "Down-Back edge."),
    Piece("DBR", "corner", ("D", "B", "R"), "Down-Back-Right corner."),
    Piece("FL",  "edge",   ("F", "L"),      "Front-Left edge."),
    Piece("FR",  "edge",   ("F", "R"),      "Front-Right edge."),
    Piece("BL",  "edge",   ("B", "L"),      "Back-Left edge."),
    Piece("BR",  "edge",   ("B", "R"),      "Back-Right edge."),
)


def names() -> tuple[str, ...]:
    return tuple(p.name for p in PIECES)


def doc_for(name: str) -> str | None:
    for p in PIECES:
        if p.name == name:
            return p.doc
    return None
