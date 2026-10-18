"""Validate the bounded ZeroTier 1.x public world format, without exposing keys.

Format: ZeroTierOne 1.16.2 node/World.hpp, Identity.hpp, InetAddress.hpp.
This checks structure, not signing-key ownership or server reachability.
"""
import hashlib
import ipaddress
import json
import struct
import sys
from pathlib import Path


def inspect(path, expected_type):
    data = Path(path).read_bytes()
    if not 178 <= len(data) <= 16384:
        raise ValueError("World file length outside supported bounds")
    pos = 0

    def take(n):
        nonlocal pos
        if pos + n > len(data):
            raise ValueError("Truncated world file")
        value = data[pos:pos + n]
        pos += n
        return value

    kind = take(1)[0]
    if kind != expected_type:
        raise ValueError("Wrong world type")
    world_id, timestamp = struct.unpack(">QQ", take(16))
    if not world_id or not timestamp:
        raise ValueError("Empty world ID/revision")
    take(64 + 96)  # Public update key and world signature; never a private key.
    count = take(1)[0]
    if not 1 <= count <= 4:
        raise ValueError("Invalid root count")
    roots = []
    for _ in range(count):
        address = take(5).hex()
        if take(1) != b"\0":
            raise ValueError("Unsupported identity type")
        take(64)
        if take(1) != b"\0":
            raise ValueError("World identity contains private key material")
        endpoints = []
        endpoint_count = take(1)[0]
        if not 1 <= endpoint_count <= 32:
            raise ValueError("Missing/invalid stable endpoints")
        for _ in range(endpoint_count):
            family = take(1)[0]
            if family not in (4, 6):
                raise ValueError("Unsupported endpoint family")
            ip = str(ipaddress.ip_address(take(4 if family == 4 else 16)))
            port = struct.unpack(">H", take(2))[0]
            if not port:
                raise ValueError("Invalid endpoint port")
            endpoints.append(f"{ip}/{port}")
        roots.append({"address": address, "endpoints": endpoints})
    if kind == 127 and take(2) != b"\0\0":
        raise ValueError("World extensions not supported")
    if pos != len(data):
        raise ValueError("Unexpected trailing world data")
    return {"id": f"{world_id:016x}", "timestamp": timestamp,
            "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data), "roots": roots}


if __name__ == "__main__":
    print(json.dumps({"planet": inspect(sys.argv[1], 1), "moon": inspect(sys.argv[2], 127)}))
