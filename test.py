#!/usr/bin/env python3
"""End-to-end tests for webserv. Usage: python3 tests/run_tests.py [--stress] [--no-spawn]

Starts ./webserv with conf/default.conf (unless --no-spawn) and talks to it with
raw sockets, so the server's HTTP behaviour is checked byte by byte.
"""
import os
import socket
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
HOST, PORT = "127.0.0.1", 8080
passed = failed = 0


def raw(data, port=PORT, timeout=5, read_all=True):
    s = socket.create_connection((HOST, port), timeout=timeout)
    if isinstance(data, str):
        data = data.encode()
    s.sendall(data)
    buf = b""
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
            if not read_all and b"\r\n\r\n" in buf:
                break
    except socket.timeout:
        pass
    s.close()
    return buf


def parse(resp):
    head, _, body = resp.partition(b"\r\n\r\n")
    lines = head.decode("latin1").split("\r\n")
    status = int(lines[0].split()[1]) if lines and lines[0] else 0
    headers = {}
    for l in lines[1:]:
        k, _, v = l.partition(":")
        headers[k.strip().lower()] = v.strip()
    return status, headers, body


def get(path, port=PORT, extra=""):
    return parse(raw("GET %s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n%s\r\n" % (path, extra), port))


def check(name, cond, info=""):
    global passed, failed
    if cond:
        passed += 1
        print("  ok    " + name)
    else:
        failed += 1
        print("  FAIL  " + name + ("  -> " + str(info) if info else ""))


def multipart(filename, content, boundary="----wsboundary"):
    body = ("--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
            "Content-Type: text/plain\r\n\r\n%s\r\n--%s--\r\n" % (boundary, filename, content, boundary))
    return body, "multipart/form-data; boundary=" + boundary


def test_static():
    print("static files")
    s, h, b = get("/")
    check("GET / -> 200 index.html", s == 200 and b"webserv" in b and "text/html" in h["content-type"], s)
    check("Content-Length matches", int(h["content-length"]) == len(b))
    s, h, b = get("/css/style.css")
    check("css mime type", s == 200 and h["content-type"].startswith("text/css"))
    s, h, b = get("/nothing")
    check("404 uses custom page", s == 404 and b"Not Found" in b and b"Back to the home page" in b)
    s, h, b = get("/../etc/passwd")
    check("path traversal refused", s in (400, 403, 404) and b"root:" not in b, s)
    s, h, b = get("/%2e%2e/%2e%2e/etc/passwd")
    check("encoded traversal refused", s in (400, 403, 404) and b"root:" not in b, s)
    s, h, b = get("/files")
    check("directory without slash -> 301", s == 301 and h["location"] == "/files/", (s, h))
    s, h, b = get("/files/")
    check("autoindex lists files", s == 200 and b"hello.txt" in b and b"sub/" in b)
    s, h, b = get("/files/sub/deep.txt")
    check("nested file", s == 200 and b == b"nested\n")
    s, h, b = get("/old-page")
    check("redirect 301", s == 301 and h["location"] == "/about.html")
    s, h, b = get("/temporary")
    check("redirect 302", s == 302)
    s, h, b = get("/", port=8081)
    check("second port serves other site", s == 200 and b"Second website" in b)
    s, h, b = parse(raw("GET / HTTP/1.1\r\nHost: second.local\r\nConnection: close\r\n\r\n"))
    check("virtual host on shared port", s == 200 and b"Second website" in b)


def test_methods():
    print("methods / errors")
    s, h, b = parse(raw("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"))
    check("POST on GET-only route -> 405 + Allow", s == 405 and "GET" in h.get("allow", ""), (s, h))
    s, h, b = parse(raw("PUT / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"))
    check("PUT -> 405", s == 405, s)
    s, h, b = parse(raw("FOOBAR / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"))
    check("unknown method -> 501", s == 501, s)
    s, h, b = parse(raw("GET / HTTP/2.0\r\nHost: x\r\n\r\n"))
    check("unsupported version -> 505", s == 505, s)
    s, h, b = parse(raw("GARBAGE\r\n\r\n"))
    check("garbage request line -> 400", s == 400, s)
    s, h, b = parse(raw("GET / HTTP/1.1\r\n\r\n"))
    check("missing Host (1.1) -> 400", s == 400, s)
    s, h, b = parse(raw("GET /" + "a" * 9000 + " HTTP/1.1\r\nHost: x\r\n\r\n"))
    check("URI too long -> 414", s == 414, s)
    s, h, b = parse(raw("GET / HTTP/1.1\r\nHost: x\r\nX: " + "a" * 20000 + "\r\n\r\n"))
    check("header too large -> 431", s == 431, s)
    s, h, b = parse(raw("GET / HTTP/1.0\r\n\r\n"))
    check("HTTP/1.0 without Host works and closes", s == 200 and h["connection"] == "close", (s, h))
    s, h, b = parse(raw("DELETE /only-delete/nope.txt HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"))
    check("DELETE missing file -> 404", s == 404, s)
    s, h, b = parse(raw("GET /only-delete/ HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"))
    check("GET refused on DELETE-only route", s == 405, s)
    s, h, b = parse(raw("POST /upload HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"))
    check("POST without length -> 411", s == 411, s)


def test_upload_delete():
    print("upload / delete")
    body, ctype = multipart("hello world.txt", "uploaded content 42")
    req = ("POST /upload HTTP/1.1\r\nHost: x\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
           "Connection: close\r\n\r\n%s" % (ctype, len(body), body))
    s, h, b = parse(raw(req))
    check("multipart upload -> 201", s == 201 and "location" in h, (s, h))
    saved = os.path.join(ROOT, "www/uploads/hello_world.txt")
    check("file stored on disk (name sanitised)", os.path.isfile(saved) and open(saved).read() == "uploaded content 42")
    s, h, b = get("/upload/hello_world.txt")
    check("uploaded file can be fetched", s == 200 and b == b"uploaded content 42", s)
    s, h, b = parse(raw("POST /upload/raw.bin HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nConnection: close\r\n\r\nabcde"))
    check("raw body upload -> 201", s == 201 and os.path.isfile(os.path.join(ROOT, "www/uploads/raw.bin")), s)
    body, ctype = multipart("../../evil.txt", "x")
    req = ("POST /upload HTTP/1.1\r\nHost: x\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s"
           % (ctype, len(body), body))
    raw(req)
    check("filename traversal neutralised", not os.path.exists(os.path.join(ROOT, "evil.txt")) and
          os.path.exists(os.path.join(ROOT, "www/uploads/evil.txt")))
    for name in ("hello_world.txt", "raw.bin", "evil.txt"):
        s, h, b = parse(raw("DELETE /upload/%s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n" % name))
        check("DELETE %s -> 204" % name, s == 204 and not os.path.exists(os.path.join(ROOT, "www/uploads", name)), s)
    s, h, b = parse(raw("DELETE /upload/hello_world.txt HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"))
    check("DELETE twice -> 404", s == 404, s)
    big = "x" * (2 * 1024 * 1024)
    s, h, b = parse(raw("POST /cgi-bin/echo.py HTTP/1.1\r\nHost: x\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s"
                        % (len(big), big), timeout=10))
    check("2 MB body accepted where the limit is 20M", s == 200 and b"received=%d" % len(big) in b, s)
    big = "x" * (1024 * 1024 + 10)
    s, h, b = parse(raw("POST /only-delete HTTP/1.1\r\nHost: x\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s"
                        % (len(big), big)))
    check("body over client_max_body_size -> 413", s == 413, s)
    s, h, b = parse(raw("POST /only-delete HTTP/1.1\r\nHost: x\r\nContent-Length: 99999999\r\nConnection: close\r\n\r\n"))
    check("413 sent before reading the body", s == 413, s)


def test_chunked():
    print("chunked requests")
    req = ("POST /cgi-bin/echo.py HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
           "5\r\nhello\r\n7\r\n world!\r\n0\r\n\r\n")
    s, h, b = parse(raw(req))
    check("chunked body is un-chunked for the CGI", s == 200 and b.endswith(b"hello world!") and b"received=12" in b, b)
    # byte by byte
    sock = socket.create_connection((HOST, PORT))
    for ch in req:
        sock.send(ch.encode())
        time.sleep(0.002)
    data = b""
    sock.settimeout(5)
    while True:
        try:
            c = sock.recv(4096)
        except socket.timeout:
            break
        if not c:
            break
        data += c
    s, h, b = parse(data)
    check("chunked request sent byte by byte", s == 200 and b.endswith(b"hello world!"), s)
    s, h, b = parse(raw("POST /cgi-bin/echo.py HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\nZZ\r\n"))
    check("bad chunk size -> 400", s == 400, s)
    s, h, b = parse(raw("POST /upload/c.txt HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n3\r\nabc\r\n0\r\n\r\n"))
    check("chunked upload", s == 201 and open(os.path.join(ROOT, "www/uploads/c.txt")).read() == "abc", s)
    os.remove(os.path.join(ROOT, "www/uploads/c.txt"))


def test_cgi():
    print("cgi")
    s, h, b = get("/cgi-bin/hello.py?name=42&x=y")
    check("GET CGI + QUERY_STRING", s == 200 and b"QUERY_STRING=name=42&x=y" in b and b"REQUEST_METHOD=GET" in b, s)
    check("CGI runs in its own directory", b"www/cgi-bin" in b)
    s, h, b = get("/cgi-bin/hello.py/extra/info")
    check("PATH_INFO", s == 200 and b"PATH_INFO=/extra/info" in b, s)
    s, h, b = get("/cgi-bin/")
    check("CGI index file", s == 200 and b"default CGI index" in b, (s, b))
    s, h, b = get("/cgi-bin/status.py")
    check("CGI Status header", s == 418 and b"short and stout" in b, s)
    s, h, b = parse(raw("POST /cgi-bin/echo.py HTTP/1.1\r\nHost: x\r\nContent-Type: text/plain\r\nContent-Length: 11\r\nConnection: close\r\n\r\nhello=world"))
    check("POST body reaches the CGI", s == 200 and b.endswith(b"hello=world") and b"content-length=11" in b, b)
    s, h, b = get("/cgi-bin/missing.py")
    check("missing script -> 404", s == 404, s)
    s, h, b = get("/cgi-bin/crash.py")
    check("crashing CGI -> 502", s == 502, s)
    t = time.time()
    s, h, b = parse(raw("GET /cgi-bin/loop.py HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", timeout=40))
    check("endless CGI -> 504 (and it is killed)", s == 504 and time.time() - t < 30, (s, time.time() - t))
    s, h, b = get("/")
    check("server still alive after CGI failures", s == 200)


def test_keepalive():
    print("keep-alive / pipelining")
    sock = socket.create_connection((HOST, PORT))
    sock.settimeout(5)
    sock.sendall(b"GET /about.html HTTP/1.1\r\nHost: x\r\n\r\nGET /about.html HTTP/1.1\r\nHost: x\r\n\r\n")
    data = b""
    t = time.time()
    while data.count(b"HTTP/1.1 200") < 2 and time.time() - t < 5:
        try:
            data += sock.recv(65536)
        except socket.timeout:
            break
    check("two pipelined requests, two responses", data.count(b"HTTP/1.1 200 OK") == 2, data[:80])
    sock.sendall(b"GET /nothing HTTP/1.1\r\nHost: x\r\n\r\n")
    time.sleep(0.3)
    check("connection reusable after a 404", b"404" in sock.recv(65536))
    sock.close()


def test_resilience():
    print("resilience")
    for payload in (b"\x00\x01\x02\xff" * 100, b"GET", b"\r\n" * 50, b"GET / HTTP/1.1\r\nHost", b"POST / HTTP/1.1\r\nContent-Length: 10\r\n\r\nabc"):
        try:
            s = socket.create_connection((HOST, PORT))
            s.sendall(payload)
            s.close()
        except OSError:
            pass
    time.sleep(0.2)
    s, h, b = get("/")
    check("abrupt disconnects / garbage do not break the server", s == 200)
    # client vanishes while a large static file is being sent
    for _ in range(20):
        sock = socket.create_connection((HOST, PORT))
        sock.sendall(b"GET /css/style.css HTTP/1.1\r\nHost: x\r\n\r\n")
        sock.close()
    s, h, b = get("/")
    check("server survives clients that vanish mid-response", s == 200)
    # slow headers must not block other clients
    slow = socket.create_connection((HOST, PORT))
    slow.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n")
    t = time.time()
    s, h, b = get("/")
    check("a stalled client does not block others", s == 200 and time.time() - t < 1)
    slow.close()


def stress(n_threads=50, per_thread=40):
    print("stress (%d threads x %d requests)" % (n_threads, per_thread))
    errors = []

    def worker():
        for i in range(per_thread):
            try:
                s, h, b = get("/" if i % 2 else "/about.html")
                if s != 200:
                    errors.append(s)
                if i % 10 == 0:
                    s, h, b = get("/cgi-bin/hello.py")
                    if s != 200:
                        errors.append(("cgi", s))
            except Exception as e:  # noqa
                errors.append(repr(e))

    t = time.time()
    threads = [threading.Thread(target=worker) for _ in range(n_threads)]
    for th in threads:
        th.start()
    for th in threads:
        th.join()
    check("%d requests, no error (%.1fs)" % (n_threads * per_thread, time.time() - t), not errors, errors[:5])


def main():
    global passed, failed
    spawn = "--no-spawn" not in sys.argv
    proc = None
    if spawn:
        subprocess.call(["make", "-s", "-C", ROOT])
        proc = subprocess.Popen([os.path.join(ROOT, "webserv"), os.path.join(ROOT, "conf/default.conf")],
                                cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)
    try:
        test_static()
        test_methods()
        test_upload_delete()
        test_chunked()
        test_keepalive()
        test_cgi()
        test_resilience()
        if "--stress" in sys.argv:
            stress()
        if proc:
            check("server process is still running", proc.poll() is None)
    finally:
        if proc:
            proc.terminate()
            proc.wait()
    print("\n%d passed, %d failed" % (passed, failed))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()