"""cubyte_lsp.analyzer — runs the cubyte compiler and parses its output.

The compiler is the source of truth for diagnostics. We invoke it on a
temporary copy of the in-memory buffer (the editor's text might not yet
be saved to disk) and translate its ``[stage] line N: message`` stderr
output into LSP :class:`Diagnostic` objects.

The diagnostic mapping table is intentionally explicit: the cubyte exit
code determines the LSP ``severity`` (``Error`` for everything) and the
stage name becomes the diagnostic ``code`` so the editor can group them.
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

# Stages reported by cubyte on stderr. The first capture group is the
# stage name; the second is the line number (1-based per the compiler,
# which we convert to 0-based for LSP). Many compiler errors come out as
# ``<Stage> error at line N, column M: message``; we accept that form too.
_STAGE_RE = re.compile(
    r"^(?:"
    r"\[(?P<stage_bracketed>\w+)\]\s+line\s+(?P<line1>\d+)\s*:\s*(?P<msg1>.*)$"
    r"|"
    r"(?P<stage_plain>\w+)\s+error\s+at\s+line\s+(?P<line2>\d+)\s*,"
    r"\s*column\s+(?P<col>\d+)\s*:\s*(?P<msg2>.*)$"
    r")"
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
            diagnostics = _parse_stderr(stderr_text)
            return AnalysisResult(
                diagnostics=tuple(diagnostics),
                raw_stderr=stderr_text,
                raw_stdout=stdout_text,
                exit_code=proc.returncode if proc.returncode is not None else -1,
            )


def _parse_stderr(stderr: str) -> list[Diagnostic]:
    diagnostics: list[Diagnostic] = []
    for line in stderr.splitlines():
        m = _STAGE_RE.match(line)
        if not m:
            continue
        if m.group("stage_bracketed") is not None:
            stage = m.group("stage_bracketed")
            line_no = int(m.group("line1"))
            col = 0
            message = m.group("msg1").strip()
        else:
            stage = m.group("stage_plain")
            line_no = int(m.group("line2"))
            col = max(0, int(m.group("col")) - 1)
            message = m.group("msg2").strip()

        if line_no <= 0:
            range_ = Range(start=Position(0, 0), end=Position(0, 0))
        else:
            # 0-based for LSP.
            end_char = max(col + 1, len(message))
            range_ = Range(
                start=Position(line=line_no - 1, character=col),
                end=Position(line=line_no - 1, character=end_char),
            )
        diagnostics.append(Diagnostic(
            range=range_,
            message=message,
            severity=1,  # DiagnosticSeverity.Error
            source="cubyte",
            code=stage.lower(),
        ))
    return diagnostics