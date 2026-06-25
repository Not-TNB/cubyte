"""LSP server entry point.

Wires the JSON-RPC handler to the feature modules and starts the
asyncio event loop reading from stdin. The server is intentionally
stateless across editor sessions: we do not write to disk, and the
in-memory :class:`DocumentStore` is rebuilt on every ``initialize``.

Run with ``python -m cubyte_lsp``.
"""

from __future__ import annotations

import asyncio
import json
import logging
import os
import signal
import sys
from typing import Any, Optional

from .analyzer import CubyteAnalyzer
from .documents import DocumentStore
from .features import (
    completion as feature_completion,
)
from .features import (
    definition as feature_definition,
)
from .features import (
    diagnostics as feature_diagnostics,
)
from .features import (
    formatting as feature_formatting,
)
from .features import (
    hover as feature_hover,
)
from .features import (
    symbols as feature_symbols,
)
from .protocol.jsonrpc import JsonRpcHandler
from .protocol.types import TextDocumentContentChangeEvent, TextDocumentItem
from .utils import uri_to_path

log = logging.getLogger("cubyte_lsp.server")

SERVER_NAME = "cubyte-lsp"
SERVER_VERSION = "0.5.0"


def _log_to_stderr(level: int = logging.INFO) -> None:
    """Send server logs to stderr so they don't pollute the LSP stream.

    Editors that capture child-process stderr (e.g. VSCode's Output
    panel) will see them. They are deliberately simple: one line per
    record, no fancy formatter.
    """
    handler = logging.StreamHandler(stream=sys.stderr)
    handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(name)s: %(message)s"))
    root = logging.getLogger()
    root.handlers.clear()
    root.addHandler(handler)
    root.setLevel(level)


class CubyteLanguageServer:
    """Owns the document store, analyzer, and RPC handler."""

    def __init__(self, analyzer: CubyteAnalyzer) -> None:
        self.documents = DocumentStore()
        self.analyzer = analyzer
        self.rpc = JsonRpcHandler()
        self.diagnostics = feature_diagnostics.DiagnosticPublisher(self.rpc, analyzer)
        self._register_handlers()

    # ------------------------------------------------------------------
    # registration
    # ------------------------------------------------------------------

    def _register_handlers(self) -> None:
        self.rpc.on("initialize", self._on_initialize)
        self.rpc.on("initialized", self._on_initialized)
        self.rpc.on("shutdown", self._on_shutdown)
        self.rpc.on("exit", self._on_exit)

        self.rpc.on("textDocument/didOpen", self._on_did_open)
        self.rpc.on("textDocument/didChange", self._on_did_change)
        self.rpc.on("textDocument/didClose", self._on_did_close)
        self.rpc.on("textDocument/didSave", self._on_did_save)

        # Feature requests.
        self.rpc.on("textDocument/hover", self._feature_hover)
        self.rpc.on("textDocument/completion", self._feature_completion)
        self.rpc.on("textDocument/definition", self._feature_definition)
        self.rpc.on("textDocument/documentSymbol", self._feature_symbols)
        self.rpc.on("textDocument/formatting", self._feature_formatting)

    # ------------------------------------------------------------------
    # lifecycle
    # ------------------------------------------------------------------

    async def _on_initialize(self, params: Any, msg_id) -> dict:
        log.info("initialize from %s", (params or {}).get("clientInfo"))
        self.rpc.initialized = True
        return {
            "capabilities": {
                "textDocumentSync": {
                    "openClose": True,
                    "change": 1,  # Full
                    "save": {"includeText": False},
                },
                "hoverProvider": True,
                "completionProvider": {
                    "resolveProvider": False,
                    "triggerCharacters": [":"],
                },
                "definitionProvider": True,
                "documentSymbolProvider": True,
                "documentFormattingProvider": True,
            },
            "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
        }

    async def _on_initialized(self, params: Any, msg_id) -> None:
        log.info("client initialised")
        return None

    async def _on_shutdown(self, params: Any, msg_id) -> None:
        log.info("shutdown requested")
        return None

    async def _on_exit(self, params: Any, msg_id) -> None:
        log.info("exit requested")
        # Per the spec we exit with code 0 if shutdown was called, 1 otherwise.
        sys.exit(0 if self.rpc.initialized else 1)

    # ------------------------------------------------------------------
    # document sync
    # ------------------------------------------------------------------

    async def _on_did_open(self, params: Any, msg_id) -> None:
        td = TextDocumentItem.from_dict(params["textDocument"])
        doc = self.documents.open(td.uri, td.version, td.text, td.language_id)
        self.diagnostics.schedule(doc)
        return None

    async def _on_did_change(self, params: Any, msg_id) -> None:
        td = params["textDocument"]
        uri = td["uri"]
        # We declared sync kind = 1 (Full), so changes is a list with one
        # full replacement.
        change = TextDocumentContentChangeEvent.from_dict(params["contentChanges"][-1])
        doc = self.documents.update(uri, int(td["version"]), change.text)
        if doc is not None:
            self.diagnostics.schedule(doc)
        return None

    async def _on_did_close(self, params: Any, msg_id) -> None:
        uri = params["textDocument"]["uri"]
        self.documents.close(uri)
        # Clear stale diagnostics so the editor does not leave them up.
        await self.rpc.send_notification(
            "textDocument/publishDiagnostics",
            {"uri": uri, "diagnostics": []},
        )
        return None

    async def _on_did_save(self, params: Any, msg_id) -> None:
        uri = params["textDocument"]["uri"]
        doc = self.documents.get(uri)
        if doc is not None:
            self.diagnostics.schedule(doc)
        return None

    # ------------------------------------------------------------------
    # feature dispatch
    # ------------------------------------------------------------------

    def _resolve_doc(self, params: Any):
        uri = params.get("textDocument", {}).get("uri")
        if not uri:
            return None
        return self.documents.get(uri)

    async def _feature_hover(self, params: Any, msg_id) -> Optional[dict]:
        doc = self._resolve_doc(params)
        if doc is None:
            return None
        return await feature_hover.hover(params, doc)

    async def _feature_completion(self, params: Any, msg_id) -> list[dict]:
        doc = self._resolve_doc(params)
        if doc is None:
            return []
        return await feature_completion.completion(params, doc)

    async def _feature_definition(self, params: Any, msg_id) -> Optional[list[dict]]:
        doc = self._resolve_doc(params)
        if doc is None:
            return None
        return await feature_definition.definition(params, doc)

    async def _feature_symbols(self, params: Any, msg_id) -> list[dict]:
        doc = self._resolve_doc(params)
        if doc is None:
            return []
        return await feature_symbols.document_symbols(params, doc)

    async def _feature_formatting(self, params: Any, msg_id) -> Optional[list[dict]]:
        doc = self._resolve_doc(params)
        if doc is None:
            return None
        return await feature_formatting.formatting(params, doc)


async def amain() -> None:
    _log_to_stderr()

    analyzer = CubyteAnalyzer()
    if not analyzer.available:
        log.warning("cubyte binary not on PATH — diagnostics will be empty")

    server = CubyteLanguageServer(analyzer)

    # Wire stdin/stdout to the JSON-RPC handler.
    loop = asyncio.get_running_loop()
    reader = asyncio.StreamReader(loop=loop)
    protocol = asyncio.StreamReaderProtocol(reader)
    await loop.connect_read_pipe(lambda: protocol, sys.stdin)

    writer_transport, writer_protocol = await loop.connect_write_pipe(
        asyncio.streams.FlowControlMixin, sys.stdout
    )
    writer = asyncio.StreamWriter(writer_transport, writer_protocol, reader, loop)
    server.rpc.set_writer(writer)

    # Stop the server gracefully on SIGTERM/SIGINT.
    for sig in (signal.SIGTERM, signal.SIGINT):
        loop.add_signal_handler(sig, lambda: asyncio.create_task(_shutdown(loop)))

    log.info("cubyte-lsp %s ready", SERVER_VERSION)
    await server.rpc.serve(reader)


async def _shutdown(loop: asyncio.AbstractEventLoop) -> None:
    log.info("shutting down")
    loop.stop()


def main() -> None:
    try:
        asyncio.run(amain())
    except KeyboardInterrupt:
        sys.exit(0)


if __name__ == "__main__":
    main()