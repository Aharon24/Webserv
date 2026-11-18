#!/usr/bin/env python3
import os
import sys
length_text = os.environ.get("CONTENT_LENGTH", "0") or "0"
try:
    length = int(length_text)
except ValueError:
    length = 0
data = sys.stdin.buffer.read(length)
sys.stdout.buffer.write(b"Content-Type: application/octet-stream\r\n\r\n")
sys.stdout.buffer.write(data)
