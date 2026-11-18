#!/usr/bin/env python3
import os
import sys
sys.stdout.write("Content-Type: text/plain\r\n\r\n")
sys.stdout.write("QUERY_STRING=" + os.environ.get("QUERY_STRING", "") + "\n")
sys.stdout.write("REQUEST_METHOD=" + os.environ.get("REQUEST_METHOD", "") + "\n")
sys.stdout.write("CONTENT_LENGTH=" + os.environ.get("CONTENT_LENGTH", "") + "\n")
