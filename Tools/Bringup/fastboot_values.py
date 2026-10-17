#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Parse Fastboot size variables, whose representation is hexadecimal."""
import re


def hex_size(text):
    if not re.fullmatch(r"(?:0[xX])?[0-9a-fA-F]{1,16}", text):
        raise ValueError("Malformed hexadecimal Fastboot size")
    return int(text, 16)
