"""Repeatable negative traffic, session isolation, and concurrent clients."""
import random
import socket
from concurrent.futures import ThreadPoolExecutor

from nver_protocol import ACK, DATA, Packet, stats, transact
from conftest import wait_counter


def test_concurrent_sessions_have_no_cross_talk(server):
    def work(session):
        for seq in range(1, 16):
            payload = f"client={session};seq={seq}".encode()
            result = transact(server, Packet(DATA, session, seq, payload), timeout=1.0)
            assert (result.kind, result.session, result.sequence, result.payload) == (
                ACK, session, seq, payload
            )
    with ThreadPoolExecutor(max_workers=6) as pool:
        list(pool.map(work, range(1, 7)))
    record = stats(server)
    assert record["accepted"] == 90
    assert record["sessions"] == 6
    assert record["invalid"] == 0


def test_deterministic_fuzz_corruptions_do_not_crash_server(server):
    rng = random.Random(20261013)
    samples = []
    for seq in range(100):
        wire = bytearray(Packet(DATA, 1, seq, bytes(range(64))).encode())
        # Flip only the payload so header lengths are still correct; CRC must fail.
        index = rng.randint(24, len(wire)-1)
        wire[index] ^= 1 << rng.randrange(8)
        samples.append(bytes(wire))
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        for wire in samples:
            s.sendto(wire, server)
    record = wait_counter(server, "crc_errors", 100)
    assert record["accepted"] == 0
    assert record["invalid"] == 100
    assert transact(server, Packet(DATA, 1, 1, b"healthy")).kind == ACK


def test_protocol_is_binary_and_endian_correct(server):
    reply = transact(server, Packet(DATA, 0x12345678, 0x87654321, b"binary"))
    assert (reply.session, reply.sequence) == (0x12345678, 0x87654321)
    assert reply.payload == b"binary"
    assert stats(server)["bytes_accepted"] == len(b"binary")
