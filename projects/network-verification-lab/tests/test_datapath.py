"""Integration tests against the compiled C++ network process."""
import socket
import struct

import pytest

from nver_protocol import (
    ACK, DATA, RESET_REPLY, RESET_REQUEST, STATS_REPLY, STATS_REQUEST,
    Packet, reset, stats, transact,
)
from conftest import wait_counter


def send_and_expect_drop(address, wire):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.settimeout(0.055)
        s.sendto(wire, address)
        with pytest.raises(socket.timeout):
            s.recvfrom(65535)


@pytest.mark.parametrize("payload", [b"", b"A", b"hi there", b"x" * 255, b"z" * 1400])
def test_payload_roundtrip(server, payload):
    packet = Packet(DATA, 1, 10, payload)
    reply = transact(server, packet)
    assert reply.kind == ACK
    assert reply.payload == payload
    assert (reply.session, reply.sequence) == (1, 10)
    record = stats(server)
    assert record["accepted"] == 1
    assert record["bytes_accepted"] == len(payload)


def test_binary_payload_including_nulls(server):
    payload = bytes(range(256)) + b"\x00\xff"
    assert transact(server, Packet(DATA, 14, 1, payload)).payload == payload


def test_sequence_increases(server):
    for sequence in [0, 1, 10, 2**31, 2**32 - 1]:
        assert transact(server, Packet(DATA, 3, sequence)).kind == ACK
    assert stats(server)["accepted"] == 5


def test_duplicate_sequence_dropped(server):
    packet = Packet(DATA, 3, 1, b"first")
    transact(server, packet)
    send_and_expect_drop(server, packet.encode())
    assert wait_counter(server, "replay_errors", 1)["accepted"] == 1


def test_older_sequence_dropped(server):
    transact(server, Packet(DATA, 3, 100))
    send_and_expect_drop(server, Packet(DATA, 3, 99).encode())
    assert wait_counter(server, "replay_errors", 1)["invalid"] == 1


def test_session_sequence_spaces_are_independent(server):
    for session in (1, 2, 4294967295):
        assert transact(server, Packet(DATA, session, 1)).kind == ACK
    record = stats(server)
    assert record["sessions"] == 3
    assert record["accepted"] == 3


@pytest.mark.parametrize("bad_packet, counter", [
    (Packet(DATA, 1, 1, b"x", magic=0x12345678), "header_errors"),
    (Packet(DATA, 1, 1, b"x", version=2), "header_errors"),
    (Packet(DATA, 1, 1, b"x", flags=1), "header_errors"),
    (Packet(DATA, 1, 1, b"x", reserved=1), "header_errors"),
    (Packet(DATA, 1, 1, b"x" * 1401), "length_errors"),
    (Packet(99, 0, 0), "type_errors"),
    (Packet(STATS_REQUEST, 1, 0), "type_errors"),
    (Packet(STATS_REQUEST, 0, 0, b"unauthorized"), "type_errors"),
    (Packet(ACK, 1, 1), "type_errors"),
])
def test_invalid_valid_crc_packets_are_rejected(server, bad_packet, counter):
    send_and_expect_drop(server, bad_packet.encode())
    assert wait_counter(server, counter, 1)["invalid"] == 1


@pytest.mark.parametrize("wire", [b"", b"\x01", b"NVER\x01", b"\x00" * 23])
def test_truncated_header(server, wire):
    send_and_expect_drop(server, wire)
    assert wait_counter(server, "length_errors", 1)["accepted"] == 0


def test_truncated_payload(server):
    wire = Packet(DATA, 1, 1, b"hello").encode()
    send_and_expect_drop(server, wire[:-1])
    assert wait_counter(server, "length_errors", 1)["accepted"] == 0


def test_false_length_field(server):
    wire = bytearray(Packet(DATA, 1, 1, b"hello").encode())
    wire[16:18] = struct.pack("!H", 90)
    send_and_expect_drop(server, bytes(wire))
    assert wait_counter(server, "length_errors", 1)["accepted"] == 0


def test_corrupted_payload_crc(server):
    wire = bytearray(Packet(DATA, 1, 1, b"hello").encode())
    wire[-1] ^= 0xAB
    send_and_expect_drop(server, bytes(wire))
    assert wait_counter(server, "crc_errors", 1)["accepted"] == 0


def test_corrupted_crc_field(server):
    wire = bytearray(Packet(DATA, 1, 1, b"hello").encode())
    wire[20] ^= 1
    send_and_expect_drop(server, bytes(wire))
    assert wait_counter(server, "crc_errors", 1)["invalid"] == 1


def test_stats_are_machine_readable(server):
    record = stats(server)
    assert record == {
        "datagrams": 1, "accepted": 0, "invalid": 0,
        "crc_errors": 0, "length_errors": 0, "replay_errors": 0,
        "header_errors": 0, "type_errors": 0, "session_errors": 0, "bytes_accepted": 0, "sessions": 0,
    }
    assert record["datagrams"] == record["accepted"] + record["invalid"] + 1


def test_reset_clears_counters_and_replay_history(server):
    transact(server, Packet(DATA, 5, 5, b"hello"))
    assert stats(server)["accepted"] == 1
    assert transact(server, Packet(RESET_REQUEST, 0, 0)).kind == RESET_REPLY
    assert stats(server)["accepted"] == 0
    assert stats(server)["sessions"] == 0
    assert transact(server, Packet(DATA, 5, 5)).kind == ACK


def test_decoder_rejects_bad_crc():
    wire = bytearray(Packet(DATA, 1, 1, b"payload").encode())
    wire[23] ^= 0x11
    with pytest.raises(ValueError, match="CRC mismatch"):
        Packet.decode(wire)


def test_decoder_rejects_short_header():
    with pytest.raises(ValueError, match="truncated header"):
        Packet.decode(b"short")


def test_decoder_rejects_bad_length():
    wire = Packet(DATA, 1, 1, b"payload").encode()[:-1]
    with pytest.raises(ValueError, match="length mismatch"):
        Packet.decode(wire)


def test_reset_helper(server):
    transact(server, Packet(DATA, 1, 1))
    reset(server)
    assert stats(server)["accepted"] == 0



def test_sequence_wraparound_and_old_packet(server):
    for seq in [2**32-2, 2**32-1, 0, 1]:
        assert transact(server, Packet(DATA, 9, seq)).kind == ACK
    send_and_expect_drop(server, Packet(DATA, 9, 2**32-1).encode())
    record = wait_counter(server, "replay_errors", 1)
    assert record["accepted"] == 4


def test_ambiguous_half_range_sequence_is_rejected(server):
    transact(server, Packet(DATA, 9, 0))
    send_and_expect_drop(server, Packet(DATA, 9, 2**31).encode())
    assert wait_counter(server, "replay_errors", 1)["accepted"] == 1


def test_session_state_is_bounded_and_reset_releases_capacity(server):
    for session in range(64):
        assert transact(server, Packet(DATA, session, 1)).kind == ACK
    send_and_expect_drop(server, Packet(DATA, 64, 1).encode())
    record = wait_counter(server, "session_errors", 1)
    assert record["sessions"] == 64
    assert record["accepted"] == 64
    assert transact(server, Packet(DATA, 0, 2)).kind == ACK
    reset(server)
    assert transact(server, Packet(DATA, 64, 1)).kind == ACK


@pytest.mark.parametrize("port", ["12oops", "-1", "65536"])
def test_cli_rejects_invalid_port(port, compile_datapath):
    import subprocess
    from conftest import BINARY
    result = subprocess.run([str(BINARY), "--port", port], capture_output=True, text=True, timeout=2)
    assert result.returncode == 2
    assert "invalid port" in result.stderr
