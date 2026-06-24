"""Minimal LSP-flavoured JSON-RPC 2.0 framing over stdio.

We implement just enough of the spec for the cubyte LSP:

* ``Content-Length: N\\r\\n\\r\\n<N bytes of UTF-8 JSON>`` framing on
  stdin/stdout.
* Single-reader / single-writer task pair reading from stdin and
  dispatching to a :class:`~cubyte_lsp.protocol.jsonrpc.JsonRpcHandler`.

The framing is intentionally separate from message semantics: nothing
here knows what a "hover" is. That lives in :mod:`cubyte_lsp.server`.
"""

from __future__ import annotations

import asyncio
import json
import logging
from dataclasses import dataclass
from typing import Any, Awaitable, Callable, Optional

log = logging.getLogger("cubyte_lsp.jsonrpc")

CONTENT_LENGTH = "Content-Length: "
HEADER_TERMINATOR = b"\r\n\r\n"


class ProtocolError(Exception):
    """Raised when the wire protocol is malformed."""


@dataclass
class Message:
    """A decoded JSON-RPC message.

    ``id`` is ``None`` for notifications (per JSON-RPC 2.0).
    """

    method: str
    params: Any
    id: Optional[int | str] = None


# A handler is an async function taking (params, message_id). For
# notifications ``id`` will be None and the return value is ignored.
Handler = Callable[[Any, Optional[int | str]], Awaitable[Optional[Any]]]


class JsonRpcHandler:
    """Dispatches incoming messages by ``method`` to a registered handler.

    Register a handler with :meth:`on`; unknown methods produce a
    ``Method not found`` error response. Notifications (no ``id``) never
    produce a response, even on handler exceptions — we log them instead.
    """

    def __init__(self) -> None:
        self._handlers: dict[str, Handler] = {}
        # The server fills this in once the client sends ``initialize``.
        # We do not gate handlers on it; LSP clients send ``initialized``
        # asynchronously and we just want to keep going.
        self.initialized: bool = False

    # ------------------------------------------------------------------
    # registration
    # ------------------------------------------------------------------

    def on(self, method: str, handler: Handler) -> None:
        self._handlers[method] = handler

    # ------------------------------------------------------------------
    # decoding
    # ------------------------------------------------------------------

    @staticmethod
    async def _read_headers(stream: asyncio.StreamReader) -> dict[str, str]:
        """Read the ``Header: value\r\n`` block up to the empty line.

        Returns the headers in lower-case-keyed form. Raises
        :class:`ProtocolError` on EOF before the terminator.

        ``StreamReader.readline()`` is a coroutine, so the whole loop
        must be async. The test shim provides a sync ``readline`` that
        returns bytes directly, so the call site is the same shape as
        in the production reader.
        """
        headers: dict[str, str] = {}
        while True:
            line = await stream.readline()
            if not line:  # EOF
                raise ProtocolError("EOF in header block")
            line = line.rstrip(b"\r\n")
            if not line:
                return headers
            try:
                name, _, value = line.decode("ascii").partition(":")
            except UnicodeDecodeError as e:
                raise ProtocolError(f"non-ASCII header line: {line!r}") from e
            headers[name.strip().lower()] = value.strip()

    @staticmethod
    async def read_message(stream: asyncio.StreamReader) -> Optional[Message]:
        """Return the next framed message, or ``None`` on clean EOF."""
        headers = await JsonRpcHandler._read_headers(stream)
        if not headers:
            return None  # EOF before any header

        length_str = headers.get("content-length")
        if length_str is None:
            raise ProtocolError("missing Content-Length header")
        try:
            length = int(length_str)
        except ValueError as e:
            raise ProtocolError(f"bad Content-Length {length_str!r}") from e
        if length < 0:
            raise ProtocolError(f"negative Content-Length {length}")

        body = await stream.readexactly(length)
        try:
            payload = json.loads(body)
        except json.JSONDecodeError as e:
            raise ProtocolError(f"invalid JSON body: {e}") from e

        if not isinstance(payload, dict):
            raise ProtocolError("top-level JSON value must be an object")

        try:
            method = payload["method"]
        except KeyError as e:
            raise ProtocolError("message missing 'method'") from e
        return Message(
            method=method,
            params=payload.get("params"),
            id=payload.get("id"),
        )

    # ------------------------------------------------------------------
    # dispatch loop
    # ------------------------------------------------------------------

    async def serve(self, stream_in: asyncio.StreamReader) -> None:
        """Run the dispatch loop until EOF or :class:`ProtocolError`."""
        while True:
            try:
                msg = await JsonRpcHandler.read_message(stream_in)
            except ProtocolError as e:
                log.error("protocol error: %s — exiting", e)
                return
            if msg is None:
                log.info("client closed stdin — exiting")
                return
            asyncio.create_task(self._dispatch(msg))

    async def _dispatch(self, msg: Message) -> None:
        handler = self._handlers.get(msg.method)
        if handler is None:
            if msg.id is not None:
                await self._send_error(msg.id, -32601, f"Method not found: {msg.method}")
            else:
                log.warning("ignoring notification with no handler: %s", msg.method)
            return

        try:
            result = await handler(msg.params, msg.id)
        except Exception:  # noqa: BLE001 — LSP clients need to see this
            log.exception("handler for %s raised", msg.method)
            if msg.id is not None:
                await self._send_error(msg.id, -32603, "internal error")
            return

        if msg.id is not None:
            await self._send_result(msg.id, result)

    # ------------------------------------------------------------------
    # output — called by feature handlers via the writer passed to
    # ``server.start()``. We expose them here so the handler can be used
    # without the rest of the server if you want to embed the LSP in
    # something else.
    # ------------------------------------------------------------------

    @staticmethod
    def encode_message(payload: dict[str, Any]) -> bytes:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        header = f"{CONTENT_LENGTH}{len(body)}\r\n\r\n".encode("ascii")
        return header + body

    async def _send_result(self, msg_id: int | str, result: Any) -> None:
        await self._write(self.encode_message({"jsonrpc": "2.0", "id": msg_id, "result": result}))

    async def _send_error(self, msg_id: int | str, code: int, message: str) -> None:
        await self._write(self.encode_message({
            "jsonrpc": "2.0",
            "id": msg_id,
            "error": {"code": code, "message": message},
        }))

    async def send_notification(self, method: str, params: Any) -> None:
        await self._write(self.encode_message({"jsonrpc": "2.0", "method": method, "params": params}))

    # The default writer is the stdout stream registered by server.start().
    _writer: Optional[asyncio.StreamWriter] = None

    def set_writer(self, writer: asyncio.StreamWriter) -> None:
        self._writer = writer

    async def _write(self, data: bytes) -> None:
        if self._writer is None:
            raise RuntimeError("JsonRpcHandler.set_writer was not called")
        self._writer.write(data)
        await self._writer.drain()
