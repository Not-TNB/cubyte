"""cubyte_lsp — Language Server Protocol for cubyte.

Entry point: ``python -m cubyte_lsp`` (see ``server.py``).

The package is organised so that each LSP feature lives in its own module
under :mod:`cubyte_lsp.features`. The package itself only re-exports
public names; importing submodules directly is supported.
"""

__all__ = ["server", "analyzer"]
