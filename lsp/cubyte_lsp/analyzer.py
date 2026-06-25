"""cubyte_lsp.analyzer — runs the cubyte compiler and parses its output.

The compiler is the source of truth for diagnostics. We invoke it on a
temporary copy of the in-memory buffer (the editor's text might not yet
be saved to disk) and translate its ``file:line:col: error[stage]: msg``
stderr output into LSP :class:`Diagnostic` objects.

Three shapes actually appear on stderr:

* ``file:line:col: error[stage]: msg`` — ``lexer`` and ``parse`` stages
  report the exact offending column.
* ``file:line: error[stage]: msg``     — ``typecheck`` usually omits the
  column; if the message names a token in single quotes (e.g.
  ``variable 'nope' has not been declared``) we locate it on the line
  to set the squiggle. Otherwise the whole line is highlighted.
* ``error[stage]: msg``                — ``regalloc`` errors come without
  any source location; we anchor them to the first line of the buffer
  so editors surface them in the Problems panel and on the gutter.

The diagnostic ``severity`` is always ``Error``; the ``code`` is the
stage name lower-cased so the editor can group by stage.
"""

from __future__ import annotations

import asyncio
import logging
import os
import re
import shutil
import tempfile
from dataclasses import dataclass
from typing import Optional

from .protocol.types import Diagnostic, Position, Range

log = logging.getLogger("cubyte_lsp.analyzer")

# The leading ``file:line:col:`` preamble is fully optional and each
# piece within it is independently optional — that is what lets one
# pattern cover all three real shapes (with-col, no-col, no-location).
# The ``file`` capture is the analyzer's own tempfile path; we ignore it.
_DIAG_RE = re.compile(
    r"""
    ^\s*
    (?:(?P<file>[^:\s]+?)
       (?::(?P<line>\d+))?
       (?::(?P<col>\d+))?
       :\s+)?
    error\[(?P<stage>[a-zA-Z]+)\]:\s*
    (?P<msg>.*)$
    """,
    re.VERBOSE,
)


@dataclass(frozen=True)
class AnalysisResult:
    diagnostics: tuple[Diagnostic, ...]
    raw_stderr: str
    raw_stdout: str
    exit_code: int


class CubyteAnalyzer:
    """Invokes the ``cubyte`` binary on a buffer and reports diagnostics.

    The analyzer writes the buffer to a temporary ``.cbyte`` file because
    the compiler expects a real path (it derives intermediate filenames
    from it). The file is cleaned up after the run; the analyzer never
    touches the editor's on-disk file.
    """

    def __init__(self, binary: Optional[str] = None) -> None:
        self._binary = binary or os.environ.get("CUBYTE_BIN") or shutil.which("cubyte")

    @property
    def available(self) -> bool:
        return self._binary is not None

    async def analyze(self, uri: str, text: str) -> AnalysisResult:
        """Run the compiler on ``text`` and return its diagnostics."""
        if not self.available:
            log.warning("cubyte binary not found on PATH and CUBYTE_BIN not set")
            return AnalysisResult((), "", "", -1)

        # The compiler derives ``<stem>-pp.cbyte`` and ``<stem>.cubin`` from
        # the input path; passing the full ``.cbyte`` filename lets those
        # siblings land in the same tmp directory we own.
        with tempfile.TemporaryDirectory(prefix="cubyte-lsp-") as tmp:
            src_path = os.path.join(tmp, "buf.cbyte")
            with open(src_path, "w", encoding="utf-8") as f:
                f.write(text)

            cmd = [self._binary, src_path, os.path.join(tmp, "buf.cubin")]
            try:
                proc = await asyncio.create_subprocess_exec(
                    *cmd,
                    stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.PIPE,
                )
                stdout, stderr = await proc.communicate()
            except FileNotFoundError:
                log.error("cubyte binary %s could not be executed", self._binary)
                return AnalysisResult((), "", "", -1)

            stderr_text = stderr.decode("utf-8", errors="replace")
            stdout_text = stdout.decode("utf-8", errors="replace")
            diagnostics = _parse_stderr(stderr_text, text)
            return AnalysisResult(
                diagnostics=tuple(diagnostics),
                raw_stderr=stderr_text,
                raw_stdout=stdout_text,
                exit_code=proc.returncode if proc.returncode is not None else -1,
            )


def _parse_stderr(stderr: str, buffer_text: str) -> list[Diagnostic]:
    """Translate cubyte's stderr into LSP diagnostics.

    ``buffer_text`` is needed because ``typecheck`` errors usually omit
    the column; we then look for a token named in the message (e.g.
    ``'nope'``) to anchor the squiggle.
    """
    buffer_lines = buffer_text.splitlines()
    diagnostics: list[Diagnostic] = []

    for raw in stderr.splitlines():
        m = _DIAG_RE.match(raw)
        if not m:
            continue
        # ``file`` is the analyzer's own tempfile path — never the
        # user's file — so we deliberately ignore it.
        line_str = m.group("line")
        col_str = m.group("col")
        stage = m.group("stage")
        msg = m.group("msg")

        if line_str is not None:
            line_no = max(0, min(int(line_str) - 1, len(buffer_lines) - 1))
            line_text = buffer_lines[line_no] if buffer_lines else ""
        else:
            # No location (e.g. ``error[regalloc]: …``). Anchor at the
            # start of the buffer; the editor will surface it in both
            # the Problems panel and the gutter.
            line_no = 0
            line_text = buffer_lines[0] if buffer_lines else ""

        if col_str is not None:
            start = max(0, min(int(col_str) - 1, max(len(line_text) - 1, 0)))
            end = _end_of_token(line_text, start)
        else:
            quoted = _find_quoted_in_line(line_text, msg)
            if quoted is not None:
                start, end = quoted
            else:
                start, end = 0, len(line_text)

        diagnostics.append(Diagnostic(
            range=Range(
                start=Position(line=line_no, character=start),
                end=Position(line=line_no, character=end),
            ),
            message=msg,
            severity=1,  # DiagnosticSeverity.Error
            source="cubyte",
            code=stage.lower(),
        ))
    return diagnostics


def _end_of_token(line_text: str, start: int) -> int:
    """Return the column one past the end of the token starting at *start*.

    A token runs until whitespace or end-of-line. If *start* is past the
    end of the line or already on whitespace, return ``start + 1``
    (clamped to ``len(line_text)``) so the squiggle has at least one
    character of width.
    """
    if start >= len(line_text):
        return len(line_text)
    i = start
    while i < len(line_text) and not line_text[i].isspace():
        i += 1
    if i == start:
        return min(len(line_text), start + 1)
    return i


def _find_quoted_in_line(line_text: str, msg: str) -> Optional[tuple[int, int]]:
    """Locate the first single-quoted token from *msg* inside *line_text*.

    cubyte's typecheck messages often name the offending identifier in
    single quotes (``variable 'nope' has not been declared``); pointing
    the squiggle at the same identifier in the buffer makes the error
    much easier to read. Returns ``None`` if no quoted token appears on
    the line — the caller falls back to the whole line.
    """
    for ident in re.findall(r"'([^']+)'", msg):
        idx = line_text.find(ident)
        if idx != -1:
            return idx, idx + len(ident)
    return None