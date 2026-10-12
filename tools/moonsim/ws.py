"""Just enough RFC 6455 for the simulator, standard library only.

serve() runs moonraker's port: a websocket upgrade becomes a Conn for the
handler, any other request gets http(path) -> (status, headers, body).
connect() is the client side the helper scripts use.
"""
import asyncio
import base64
import hashlib
import json
import os
import struct

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
REASONS = {101: "Switching Protocols", 200: "OK", 404: "Not Found", 503: "Service Unavailable"}


class Closed(Exception):
    pass


class Conn:
    def __init__(self, reader, writer, client):
        self.reader, self.writer, self.client = reader, writer, client
        self.remote_address = writer.get_extra_info("peername")
        self.lock = asyncio.Lock()

    async def _frame(self, opcode, data):
        head = bytes([0x80 | opcode])
        mask = 0x80 if self.client else 0           # clients must mask, servers must not
        n = len(data)
        if n < 126:
            head += bytes([mask | n])
        elif n < 1 << 16:
            head += bytes([mask | 126]) + struct.pack(">H", n)
        else:
            head += bytes([mask | 127]) + struct.pack(">Q", n)
        if self.client:
            key = os.urandom(4)
            head += key
            data = bytes(b ^ key[i % 4] for i, b in enumerate(data))
        async with self.lock:
            self.writer.write(head + data)
            await self.writer.drain()

    async def send(self, text):
        try:
            await self._frame(0x1, text.encode())
        except (ConnectionError, RuntimeError) as e:
            raise Closed() from e

    async def recv(self):
        """The next text message; Closed when the other side goes away."""
        parts = []
        try:
            while True:
                b1, b2 = await self.reader.readexactly(2)
                n = b2 & 0x7F
                if n == 126:
                    n = struct.unpack(">H", await self.reader.readexactly(2))[0]
                elif n == 127:
                    n = struct.unpack(">Q", await self.reader.readexactly(8))[0]
                key = await self.reader.readexactly(4) if b2 & 0x80 else None
                data = await self.reader.readexactly(n)
                if key:
                    data = bytes(b ^ key[i % 4] for i, b in enumerate(data))
                op = b1 & 0x0F
                if op == 0x8:
                    raise Closed()
                if op == 0x9:
                    await self._frame(0xA, data)
                elif op in (0x0, 0x1, 0x2):
                    parts.append(data)
                    if b1 & 0x80:
                        return b"".join(parts).decode("utf-8", "replace")
        except (asyncio.IncompleteReadError, ConnectionError) as e:
            raise Closed() from e

    def __aiter__(self):
        return self

    async def __anext__(self):
        try:
            return await self.recv()
        except Closed:
            raise StopAsyncIteration

    async def close(self):
        try:
            await self._frame(0x8, b"")
        except Exception:
            pass
        self.writer.close()


async def _read_head(reader):
    lines = (await reader.readuntil(b"\r\n\r\n")).decode("latin-1").split("\r\n")
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()
    return lines[0], headers


async def serve(handler, http, host, port):
    async def accept(reader, writer):
        try:
            request, headers = await _read_head(reader)
            path = request.split(" ")[1]
        except (asyncio.IncompleteReadError, asyncio.LimitOverrunError, IndexError, ConnectionError):
            writer.close()
            return
        reply = await http(path)
        if reply is None and headers.get("upgrade", "").lower() == "websocket":
            accept_key = base64.b64encode(hashlib.sha1(
                (headers.get("sec-websocket-key", "") + GUID).encode()).digest()).decode()
            writer.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                          "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n" % accept_key).encode())
            await writer.drain()
            await handler(Conn(reader, writer, client=False))
            writer.close()
            return
        status, extra, body = reply or (404, [], b"not found\n")
        head = "HTTP/1.1 %d %s\r\nContent-Length: %d\r\nConnection: close\r\n" % (
            status, REASONS.get(status, ""), len(body))
        head += "".join("%s: %s\r\n" % kv for kv in extra)
        writer.write(head.encode() + b"\r\n" + body)
        await writer.drain()
        writer.close()

    return await asyncio.start_server(accept, host, port, limit=1 << 16)


async def connect(port, host="127.0.0.1", path="/websocket"):
    reader, writer = await asyncio.open_connection(host, port, limit=1 << 24)
    key = base64.b64encode(os.urandom(16)).decode()
    writer.write(("GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                  "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (path, host, port, key)).encode())
    status, _ = await _read_head(reader)
    if " 101 " not in status:
        writer.close()
        raise ConnectionError(status)
    return Conn(reader, writer, client=True)


async def call(port, method, params=None):
    """One json-rpc request, its reply (notifications in between are skipped)."""
    conn = await connect(port)
    try:
        await conn.send(json.dumps({"jsonrpc": "2.0", "method": method, "params": params or {}, "id": 1}))
        while True:
            msg = json.loads(await conn.recv())
            if msg.get("id") == 1:
                return msg
    finally:
        await conn.close()
