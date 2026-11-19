#!/usr/bin/python3
import os
import sys

body = sys.stdin.read()

print("Content-Type: text/plain")
print()
print("Method:", os.environ.get("REQUEST_METHOD"))
print("Content-Length:", os.environ.get("CONTENT_LENGTH"))
print("Body:", body)