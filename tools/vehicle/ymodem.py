# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Thalles Campagnani
"""Minimal YMODEM sender (1 KiB blocks, CRC-16) for the SPL receiver."""

import pathlib
import time
from typing import Callable

SOH, STX, EOT, ACK, NAK, CAN = 0x01, 0x02, 0x04, 0x06, 0x15, 0x18
CRC = ord("C")

Getc = Callable[[float], int | None]


def crc16(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc


def _block(seq: int, payload: bytes, size: int) -> bytes:
    data = payload.ljust(size, b"\x1a" if seq else b"\0")
    head = bytes((STX if size == 1024 else SOH, seq & 0xFF, ~seq & 0xFF))
    return head + data + crc16(data).to_bytes(2, "big")


def _wait(getc: Getc, want: set[int], timeout: float) -> int | None:
    """First byte from want; CAN, or nothing before timeout, gives None."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        c = getc(deadline - time.time())
        if c is None or c == CAN:
            return None
        if c in want:
            return c
    return None


def _send(write: Callable[[bytes], object], getc: Getc, blk: bytes,
          tries: int, timeout: float) -> bool:
    for _ in range(tries):
        write(blk)
        c = _wait(getc, {ACK, NAK}, timeout)
        if c == ACK:
            return True
        if c is None:
            return False
    return False


def send(path: pathlib.Path, write: Callable[[bytes], object], getc: Getc,
         progress: Callable[[int, int], None] | None = None,
         start_timeout: float = 60, tries: int = 10, timeout: float = 10) -> bool:
    data = path.read_bytes()
    if _wait(getc, {CRC}, start_timeout) is None:
        return False
    header = path.name.encode() + b"\0" + str(len(data)).encode() + b"\0"
    if not _send(write, getc, _block(0, header, 128), tries, timeout):
        return False
    if _wait(getc, {CRC}, timeout) is None:
        return False
    for seq, off in enumerate(range(0, len(data), 1024), start=1):
        if not _send(write, getc, _block(seq, data[off:off + 1024], 1024),
                     tries, timeout):
            return False
        if progress:
            progress(min(off + 1024, len(data)), len(data))
    for _ in range(tries):
        write(bytes((EOT,)))
        c = _wait(getc, {ACK, NAK}, timeout)
        if c == ACK:
            break
        if c is None:
            return False
    else:
        return False
    if _wait(getc, {CRC}, timeout) is None:
        return False
    return _send(write, getc, _block(0, b"", 128), tries, timeout)
