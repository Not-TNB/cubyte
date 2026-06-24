"""Smoke test for the JSON-RPC framing.

We round-trip a few messages through :class:`JsonRpcHandler._encode_message`
and the corresponding read loop to make sure the framing and payload
format are valid against a minimal ``StreamReader`` shim.

Run with ``python -m cubyte_lsp.tests.test_jsonrpc`` from the ``lsp/`` dir.
"""

from __future__ import annotations

import asyncio
import sys
from pathlib import Path

# Allow running the test from the lsp/ directory without installing.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from cubyte_lsp.protocol.jsonrpc import JsonRpcHandler  # noqa: E402


class _FakeStreamReader:
    def __init__(self, data: bytes) -> None:
        self._data = data
        self._pos = 0

    async def readexactly(self, n: int) -> bytes:
        chunk = self._data[self._pos:self._pos + n]
        if len(chunk) < n:
            raise asyncio.IncompleteReadError(chunk, n)
        self._pos += n
        return chunk

    async def readline(self) -> bytes:
        # Mirror ``asyncio.StreamReader.readline``'s coroutine shape —
        # the production reader's ``readline`` is a coroutine, so the
        # call site must be able to ``await`` it.
        end = self._data.find(b"\n", self._pos)
        if end == -1:
            line, self._pos = self._data[self._pos:], len(self._data)
            return line
        line, self._pos = self._data[self._pos:end + 1], end + 1
        return line


def test_encode_then_read() -> None:
    payload = {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"x": 1}}
    encoded = JsonRpcHandler.encode_message(payload)

    async def _go() -> None:
        reader = _FakeStreamReader(encoded)
        msg = await JsonRpcHandler.read_message(reader)
        assert msg is not None
        assert msg.method == "initialize"
        assert msg.id == 1
        assert msg.params == {"x": 1}

    asyncio.run(_go())


def test_read_handles_two_consecutive() -> None:
    a = JsonRpcHandler.encode_message({"jsonrpc": "2.0", "id": 1, "method": "a", "params": None})
    b = JsonRpcHandler.encode_message({"jsonrpc": "2.0", "method": "notify", "params": {"k": "v"}})
    combined = a + b

    async def _go() -> None:
        reader = _FakeStreamReader(combined)
        m1 = await JsonRpcHandler.read_message(reader)
        m2 = await JsonRpcHandler.read_message(reader)
        assert m1 is not None and m2 is not None
        assert m1.method == "a" and m1.id == 1
        assert m2.method == "notify" and m2.id is None
        assert m2.params == {"k": "v"}

    asyncio.run(_go())


if __name__ == "__main__":
    test_encode_then_read()
    test_read_handles_two_consecutive()
    print("ok")