"""cubyte_lsp.features — per-LSP-method implementations.

Each module exports a single coroutine that takes the
:class:`cubyte_lsp.protocol.jsonrpc.JsonRpcHandler` params dict and the
open :class:`~cubyte_lsp.documents.Document` (or documents) and returns
the JSON-serialisable LSP response body.

The convention is:

    async def feature_<name>(params: dict, doc: Document) -> dict | list | None

``server.py`` wires these into JSON-RPC handlers. The feature modules
never touch the wire directly.
"""

from . import completion, definition, diagnostics, formatting, hover, symbols  # noqa: F401