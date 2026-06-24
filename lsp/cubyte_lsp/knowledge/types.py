"""Static metadata for cubyte's type system.

Only two type names exist in the surface language: ``int`` and ``alg``.
The compiler also has a synthetic ``_io`` register (R0) and an internal
scratch register (R1) that the user references indirectly — those are
documented here so the LSP can show them on hover.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Type:
    name: str
    doc: str


TYPES: tuple[Type, ...] = (
    Type("int",
         "Integer register variable. The required order is given at the "
         "declaration site (`let int : <order> <name>`). All arithmetic "
         "wraps modulo the order."),
    Type("alg",
         "Algorithm literal. A compile-time string of cube moves that can "
         "be applied with `apply`."),
)


# Reserved identifiers in the language.
RESERVED_IO = "_io"  # always bound to the I/O register (R0)


def doc_for(name: str) -> str | None:
    for t in TYPES:
        if t.name == name:
            return t.doc
    if name == RESERVED_IO:
        return (
            "Reserved: refers to the I/O register (R0). `input` and `output` "
            "operate on it. It behaves like a regular `int` but cannot be "
            "redeclared. Order is 9."
        )
    return None
