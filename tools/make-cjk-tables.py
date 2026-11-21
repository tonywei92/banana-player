#!/usr/bin/env python3
"""Writes src/cjk/{cp932,gbk,big5}.bin: double-byte -> Unicode tables for src/tagtext.cpp, so the player decodes old
tag text itself (BananaOS's glibc has no iconv modules for these). Each is uint16 LE, rows = lead 0x81..0xFE, columns =
trail 0x40..0xFE, 0 = no such character (only BMP characters), then zlib-compressed. Run from anywhere."""
import zlib, os
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "cjk")
os.makedirs(out, exist_ok=True)
for name in ("cp932", "gbk", "big5"):
	raw = bytearray()
	for lead in range(0x81, 0xFF):
		for trail in range(0x40, 0xFF):
			try:
				s = bytes([lead, trail]).decode(name)
				cp = ord(s) if len(s) == 1 and ord(s) < 0x10000 else 0
			except UnicodeDecodeError:
				cp = 0
			raw += cp.to_bytes(2, "little")
	z = zlib.compress(bytes(raw), 9)
	open(os.path.join(out, name + ".bin"), "wb").write(z)
	print(name, len(raw), "->", len(z))
