# cubyte-lsp

A Language Server Protocol implementation for [cubyte](../README.md), the
high-level language whose runtime is a Rubik's cube. The server speaks LSP
3.17 over stdin/stdout and provides:

- **Diagnostics** — the server shells out to the `cubyte` compiler and
  converts its `file:line:col: error[stage]: message` stderr output
  into LSP diagnostics published on `textDocument/publishDiagnostics`.
  `lexer` and `parse` errors squiggle the offending token;
  `typecheck` errors squiggle the identifier named in the message
  (or the whole line if none is named); `regalloc` errors appear in
  the Problems panel and gutter but the compiler doesn't report a
  source location for them.
- **Hover** — shows the type, required register order, and (when available)
  the physical register the variable is bound to, plus a short description
  of built-in pieces, keywords, and the I/O register.
- **Completion** — completes keywords, type names, piece labels
  (`UF, UFL, DBR, …`), and identifiers visible in the current document.
- **Go-to-definition / document symbols** — jumps to the declaration of a
  variable, function-free label, or algorithm literal.
- **Formatting** — a minimal, opinionated formatter that re-indents blocks
  and aligns `:=` in a column. It is safe to call on unsaved buffers
  because it operates on a string in/out.

## Layout

```
lsp/
├── server.py           # Entry point: starts the LSP, dispatches requests.
├── analyzer.py         # Re-runs the cubyte compiler, parses diagnostics.
├── features/
│   ├── __init__.py
│   ├── completion.py   # textDocument/completion
│   ├── definition.py   # textDocument/definition
│   ├── diagnostics.py  # Background publishDiagnostics loop
│   ├── formatting.py   # textDocument/formatting
│   ├── hover.py        # textDocument/hover
│   └── symbols.py      # textDocument/documentSymbol
├── knowledge/
│   ├── __init__.py
│   ├── keywords.py     # Built-in keyword metadata
│   ├── pieces.py       # Piece-label metadata
│   └── types.py        # Type + register-order metadata
├── protocol/
│   ├── __init__.py
│   ├── jsonrpc.py      # Minimal stdlib LSP/JSON-RPC framing
│   └── types.py        # Request/response dataclasses
├── utils.py            # URI <-> path, range helpers
├── pyproject.toml      # `python -m cubyte_lsp` entry point
└── tests/
    └── test_jsonrpc.py # Round-trip framing test (placeholder)
```

## Running

The server expects the `cubyte` binary on `PATH` (or set `CUBYTE_BIN`).
It is meant to be launched by an editor client, not by hand:

```bash
python -m cubyte_lsp             # default: read PATH for cubyte
CUBYTE_BIN=/path/to/cubyte \
  python -m cubyte_lsp           # explicit path
```

### Editor configuration

`neovim` (with `nvim-lspconfig`):

```lua
require('lspconfig').cubyte_lsp.setup{
  cmd = { 'python', '-m', 'cubyte_lsp' },
  filetypes = { 'cubyte' },
  root_dir = function(fname) return vim.fs.dirname(fname) end,
}
```

`vscode`: see `editors/vscode-cubyte/README.md` (out of scope for this
skeleton — left to whoever wires the extension together).

## Status

This is a skeleton: the JSON-RPC framing, document-state plumbing, and
feature modules are in place, but only diagnostics and hover are wired
through to real behaviour. Completion, definition, symbols, and
formatting ship with sensible stub responses that the editor will
display; flesh them out by filling in the marked `TODO(stub)` blocks in
`features/*.py`.
