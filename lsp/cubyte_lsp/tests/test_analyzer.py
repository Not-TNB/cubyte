"""Tests for the stderr -> diagnostic parser in :mod:`cubyte_lsp.analyzer`.

These tests exercise :func:`_parse_stderr` directly (no subprocess). The
real ``cubyte`` binary emits three shapes on stderr:

* ``file:line:col: error[stage]: msg``      — ``lexer`` / ``parse``
* ``file:line:      error[stage]: msg``     — ``typecheck`` (no col)
* ``                  error[stage]: msg``   — ``regalloc`` (no location)

Each test pins one branch of the parser down so a future regression in
either the regex or the column math surfaces here.

Run with ``python -m cubyte_lsp.tests.test_analyzer`` from the ``lsp/``
dir.
"""

from __future__ import annotations

import sys
from pathlib import Path

# Allow running the test from the lsp/ directory without installing.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from cubyte_lsp.analyzer import _parse_stderr  # noqa: E402
from cubyte_lsp.protocol.types import Diagnostic, Position, Range  # noqa: E402


# Shared buffer for most tests. Three lines, indices 0 / 1 / 2.
# Line 1 contains ``nope`` at columns 4..8 so the typecheck
# "variable 'nope' has not been declared" case can locate it.
_BUFFER = "x := 1\nlet nope = 2\nfoo + bar\n"


def _d(
    line: int,
    start: int,
    end: int,
    message: str,
    code: str,
) -> Diagnostic:
    """Build the expected Diagnostic in one line."""
    return Diagnostic(
        range=Range(
            start=Position(line=line, character=start),
            end=Position(line=line, character=end),
        ),
        message=message,
        severity=1,  # DiagnosticSeverity.Error
        source="cubyte",
        code=code,
    )


def test_pattern_a_with_col() -> None:
    """``parse`` reports the exact column; squiggle is one char wide."""
    stderr = "buf.cbyte:1:5: error[parse]: expected ':=', got '='\n"
    diags = _parse_stderr(stderr, _BUFFER)
    assert diags == [_d(0, 4, 5, "expected ':=', got '='", "parse")]


def test_pattern_b_no_col_typecheck_with_quoted_id() -> None:
    """``typecheck`` with no col: locate the quoted identifier in the line."""
    stderr = "buf.cbyte:2: error[typecheck]: variable 'nope' has not been declared\n"
    diags = _parse_stderr(stderr, _BUFFER)
    # 'nope' starts at column 4 of "let nope = 2".
    assert diags == [_d(1, 4, 8, "variable 'nope' has not been declared", "typecheck")]


def test_pattern_b_no_col_no_quoted_id() -> None:
    """``typecheck`` with no col and no quoted id: squiggle the whole line."""
    stderr = "buf.cbyte:3: error[typecheck]: apply expected alg but got int\n"
    diags = _parse_stderr(stderr, _BUFFER)
    # Line 2 of the buffer is "foo + bar" (len 9).
    assert diags == [_d(2, 0, 9, "apply expected alg but got int", "typecheck")]


def test_pattern_c_no_location() -> None:
    """``regalloc`` has no source location: fall back to the first line.

    Cubyte doesn't report a line for ``regalloc`` errors; we surface the
    message anchored to the start of the buffer (whole first line) so
    editors show *something* rather than nothing.
    """
    stderr = "error[regalloc]: no register with order >= 105 available\n"
    diags = _parse_stderr(stderr, _BUFFER)
    # Line 0 is "x := 1" (len 6) — the whole line is squiggled.
    assert diags == [_d(0, 0, 6, "no register with order >= 105 available", "regalloc")]


def test_multiple_lines() -> None:
    """Multiple diagnostic lines produce diagnostics in input order."""
    stderr = (
        "buf.cbyte:1:5: error[parse]: expected ':=', got '='\n"
        "buf.cbyte:2: error[typecheck]: variable 'nope' has not been declared\n"
    )
    diags = _parse_stderr(stderr, _BUFFER)
    assert diags == [
        _d(0, 4, 5, "expected ':=', got '='", "parse"),
        _d(1, 4, 8, "variable 'nope' has not been declared", "typecheck"),
    ]


def test_unrelated_lines_ignored() -> None:
    """Non-matching stderr lines are silently dropped."""
    stderr = (
        "warning: something\n"
        "buf.cbyte:1:1: error[lexer]: bad\n"
        "note: trailing\n"
    )
    diags = _parse_stderr(stderr, _BUFFER)
    assert diags == [_d(0, 0, 1, "bad", "lexer")]


def test_leading_whitespace_tolerated() -> None:
    """cubyte sometimes prefixes lines with whitespace; the regex tolerates it."""
    stderr = "  buf.cbyte:1:1: error[lexer]: x\n"
    diags = _parse_stderr(stderr, _BUFFER)
    assert diags == [_d(0, 0, 1, "x", "lexer")]


def test_tempfile_path_ignored() -> None:
    """The ``file`` portion is the analyzer's own tempfile path; col still drives the squiggle."""
    stderr = "/tmp/cubyte-lsp-XXXXXX/buf.cbyte:1:17: error[parse]: expected ':='\n"
    diags = _parse_stderr(stderr, _BUFFER)
    # Col 17 is past end of line 0 ("x := 1", len 6), so the col clamps
    # to 5 (the last char '1'); _end_of_token advances to 6.
    assert diags == [_d(0, 5, 6, "expected ':='", "parse")]


def test_col_past_end_of_line_clamps() -> None:
    """A column past end-of-line is clamped; the squiggle still has width."""
    stderr = "buf.cbyte:1:99: error[parse]: x\n"
    diags = _parse_stderr(stderr, _BUFFER)
    # Line 0 is "x := 1" (len 6). Clamped col -> 5 (last char).
    # '1' at index 5 is not whitespace; _end_of_token advances to 6.
    assert diags == [_d(0, 5, 6, "x", "parse")]


def test_empty_stderr() -> None:
    """Empty stderr means no diagnostics, no crash."""
    assert _parse_stderr("", _BUFFER) == []


def test_1based_to_0based_line_and_col() -> None:
    """Both line and col are 1-based in cubyte and 0-based in LSP."""
    stderr = "buf.cbyte:1:1: error[lexer]: x\n"
    diags = _parse_stderr(stderr, _BUFFER)
    assert diags == [_d(0, 0, 1, "x", "lexer")]


def test_quoted_id_not_on_target_line_falls_back_to_whole_line() -> None:
    """If the named identifier isn't on the line, fall back to the whole line."""
    stderr = "buf.cbyte:3: error[typecheck]: variable 'nope' has not been declared\n"
    diags = _parse_stderr(stderr, _BUFFER)
    # Line 2 is "foo + bar" — no 'nope' there.
    assert diags == [_d(2, 0, 9, "variable 'nope' has not been declared", "typecheck")]


def test_tab_counts_as_one_char() -> None:
    """cubyte counts tabs as a single column; we mirror that."""
    buffer = "\t@bad\n"
    stderr = "buf.cbyte:1:2: error[lexer]: Malformed token at column 2: @\n"
    diags = _parse_stderr(stderr, buffer)
    # col 2 -> 0-based 1 ('@'). '_end_of_token' advances past the
    # contiguous non-whitespace run `@bad` to index 5.
    assert diags == [_d(0, 1, 5, "Malformed token at column 2: @", "lexer")]


def test_line_past_end_of_buffer_clamps() -> None:
    """A line number past the buffer end is clamped to the last line."""
    stderr = "buf.cbyte:99:1: error[parse]: x\n"
    diags = _parse_stderr(stderr, _BUFFER)
    # Last line is "foo + bar" (len 9). col 1 -> 0-based 0 ('f').
    # _end_of_token advances past the contiguous run "foo" to index 3.
    assert diags == [_d(2, 0, 3, "x", "parse")]


if __name__ == "__main__":
    test_pattern_a_with_col()
    test_pattern_b_no_col_typecheck_with_quoted_id()
    test_pattern_b_no_col_no_quoted_id()
    test_pattern_c_no_location()
    test_multiple_lines()
    test_unrelated_lines_ignored()
    test_leading_whitespace_tolerated()
    test_tempfile_path_ignored()
    test_col_past_end_of_line_clamps()
    test_empty_stderr()
    test_1based_to_0based_line_and_col()
    test_quoted_id_not_on_target_line_falls_back_to_whole_line()
    test_tab_counts_as_one_char()
    test_line_past_end_of_buffer_clamps()
    print("ok")