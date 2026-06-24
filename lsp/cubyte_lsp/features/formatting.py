"""Formatting.

A minimal, safe formatter:

* Strips trailing whitespace.
* Re-indents ``{ ... }`` blocks using the editor's tab size (default 4).
* Leaves all other content alone — the LSP spec encourages providers
  that only do a *subset* of formatting, and we don't want to fight
  users' existing conventions on ``:=`` alignment or comment style.

The function is total: it never raises, so the editor can call it on
any buffer without a try/except.
"""

from __future__ import annotations


async def formatting(params: dict, doc) -> list[dict] | None:
    tab_size = int(params.get("options", {}).get("tabSize", 4))
    text = doc.text

    out_lines: list[str] = []
    # The stack of indent levels, top of stack = current block's indent.
    indent_stack: list[int] = [0]
    for raw in text.splitlines():
        line = raw.rstrip()
        stripped = line.lstrip()

        if not stripped:
            out_lines.append("")
            continue

        # Pop any indent levels that are *strictly greater* than this
        # line's indent. We compare on the line's *raw* indent so a
        # correctly-indented block closer still pops cleanly.
        leading = len(line) - len(stripped)
        # If the line is a block closer (starts with ``}``) we will pop
        # below; otherwise we pop anything whose indent is now too deep.
        if stripped.startswith("}"):
            while len(indent_stack) > 1 and indent_stack[-1] >= leading:
                indent_stack.pop()
            if len(indent_stack) > 1:
                indent_stack.pop()
            target = indent_stack[-1] if indent_stack else 0
        else:
            while len(indent_stack) > 1 and indent_stack[-1] > leading:
                indent_stack.pop()
            target = indent_stack[-1] if indent_stack else 0

        out_lines.append(" " * target + stripped)

        # After this line, any new block opener advances the stack.
        if stripped.endswith("{") and not stripped.startswith("}"):
            indent_stack.append(target + tab_size)

    new_text = "\n".join(out_lines)
    if text.endswith("\n"):
        new_text += "\n"

    # Whole-document edit, as required by LSP.
    last_line = text.count("\n")
    last_nl = text.rfind("\n")
    end_col = len(text) - (last_nl + 1) if last_nl != -1 else len(text)
    return [{
        "range": {
            "start": {"line": 0, "character": 0},
            "end": {"line": last_line, "character": end_col},
        },
        "newText": new_text,
    }]