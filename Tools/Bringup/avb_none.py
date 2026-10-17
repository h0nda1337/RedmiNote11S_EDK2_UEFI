#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Original bounded AVB hash-footer writer for unsigned laboratory boot images.

Implements data layouts from the public AVB header/footer/hash-descriptor
specification. It neither creates an OEM signature nor alters phone vbmeta.
The owner-authored property reader is reused independently for preservation.
"""
import hashlib
import re
import struct

from own_avb_properties import properties


def aligned(value, boundary):
    return (value + boundary - 1) // boundary * boundary


def verify(image, require_unsigned=True):
    if len(image) < 4096 or len(image) > 512 * 1024 * 1024:
        raise ValueError("Invalid partition image length")
    magic, major, minor, original, offset, size = struct.unpack_from(">4sIIQQQ", image, len(image)-64)
    if magic != b"AVBf" or major != 1 or minor != 0 or not 0 < original <= offset:
        raise ValueError("Invalid AVB footer")
    if offset % 4096 or size < 256 or size > 1024 * 1024 or offset + size > len(image)-64:
        raise ValueError("AVB footer points outside the bounded metadata region")
    if any(image[-28:]):
        raise ValueError("Nonzero AVB footer reserved bytes")
    header = image[offset:offset+256]
    if header[:4] != b"AVB0" or struct.unpack_from(">II", header, 4) != (1, 0):
        raise ValueError("Unsupported vbmeta header")
    auth, aux = struct.unpack_from(">QQ", header, 12)
    algorithm = struct.unpack_from(">I", header, 28)[0]
    begin, length = struct.unpack_from(">QQ", header, 96)
    if auth % 64 or aux % 64 or 256 + auth + aux != size or begin + length > aux:
        raise ValueError("Invalid AVB metadata block layout")
    if require_unsigned and (algorithm != 0 or auth != 0 or any(header[32:96]) or any(header[112:128])):
        raise ValueError("Expected unsigned laboratory metadata with no verification-disable flags")
    position = offset + 256 + auth + begin
    end = position + length
    hashes = []
    while position < end:
        if position + 16 > end:
            raise ValueError("Truncated AVB descriptor")
        tag, following = struct.unpack_from(">QQ", image, position)
        total = 16 + following
        if following % 8 or total > end-position:
            raise ValueError("Invalid AVB descriptor bounds")
        if tag == 2:
            if total < 132:
                raise ValueError("Truncated AVB hash descriptor")
            image_size = struct.unpack_from(">Q", image, position+16)[0]
            algorithm_name = image[position+24:position+56]
            name_len, salt_len, digest_len, flags = struct.unpack_from(">4I", image, position+56)
            used = 132 + name_len + salt_len + digest_len
            if not 0 < name_len <= 64 or salt_len > 64 or digest_len != 32 or flags != 0:
                raise ValueError("Unsupported AVB hash parameters")
            if used > total or aligned(used, 8) != total or image_size != original:
                raise ValueError("Invalid AVB hash payload bounds")
            if algorithm_name != b"sha256" + bytes(26) or any(image[position+72:position+132]):
                raise ValueError("Unexpected hash algorithm or reserved bytes")
            start = position+132
            name = image[start:start+name_len]
            salt = image[start+name_len:start+name_len+salt_len]
            expected = image[start+name_len+salt_len:start+used-132]
            # Descriptor tail must remain zero padding, never executable content.
            if any(image[position+used:position+total]):
                raise ValueError("Nonzero hash descriptor padding")
            if name != b"boot" or hashlib.sha256(salt + image[:image_size]).digest() != expected:
                raise ValueError("Boot payload digest mismatch")
            hashes.append({"partition": "boot", "image_size": image_size, "sha256": expected.hex()})
        elif tag != 0:
            raise ValueError("Unsupported AVB descriptor type")
        position += total
    if len(hashes) != 1:
        raise ValueError("Expected exactly one boot hash descriptor")
    return {"unsigned": algorithm == 0, "properties": properties(image), **hashes[0]}


def encode(payload, partition_size, retained):
    if not payload or not 4096 <= partition_size <= 512 * 1024 * 1024 or partition_size % 4096:
        raise ValueError("Invalid payload or partition geometry")
    descriptors = bytearray()
    for key, value in sorted(retained.items()):
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,256}", key) or "\0" in value:
            raise ValueError("Invalid retained property")
        name, data = key.encode("ascii"), value.encode("utf8")
        if len(data) > 4096:
            raise ValueError("Excessive retained property")
        body = struct.pack(">QQ", len(name), len(data)) + name + b"\0" + data + b"\0"
        body += bytes(aligned(len(body), 8)-len(body))
        descriptors.extend(struct.pack(">QQ", 0, len(body)) + body)
    digest = hashlib.sha256(payload).digest()
    body = struct.pack(">Q32s4I60x", len(payload), b"sha256", 4, 0, 32, 0) + b"boot" + digest
    body += bytes(aligned(len(body), 8)-len(body))
    descriptors.extend(struct.pack(">QQ", 2, len(body)) + body)
    auxiliary = bytes(descriptors) + bytes(aligned(len(descriptors), 64)-len(descriptors))
    header = bytearray(256)
    struct.pack_into(">4sIIQQI", header, 0, b"AVB0", 1, 0, 0, len(auxiliary), 0)
    struct.pack_into(">QQ", header, 96, 0, len(descriptors))
    release = b"miel own laboratory container 0.1\0"
    header[128:128+len(release)] = release
    metadata = bytes(header) + auxiliary
    offset = aligned(len(payload), 4096)
    if offset + len(metadata) > partition_size-4096:
        raise ValueError("Image and metadata exceed partition capacity")
    footer = struct.pack(">4sIIQQQ28x", b"AVBf", 1, 0, len(payload), offset, len(metadata))
    result = payload + bytes(offset-len(payload)) + metadata
    result += bytes(partition_size-64-len(result)) + footer
    checked = verify(result)
    if checked["properties"] != retained:
        raise ValueError("Retained properties changed")
    return result


def add_footer(payload, stock):
    original = verify(stock, require_unsigned=False)
    return encode(payload, len(stock), original["properties"])
