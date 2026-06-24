"""Smoke test for scope-aware completion.

Verifies that :func:`completion` does not suggest a ``let`` declaration
whose text appears at or after the cursor (cubyte's scoping rule is
purely textual, so the name is not yet in scope there). Labels, by
contrast, are valid ``goto`` targets from anywhere in the program and
should always be offered.

Run with ``python -m cubyte_lsp.tests.test_completion_scope`` from the
``lsp/`` dir.
"""

from __future__ import annotations

import sys
from pathlib import Path

# Allow running the test from the lsp/ directory without installing.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from cubyte_lsp.features.completion import _document_symbols  # noqa: E402


_PROGRAM = (
    "let int : 5 acc := 0;\n"      # line 0: 'acc' declared at offset 0
    "\n"                            # line 1: blank
    "start:\n"                      # line 2: 'start' label
    "acc := acc + 1;\n"             # line 3
    "if not (acc = 0) {\n"          # line 4
    "    goto done;\n"              # line 5
    "}\n"                           # line 6
    "goto start;\n"                 # line 7
    "\n"                            # line 8
    "done:\n"                       # line 9: 'done' label
)


def _line_offset(text: str, line: int) -> int:
    """Char offset of the start of ``line`` (0-based)."""
    if line == 0:
        return 0
    offset = -1
    for _ in range(line):
        offset = text.find("\n", offset + 1)
        if offset == -1:
            return len(text)
    return offset + 1


def _labels_at(text: str, offset: int | None) -> set[str]:
    """Names yielded by :func:`_document_symbols` for a given offset."""
    return {item.label for item in _document_symbols(text, offset)}


def test_offset_zero_offers_no_variables_below() -> None:
    """At offset 0 no variable has been declared yet, so none are
    offered. Labels, however, are valid ``goto`` targets anywhere and
    are always offered (they appear in the output too, even at offset
    0)."""
    labels = _labels_at(_PROGRAM, 0)
    assert "acc" not in labels, "acc should not be in scope at offset 0"
    # Labels: still offered even at offset 0 (goto can target any).
    assert "start" in labels
    assert "done" in labels


def test_offset_after_let_offers_variable() -> None:
    """At offset of line 3 the variable is in scope."""
    offset = _line_offset(_PROGRAM, 3)
    labels = _labels_at(_PROGRAM, offset)
    assert "acc" in labels
    # Labels — always in scope per cubyte's `goto` rule.
    assert "start" in labels
    assert "done" in labels


def test_labels_always_offered() -> None:
    """Labels are valid ``goto`` targets everywhere, so they appear at
    every offset we test, including before their declaration."""
    for line in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9):
        offset = _line_offset(_PROGRAM, line)
        labels = _labels_at(_PROGRAM, offset)
        assert "start" in labels, f"start should be in scope at line {line}"
        assert "done" in labels, f"done should be in scope at line {line}"


def test_no_offset_offers_everything() -> None:
    """Defensive fallback: no position means we don't filter."""
    labels = _labels_at(_PROGRAM, None)
    assert "acc" in labels
    assert "start" in labels
    assert "done" in labels


def test_alg_let_also_filtered() -> None:
    """``let alg`` declarations follow the same textual rule."""
    text = "let alg a := \"R U\";\nlet alg b := a ++ \"R2\";\n"
    # Cursor on line 0 → only 'a' might be in scope? 'a' starts at
    # offset 0, so it is NOT in scope. 'b' is on line 1, not in scope
    # at line 0. So nothing is offered.
    labels = _labels_at(text, _line_offset(text, 0))
    assert "a" not in labels
    assert "b" not in labels
    # Cursor on line 1 (after 'a' but before 'b').
    labels = _labels_at(text, _line_offset(text, 1))
    assert "a" in labels
    assert "b" not in labels


if __name__ == "__main__":
    test_offset_zero_offers_no_variables_below()
    test_offset_after_let_offers_variable()
    test_labels_always_offered()
    test_no_offset_offers_everything()
    test_alg_let_also_filtered()
    print("ok")
