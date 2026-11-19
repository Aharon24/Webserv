#!/usr/bin/env python3
"""Isolated HTTP upload regressions. Usage: python3 tests/post_upload_test.py --binary /path/to/webserv"""
import argparse
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest

BOUNDARY = b"WebservBoundary"
TYPE = b"multipart/form-data; boundary=" + BOUNDARY


def multipart(items, field=False, boundary=BOUNDARY):
    result = b""
    for name, data in items:
        disposition = b'Content-Disposition: form-data; name="file"'
        if not field:
            disposition += b'; filename="' + name.encode() + b'"'
        result += b"--" + boundary + b"\r\n" + disposition + b"\r\n\r\n" + data + b"\r\n"
    return result + b"--" + boundary + b"--\r\n"


class UploadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="webserv-post-tests-")
        cls.work = Path(cls.temp.name)
        cls.root = cls.work / "root"
        cls.uploads = cls.work / "uploads"
        cls.root.mkdir()
        cls.uploads.mkdir()
        (cls.root / "index.html").write_bytes(b"INDEX")
        cls.guard = cls.work / "outside"
        cls.guard.write_bytes(b"DO NOT CHANGE")
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            cls.port = reservation.getsockname()[1]
        config = cls.work / "test.conf"
        config.write_text(
            "server { listen 127.0.0.1:" + str(cls.port) + "; root " + str(cls.root)
            + "; client_max_body_size 4096;"
            " location /upload { upload on; upload_path " + str(cls.uploads) + "; allowed_methods POST; }"
            " location /off { upload off; allowed_methods POST; }"
            " location /denied { upload on; upload_path " + str(cls.uploads) + "; allowed_methods GET; }"
            " location /relative { upload on; upload_path uploads; allowed_methods POST; }"
            " }\n")
        cls.log = (cls.work / "server.log").open("wb")
        cls.server = subprocess.Popen([BINARY, str(config)], cwd=cls.work,
                                      stdout=cls.log, stderr=cls.log)
        cls.addClassCleanup(cls.stop)
        for _ in range(150):
            if cls.server.poll() is not None:
                raise RuntimeError((cls.work / "server.log").read_text())
            if "Bind successful: 127.0.0.1:" + str(cls.port) in (
                    cls.work / "server.log").read_text(errors="replace"):
                return
            time.sleep(0.02)
        raise RuntimeError("Test server failed to bind")

    @classmethod
    def stop(cls):
        if cls.server.poll() is None:
            cls.server.terminate()
        try:
            cls.server.wait(timeout=3)
        except subprocess.TimeoutExpired:
            cls.server.kill()
            cls.server.wait()
        cls.log.close()
        cls.temp.cleanup()

    def setUp(self):
        # Only flat fixture files in this tester's private upload directory.
        for target in self.uploads.iterdir():
            target.unlink()

    def tearDown(self):
        self.assertEqual(self.guard.read_bytes(), b"DO NOT CHANGE")
        self.assertEqual((self.root / "index.html").read_bytes(), b"INDEX")
        self.assertEqual(set(p.name for p in self.work.iterdir()),
                         {"root", "uploads", "outside", "test.conf", "server.log"})

    def head(self, body, path="/upload", content_type=TYPE, framing=None):
        headers = b"POST " + path.encode() + b" HTTP/1.1\r\nHost: localhost\r\n"
        if content_type is not None:
            headers += b"Content-Type: " + content_type + b"\r\n"
        headers += framing if framing is not None else b"Content-Length: " + str(len(body)).encode() + b"\r\n"
        return headers + b"\r\n"

    def exchange(self, wire, fragments=None):
        with socket.create_connection(("127.0.0.1", self.port), timeout=3) as client:
            if fragments:
                for fragment in fragments:
                    client.sendall(fragment)
                    time.sleep(0.03)
            else:
                client.sendall(wire)
            data = b""
            while True:
                try:
                    chunk = client.recv(65536)
                except ConnectionResetError:
                    # Early rejection may close with unread request bytes.
                    # Accept only a complete, length-framed HTTP response.
                    header, separator, response_body = data.partition(b"\r\n\r\n")
                    lengths = [line.split(b":", 1)[1].strip()
                               for line in header.lower().split(b"\r\n")
                               if line.startswith(b"content-length:")]
                    if not separator or len(lengths) != 1 or len(response_body) != int(lengths[0]):
                        raise
                    break
                if not chunk:
                    break
                data += chunk
        self.assertIn(b"\r\n\r\n", data)
        code = int(data.split(b" ", 2)[1])
        print("  actual HTTP", code, flush=True)
        return code, data

    def post(self, items, expected=201, **kwargs):
        body = multipart(items)
        code, data = self.exchange(self.head(body, **kwargs) + body)
        self.assertEqual(code, expected)
        if expected == 201:
            for name, content in items:
                self.assertEqual((self.uploads / name).read_bytes(), content)
        return data

    def test_text(self):
        self.post([("text.txt", b"hello\n")])

    def test_binary(self):
        self.post([("binary.bin", bytes(range(256)) * 4)])

    def test_empty(self):
        self.post([("empty", b"")])

    def test_multiple(self):
        self.post([("one", b"1"), ("two", b"\x00\xff"), ("zero", b"")])

    def test_existing(self):
        (self.uploads / "existing").write_bytes(b"original")
        self.post([("existing", b"replacement")], expected=409)
        self.assertEqual((self.uploads / "existing").read_bytes(), b"original")

    def test_second_file_conflict_rolls_back(self):
        (self.uploads / "existing").write_bytes(b"original")
        self.post([("new", b"new"), ("existing", b"replacement")], expected=409)
        self.assertEqual({p.name for p in self.uploads.iterdir()}, {"existing"})
        self.assertEqual((self.uploads / "existing").read_bytes(), b"original")

    def test_duplicate_names(self):
        self.post([("duplicate", b"first"), ("duplicate", b"second")], expected=409)
        self.assertEqual(list(self.uploads.iterdir()), [])

    def test_upload_off(self):
        self.post([("off", b"x")], path="/off", expected=403)
        self.assertEqual(list(self.uploads.iterdir()), [])

    def test_method_denied(self):
        data = self.post([("denied", b"x")], path="/denied", expected=405)
        self.assertIn(b"Allow: GET", data)
        self.assertEqual(list(self.uploads.iterdir()), [])

    def test_no_location(self):
        self.post([("unknown", b"x")], path="/unknown", expected=403)

    def test_wrong_type(self):
        self.post([("wrong", b"x")], content_type=b"text/plain", expected=415)

    def test_missing_type(self):
        self.post([("missing", b"x")], content_type=None, expected=400)

    def test_missing_boundary(self):
        self.post([("missing", b"x")], content_type=b"multipart/form-data", expected=400)

    def test_duplicate_boundary(self):
        self.post([("duplicate", b"x")], content_type=TYPE + b"; boundary=other", expected=400)

    def test_quoted_boundary_and_case(self):
        self.post([("quoted", b"x")],
                  content_type=b'Multipart/Form-Data; boundary="WebservBoundary"')

    def test_malformed_multipart(self):
        body = multipart([("broken", b"x")])[:-10]
        self.assertEqual(self.exchange(self.head(body) + body)[0], 400)
        self.assertEqual(list(self.uploads.iterdir()), [])

    def test_unsafe_filename(self):
        self.post([("../outside", b"bad")], expected=400)
        self.assertEqual(list(self.uploads.iterdir()), [])

    def test_backslash_filename(self):
        self.post([("..\\\\outside", b"bad")], expected=400)
        self.assertEqual(list(self.uploads.iterdir()), [])

    def test_field_only(self):
        body = multipart([("unused", b"value")], field=True)
        self.assertEqual(self.exchange(self.head(body) + body)[0], 400)
        self.assertEqual(list(self.uploads.iterdir()), [])

    def test_size_limit(self):
        self.post([("large", b"x" * 5000)], expected=413)
        self.assertEqual(list(self.uploads.iterdir()), [])

    def test_incomplete_body_waits(self):
        body = multipart([("partial", b"data")])
        with socket.create_connection(("127.0.0.1", self.port), timeout=3) as c:
            c.sendall(self.head(body) + body[:-8])
            c.settimeout(0.2)
            with self.assertRaises(socket.timeout):
                c.recv(1)
            self.assertEqual(list(self.uploads.iterdir()), [])
            c.shutdown(socket.SHUT_WR)
            self.assertEqual(c.recv(1024), b"")
        self.assertEqual(list(self.uploads.iterdir()), [])

    def test_fragmented_content_length(self):
        body = multipart([("fragmented", b"\x00binary")])
        head = self.head(body)
        self.assertEqual(self.exchange(b"", [head[:12], head[12:], body[:17], body[17:]])[0], 201)
        self.assertEqual((self.uploads / "fragmented").read_bytes(), b"\x00binary")

    def test_chunked_fragmented(self):
        body = multipart([("chunked", b"\x00chunked\xff")])
        wire = b""
        for offset in range(0, len(body), 11):
            chunk = body[offset:offset + 11]
            wire += ("%x" % len(chunk)).encode() + b";x=y\r\n" + chunk + b"\r\n"
        wire += b"0\r\nX-Trailer: value\r\n\r\n"
        head = self.head(body, framing=b"Transfer-Encoding: chunked\r\n")
        self.assertEqual(self.exchange(b"", [head, wire[:30], wire[30:]])[0], 201)
        self.assertEqual((self.uploads / "chunked").read_bytes(), b"\x00chunked\xff")

    def test_chunked_limit(self):
        body = b"x" * 5000
        wire = b"1388\r\n" + body + b"\r\n0\r\n\r\n"
        self.assertEqual(self.exchange(self.head(body, framing=b"Transfer-Encoding: chunked\r\n") + wire)[0], 413)

    def test_bad_chunk(self):
        wire = b"ZZ\r\nbad\r\n0\r\n\r\n"
        self.assertEqual(self.exchange(self.head(wire, framing=b"Transfer-Encoding: chunked\r\n") + wire)[0], 400)

    def test_cl_te_rejected_without_waiting(self):
        self.assertEqual(self.exchange(self.head(b"", framing=b"Content-Length: 100\r\nTransfer-Encoding: chunked\r\n"))[0], 400)

    def test_invalid_length(self):
        for value in (b"-1", b"abc", b"9999999999999999999999999999999999999"):
            self.assertEqual(self.exchange(self.head(b"", framing=b"Content-Length: " + value + b"\r\n"))[0], 400)

    def test_duplicate_length(self):
        self.assertEqual(self.exchange(self.head(b"", framing=b"Content-Length: 0\r\nContent-Length: 0\r\n"))[0], 400)

    def test_unsupported_transfer_encoding(self):
        self.assertEqual(self.exchange(self.head(b"", framing=b"Transfer-Encoding: gzip, chunked\r\n"))[0], 400)

    def test_no_framing_does_not_upload(self):
        body = multipart([("unframed", b"x")])
        self.assertEqual(self.exchange(self.head(body, framing=b"") + body)[0], 400)
        self.assertEqual(list(self.uploads.iterdir()), [])

    def test_extra_bytes_do_not_complete_short_body(self):
        body = multipart([("smuggled", b"x")])
        wire = self.head(body, framing=b"Content-Length: 3\r\n") + body
        self.assertEqual(self.exchange(wire)[0], 400)
        self.assertEqual(list(self.uploads.iterdir()), [])

    def test_extra_bytes_after_complete_body(self):
        body = multipart([("bounded", b"first")])
        extra = multipart([("extra", b"second")])
        self.assertEqual(self.exchange(self.head(body) + body + extra)[0], 201)
        self.assertEqual((self.uploads / "bounded").read_bytes(), b"first")
        self.assertFalse((self.uploads / "extra").exists())

    def test_chunked_extra_bytes(self):
        body = multipart([("boundedchunk", b"first")])
        wire = ("%x" % len(body)).encode() + b"\r\n" + body + b"\r\n0\r\n\r\n" + multipart([("extra", b"bad")])
        self.assertEqual(self.exchange(self.head(body, framing=b"Transfer-Encoding: chunked\r\n") + wire)[0], 201)
        self.assertEqual((self.uploads / "boundedchunk").read_bytes(), b"first")
        self.assertFalse((self.uploads / "extra").exists())

    def test_relative_upload_path(self):
        self.post([("relative", b"yes")], path="/relative")

    def test_destination_removed(self):
        self.uploads.rmdir()
        try:
            self.post([("gone", b"x")], expected=500)
        finally:
            self.uploads.mkdir()

    def test_existing_symlink_not_followed(self):
        (self.uploads / "link").symlink_to(self.guard)
        self.post([("link", b"bad")], expected=409)
        self.assertTrue((self.uploads / "link").is_symlink())

    def test_get_and_delete_regression(self):
        self.assertEqual(self.exchange(b"GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n")[0], 200)
        (self.root / "delete-me").write_bytes(b"fixture")
        code, data = self.exchange(b"DELETE /delete-me HTTP/1.1\r\nHost: localhost\r\n\r\n")
        self.assertEqual(code, 204)
        self.assertFalse((self.root / "delete-me").exists())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args, remaining = parser.parse_known_args()
    BINARY = str(Path(args.binary).resolve())
    unittest.main(argv=[__file__] + remaining, verbosity=2)
