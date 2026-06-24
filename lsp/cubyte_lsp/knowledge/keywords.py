"""Static metadata for cubyte's built-in keywords.

The list is derived from the lexer in
``include/lexer.h`` (TokenType enum) and the syntax summary in the
project README. Keep this in sync if a new keyword is added.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Keyword:
    name: str
    doc: str  # markdown snippet used in hover


KEYWORDS: tuple[Keyword, ...] = (
    Keyword("let",     "Declare a new variable. `let int : <order> <name> := <expr>;` or `let alg <name> := \"<alg>\";`."),
    Keyword("int",     "Integer register variable. The number after `:` is the required register order."),
    Keyword("alg",     "Algorithm-valued variable. Holds a compile-time string of cube moves."),
    Keyword("if",      "Conditional. Condition must reduce to a `solved` test, an equality, or a `not`."),
    Keyword("else",    "Branch taken when the matching `if` condition is false."),
    Keyword("while",   "Loop. Like `if`, the condition must be a primitive cubyte test."),
    Keyword("goto",    "Unconditional jump to a label."),
    Keyword("input",   "Read an algorithm from the user into the reserved I/O register `_io`."),
    Keyword("output",  "Drain the I/O register, printing the number of repetitions required."),
    Keyword("apply",   "Emit an algorithm literal as raw cube moves (with cancellations applied)."),
    Keyword("ord",     "Returns the order of a variable's register algorithm."),
    Keyword("solved",  "Tests whether a set of named pieces are currently in their home positions."),
    Keyword("not",     "Boolean negation."),
)


def names() -> tuple[str, ...]:
    return tuple(k.name for k in KEYWORDS)


def doc_for(name: str) -> str | None:
    for k in KEYWORDS:
        if k.name == name:
            return k.doc
    return None
