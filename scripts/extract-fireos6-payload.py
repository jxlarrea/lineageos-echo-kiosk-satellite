#!/usr/bin/env python3
"""Reconstruct a full Android update payload partition with the stdlib only."""

import argparse
import bz2
import hashlib
import lzma
import os
import struct
import sys
import zipfile
from pathlib import Path

REPLACE = 0
REPLACE_BZ = 1
REPLACE_XZ = 8


def read_varint(data, offset):
    value = shift = 0
    while True:
        if offset >= len(data):
            raise ValueError("truncated protobuf varint")
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, offset
        shift += 7
        if shift > 63:
            raise ValueError("protobuf varint is too large")


def fields(data):
    """Return (field number, wire type, value) tuples for a protobuf message."""
    offset = 0
    result = []
    while offset < len(data):
        tag, offset = read_varint(data, offset)
        number, wire_type = tag >> 3, tag & 7
        if wire_type == 0:
            value, offset = read_varint(data, offset)
        elif wire_type == 1:
            value, offset = data[offset : offset + 8], offset + 8
        elif wire_type == 2:
            size, offset = read_varint(data, offset)
            value, offset = data[offset : offset + size], offset + size
        elif wire_type == 5:
            value, offset = data[offset : offset + 4], offset + 4
        else:
            raise ValueError("unsupported protobuf wire type %d" % wire_type)
        if offset > len(data):
            raise ValueError("truncated protobuf field")
        result.append((number, wire_type, value))
    return result


def values(data, number):
    return [value for field, _, value in fields(data) if field == number]


def one(data, number, default=None):
    found = values(data, number)
    return found[0] if found else default


def partition(payload, name):
    if payload[:4] != b"CrAU":
        raise ValueError("payload.bin is missing the CrAU header")
    if len(payload) < 20:
        raise ValueError("payload.bin header is truncated")

    version, manifest_size = struct.unpack(">QQ", payload[4:20])
    metadata_signature_size = 0
    header_size = 20
    if version >= 2:
        if len(payload) < 24:
            raise ValueError("payload.bin v2 header is truncated")
        metadata_signature_size = struct.unpack(">I", payload[20:24])[0]
        header_size = 24

    manifest_end = header_size + manifest_size
    data_offset = manifest_end + metadata_signature_size
    if data_offset > len(payload):
        raise ValueError("payload.bin manifest extends beyond the archive")

    manifest = payload[header_size:manifest_end]
    block_size = one(manifest, 3, 4096)
    for update in values(manifest, 13):
        partition_name = one(update, 1, b"").decode("utf-8")
        if partition_name == name:
            return block_size, data_offset, update
    raise ValueError("partition not found in payload: %s" % name)


def write_extents(output, data, extents, block_size):
    cursor = 0
    for extent in extents:
        start = one(extent, 1, 0)
        blocks = one(extent, 2, 0)
        size = blocks * block_size
        if cursor + size > len(data):
            raise ValueError("operation data is shorter than its destination extents")
        output.seek(start * block_size)
        output.write(data[cursor : cursor + size])
        cursor += size
    if cursor != len(data):
        raise ValueError("operation data is longer than its destination extents")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.digest()


def reconstruct(ota, partition_name, destination):
    with zipfile.ZipFile(ota) as archive:
        payload = archive.read("payload.bin")

    block_size, data_offset, update = partition(payload, partition_name)
    partition_info = one(update, 7, b"")
    output_size = one(partition_info, 1)
    expected_hash = one(partition_info, 2, b"")
    if output_size is None:
        raise ValueError("payload does not declare the new partition size")

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(".%s.partial" % destination.name)
    try:
        with temporary.open("w+b") as output:
            output.truncate(output_size)
            for index, operation in enumerate(values(update, 8)):
                operation_type = one(operation, 1)
                blob_offset = one(operation, 2)
                blob_size = one(operation, 3)
                extents = values(operation, 6)
                if operation_type not in (REPLACE, REPLACE_BZ, REPLACE_XZ):
                    raise ValueError("unsupported operation %d at index %d" % (operation_type, index))
                if blob_offset is None or blob_size is None:
                    raise ValueError("replace operation %d has no data" % index)
                start = data_offset + blob_offset
                encoded = payload[start : start + blob_size]
                if len(encoded) != blob_size:
                    raise ValueError("replace operation %d exceeds payload.bin" % index)
                if operation_type == REPLACE_BZ:
                    data = bz2.decompress(encoded)
                elif operation_type == REPLACE_XZ:
                    data = lzma.decompress(encoded)
                else:
                    data = encoded
                write_extents(output, data, extents, block_size)

        actual_hash = sha256(temporary)
        if expected_hash and actual_hash != expected_hash:
            raise ValueError("reconstructed %s SHA-256 does not match payload metadata" % partition_name)
        os.replace(temporary, destination)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise

    print("Reconstructed %s (%d bytes, sha256 %s)" % (
        destination, output_size, sha256(destination).hex()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ota", type=Path, help="full OTA ZIP containing payload.bin")
    parser.add_argument("partition", help="partition name, for example system")
    parser.add_argument("output", type=Path, help="reconstructed raw partition image")
    args = parser.parse_args()
    reconstruct(args.ota, args.partition, args.output)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, zipfile.BadZipFile, lzma.LZMAError) as error:
        print("ERROR: %s" % error, file=sys.stderr)
        sys.exit(1)
