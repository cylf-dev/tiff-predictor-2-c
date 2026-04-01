"""Embed a chonkle:signature custom section into a .wasm binary.

Vendored from chonkle (https://github.com/cylf-dev/chonkle)
src/chonkle/wasm_signature.py — pure Python, no external dependencies.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

SECTION_NAME = "chonkle:signature"

_WASM_MAGIC = b"\x00asm"
_HEADER_SIZE = 8  # magic (4) + version (4)


def _read_leb128(data: bytes, offset: int) -> tuple[int, int]:
    """Decode an unsigned LEB128 value, returning (value, new_offset)."""
    result = 0
    shift = 0
    while True:
        byte = data[offset]
        offset += 1
        result |= (byte & 0x7F) << shift
        if (byte & 0x80) == 0:
            break
        shift += 7
    return result, offset


def _encode_leb128(value: int) -> bytes:
    """Encode *value* as unsigned LEB128."""
    buf = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value != 0:
            byte |= 0x80
        buf.append(byte)
        if value == 0:
            break
    return bytes(buf)


def _strip_section(data: bytes, section_name: str) -> bytes:
    """Return *data* with any custom section named *section_name* removed."""
    result = bytearray(data[:_HEADER_SIZE])
    offset = _HEADER_SIZE

    while offset < len(data):
        section_start = offset
        section_id = data[offset]
        offset += 1
        section_size, offset = _read_leb128(data, offset)
        section_end = offset + section_size

        keep = True
        if section_id == 0:
            name_len, name_start = _read_leb128(data, offset)
            name = data[name_start : name_start + name_len].decode("utf-8")
            if name == section_name:
                keep = False

        if keep:
            result.extend(data[section_start:section_end])

        offset = section_end

    return bytes(result)


def _build_custom_section(name: str, payload: bytes) -> bytes:
    """Build a Wasm custom section (id=0) with *name* and *payload*."""
    name_bytes = name.encode("utf-8")
    name_field = _encode_leb128(len(name_bytes)) + name_bytes
    section_body = name_field + payload
    return b"\x00" + _encode_leb128(len(section_body)) + section_body


def read_signature_bytes(data: bytes, *, context: str = "<bytes>") -> dict[str, Any]:
    """Read the chonkle:signature custom section from in-memory Wasm bytes."""
    if len(data) < _HEADER_SIZE or data[:4] != _WASM_MAGIC:
        msg = f"Not a valid Wasm binary: {context}"
        raise ValueError(msg)

    offset = _HEADER_SIZE

    while offset < len(data):
        section_id = data[offset]
        offset += 1
        section_size, offset = _read_leb128(data, offset)
        section_end = offset + section_size

        if section_id == 0:  # custom section
            name_len, name_start = _read_leb128(data, offset)
            name = data[name_start : name_start + name_len].decode("utf-8")
            payload_start = name_start + name_len

            if name == SECTION_NAME:
                payload = data[payload_start:section_end]
                return json.loads(payload)

        offset = section_end

    msg = f"No {SECTION_NAME!r} custom section in {context}"
    raise ValueError(msg)


def embed_signature(wasm_bytes: bytes, signature: dict[str, Any]) -> bytes:
    """Append a chonkle:signature custom section to Wasm bytes.

    If a chonkle:signature section already exists, it is replaced.
    """
    if len(wasm_bytes) < _HEADER_SIZE or wasm_bytes[:4] != _WASM_MAGIC:
        msg = "Not a valid Wasm binary"
        raise ValueError(msg)

    stripped = _strip_section(wasm_bytes, SECTION_NAME)
    payload = json.dumps(signature, separators=(",", ":")).encode("utf-8")
    section = _build_custom_section(SECTION_NAME, payload)
    return stripped + section


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <wasm_file> <signature_json>", file=sys.stderr)
        sys.exit(1)

    wasm_path = Path(sys.argv[1])
    sig_path = Path(sys.argv[2])

    wasm_bytes = wasm_path.read_bytes()
    signature = json.loads(sig_path.read_text())

    result = embed_signature(wasm_bytes, signature)

    # Verify round-trip before writing.
    read_back = read_signature_bytes(result, context=str(wasm_path))
    if read_back != signature:
        print("ERROR: round-trip verification failed", file=sys.stderr)
        sys.exit(1)

    wasm_path.write_bytes(result)
    section_size = len(result) - len(wasm_bytes)
    print(
        f"Embedded {len(json.dumps(signature, separators=(',', ':')))} byte"
        f" {SECTION_NAME} section in {wasm_path} (verified)"
    )
