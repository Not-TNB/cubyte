"""Misc helpers used by feature modules."""

from __future__ import annotations

from urllib.parse import quote, unquote

from .protocol.types import Position, Range


def uri_to_path(uri: str) -> str:
    """Convert a ``file://`` URI to a local path.

    The LSP spec mandates URIs; ``file:///home/foo/bar.cbyte`` →
    ``/home/foo/bar.cbyte``. Handles percent-encoding and Windows drive
    letters, even though cubyte currently only runs on POSIX for
    compilation.
    """
    if not uri.startswith("file://"):
        return uri  # already a path (some clients send raw paths)

    raw = uri[len("file://"):]
    # Strip a leading slash before drive letters on Windows-style paths.
    # We don't try to detect the host; an empty host is the local machine.
    path = unquote(raw)
    if len(path) >= 2 and path[0] == "/" and path[2] == ":":
        path = path[1:]
    return path


def path_to_uri(path: str) -> str:
    return "file://" + quote(path, safe="/:")


def position_at(text: str, offset: int) -> Position:
    """Translate a 0-based character offset into an LSP ``Position``."""
    line = text.count("\n", 0, offset)
    last_nl = text.rfind("\n", 0, offset)
    column = offset - (last_nl + 1) if last_nl != -1 else offset
    return Position(line=line, character=column)


def whole_line_range(line_index: int, text: str) -> Range:
    """Build a Range that covers the whole ``line_index`` line."""
    start = Position(line=line_index, character=0)
    # 1-based on the right edge — clients clamp this safely.
    end_line_end = text.find("\n", _line_start_offset(text, line_index))
    if end_line_end == -1:
        end_char = len(text) - _line_start_offset(text, line_index)
    else:
        end_char = end_line_end - _line_start_offset(text, line_index)
    return Range(start=start, end=Position(line=line_index, character=end_char))


def _line_start_offset(text: str, line_index: int) -> int:
    if line_index == 0:
        return 0
    offset = -1
    for _ in range(line_index):
        offset = text.find("\n", offset + 1)
        if offset == -1:
            return len(text)
    return offset + 1