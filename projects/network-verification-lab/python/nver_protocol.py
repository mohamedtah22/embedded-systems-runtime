"""Binary UDP protocol shared by the traffic generator and black-box tests.

Wire format (network byte order):
  magic:u32 version:u8 kind:u8 flags:u16 session:u32 seq:u32
  payload_len:u16 reserved:u16 crc32:u32 payload:bytes
CRC covers bytes [0:20] + payload, excluding the CRC field itself.
"""
from __future__ import annotations

import json
import socket
import struct
import zlib
from dataclasses import dataclass

MAGIC = 0x4E564552  # NVER
VERSION = 1
MAX_PAYLOAD = 1400
DATA, ACK, STATS_REQUEST, STATS_REPLY, RESET_REQUEST, RESET_REPLY = range(1, 7)
HEADER = struct.Struct("!IBBHIIHHI")
HEADER_SIZE = HEADER.size


@dataclass(frozen=True)
class Packet:
    kind: int
    session: int
    sequence: int
    payload: bytes = b""
    flags: int = 0
    reserved: int = 0
    version: int = VERSION
    magic: int = MAGIC

    def encode(self) -> bytes:
        if not 0 <= len(self.payload) <= 65535:
            raise ValueError("payload size does not fit uint16")
        prefix = struct.pack("!IBBHIIHH", self.magic, self.version, self.kind,
                             self.flags, self.session, self.sequence,
                             len(self.payload), self.reserved)
        crc = zlib.crc32(prefix + self.payload) & 0xFFFFFFFF
        return prefix + struct.pack("!I", crc) + self.payload

    @classmethod
    def decode(cls, wire: bytes) -> "Packet":
        if len(wire) < HEADER_SIZE:
            raise ValueError("truncated header")
        magic, version, kind, flags, session, seq, length, reserved, crc = HEADER.unpack_from(wire)
        if len(wire) != HEADER_SIZE + length:
            raise ValueError("length mismatch")
        if zlib.crc32(wire[:20] + wire[HEADER_SIZE:]) & 0xFFFFFFFF != crc:
            raise ValueError("CRC mismatch")
        if magic != MAGIC or version != VERSION:
            raise ValueError("unexpected protocol version/magic")
        return cls(kind=kind, session=session, sequence=seq,
                   payload=wire[HEADER_SIZE:], flags=flags,
                   reserved=reserved, version=version, magic=magic)


def transact(address: tuple[str, int], request: Packet | bytes,
             timeout: float = 0.35) -> Packet:
    wire = request.encode() if isinstance(request, Packet) else request
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout)
        sock.sendto(wire, address)
        response, _ = sock.recvfrom(65535)
    return Packet.decode(response)


def stats(address: tuple[str, int]) -> dict[str, int]:
    reply = transact(address, Packet(kind=STATS_REQUEST, session=0, sequence=0))
    if reply.kind != STATS_REPLY:
        raise ValueError(f"expected stats reply, got {reply.kind}")
    return json.loads(reply.payload)


def reset(address: tuple[str, int]) -> None:
    reply = transact(address, Packet(kind=RESET_REQUEST, session=0, sequence=0))
    if reply.kind != RESET_REPLY:
        raise ValueError(f"expected reset reply, got {reply.kind}")
