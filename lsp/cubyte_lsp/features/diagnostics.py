"""Diagnostics.

Runs the analyzer in the background whenever a document changes and
publishes the results via ``textDocument/publishDiagnostics``.

We use ``asyncio.create_task`` so the editor stays responsive while the
compiler runs. The :class:`~cubyte_lsp.documents.DocumentStore`
generation counter is consulted before pushing — a late result for an
older buffer is dropped on the floor.
"""

from __future__ import annotations

import asyncio
import logging
import uuid

from ..analyzer import CubyteAnalyzer
from ..protocol.jsonrpc import JsonRpcHandler

log = logging.getLogger("cubyte_lsp.diagnostics")


class DiagnosticPublisher:
    """Schedule analysis runs and publish the results."""

    def __init__(self, rpc: JsonRpcHandler, analyzer: CubyteAnalyzer) -> None:
        self._rpc = rpc
        self._analyzer = analyzer
        # Generation counter so old runs can be discarded.
        self._counters: dict[str, int] = {}

    def schedule(self, doc) -> None:
        """Schedule an analysis for ``doc``.

        Multiple calls collapse: if a run is already in flight we let it
        finish, then re-schedule. This keeps the editor responsive
        during fast typing bursts.
        """
        gen = doc.generation
        self._counters[doc.uri] = gen
        asyncio.create_task(self._run(doc, gen))

    async def _run(self, doc, generation: int) -> None:
        try:
            result = await self._analyzer.analyze(doc.uri, doc.text)
        except Exception:  # noqa: BLE001
            log.exception("analyzer crashed for %s", doc.uri)
            return

        # Bail if the document changed while we were running.
        if self._counters.get(doc.uri) != generation:
            log.debug("discarding stale diagnostics for %s", doc.uri)
            return

        doc.last_diagnostics = result.diagnostics
        await self._rpc.send_notification(
            "textDocument/publishDiagnostics",
            {
                "uri": doc.uri,
                "diagnostics": [d.to_dict() for d in result.diagnostics],
            },
        )