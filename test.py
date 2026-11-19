#!/usr/bin/env python3
import os
import socket
import time
import threading
import http.client
import urllib.parse

HOST = "127.0.0.1"
PORT = 8080

# Համապատասխանեցրու քո .conf ֆայլի route-երին
ROUTES = {
    "index": "/",
    "not_found": "/this_file_does_not_exist_12345.html",
    "get_only": "/",                 # location որտեղ allowed_methods GET;
    "redirect": "/redirect",         # location return 301 ...;
    "autoindex": "/autoindex/",      # location autoindex on;
    "upload": "/upload",             # location upload on; upload_path ...;
    "delete_dir": "/upload",         # որտեղից կարելի է DELETE անել
    "body_limit": "/post_body",      # փոքր client_max_body_size ունեցող route
    "cgi": "/cgi-bin/test.py",       # CGI script route
}

PASSED = 0
FAILED = 0


def record(name, ok, detail=""):
    global PASSED, FAILED
    if ok:
        PASSED += 1
        print(f"[PASS] {name}")
    else:
        FAILED += 1
        print(f"[FAIL] {name} -> {detail}")


def raw_request(payload: bytes, timeout=3.0) -> bytes:
    with socket.create_connection((HOST, PORT), timeout=timeout) as s:
        s.sendall(payload)
        s.shutdown(socket.SHUT_WR)
        chunks = []
        while True:
            try:
                data = s.recv(4096)
                if not data:
                    break
                chunks.append(data)
            except socket.timeout:
                break
        return b"".join(chunks)


def http_req(method, path, body=None, headers=None):
    conn = http.client.HTTPConnection(HOST, PORT, timeout=5)
    conn.request(method, path, body=body, headers=headers or {})
    resp = conn.getresponse()
    data = resp.read()
    hdrs = {k.lower(): v for k, v in resp.getheaders()}
    conn.close()
    return resp.status, hdrs, data


def test_basic_get_and_errors():
    # 1. GET /
    status, hdrs, data = http_req("GET", ROUTES["index"])
    record("GET / returns 200", status == 200 and len(data) > 0, f"got {status}")

    # 2. 404 Not Found
    status, _, _ = http_req("GET", ROUTES["not_found"])
    record("GET nonexistent returns 404", status == 404, f"got {status}")

    # 3. 405 Method Not Allowed + Allow header
    status, hdrs, _ = http_req("DELETE", ROUTES["get_only"])
    record(
        "Disallowed method returns 405 with Allow",
        status == 405 and "allow" in hdrs,
        f"got {status}, headers={hdrs}",
    )

    # 4. Path traversal protection
    raw = raw_request(b"GET /../../../../etc/passwd HTTP/1.1\r\nHost: localhost\r\n\r\n")
    first_line = raw.split(b"\r\n", 1)[0].decode(errors="replace")
    record(
        "Path traversal blocked (403/400/404)",
        any(code in first_line for code in ("403", "400", "404")),
        f"got {first_line}",
    )

    # 5. Malformed request (400) & Missing Host in HTTP/1.1 (400)
    raw = raw_request(b"GET / HTTP/1.1\r\n\r\n")
    first_line = raw.split(b"\r\n", 1)[0].decode(errors="replace")
    record("HTTP/1.1 without Host returns 400", "400" in first_line, f"got {first_line}")

    # 6. URI Too Long (414)
    long_path = "/" + ("a" * 2500)
    status, _, _ = http_req("GET", long_path)
    record("URI > 2048 returns 414", status == 414, f"got {status}")


def test_redirect_and_autoindex():
    # Redirect
    status, hdrs, _ = http_req("GET", ROUTES["redirect"])
    record(
        "Redirect returns 301/302 with Location",
        status in (301, 302, 303, 307, 308) and "location" in hdrs,
        f"got {status}, headers={hdrs}",
    )

    # Autoindex
    status, hdrs, data = http_req("GET", ROUTES["autoindex"])
    record(
        "Autoindex returns 200 HTML listing",
        status == 200 and b"Index of" in data,
        f"got {status}",
    )


def test_upload_and_delete():
    boundary = "----WebservTestBoundary7MA4YWxkTrZu0gW"
    filename = f"test_upload_{int(time.time())}.txt"
    file_content = b"Hello binary \x00\x01\x02 world from webserv test!"

    body = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
        f"Content-Type: application/octet-stream\r\n\r\n"
    ).encode() + file_content + f"\r\n--{boundary}--\r\n".encode()

    headers = {"Content-Type": f"multipart/form-data; boundary={boundary}"}

    # 1. Upload file -> 201
    status, _, _ = http_req("POST", ROUTES["upload"], body=body, headers=headers)
    record("POST multipart upload returns 201", status == 201, f"got {status}")

    # 2. Duplicate upload -> 409 Conflict
    status, _, _ = http_req("POST", ROUTES["upload"], body=body, headers=headers)
    record("POST duplicate filename returns 409", status == 409, f"got {status}")

    # 3. GET uploaded file -> exact binary match
    file_url = f"{ROUTES['delete_dir'].rstrip('/')}/{filename}"
    status, _, data = http_req("GET", file_url)
    record(
        "GET uploaded binary file matches byte-for-byte",
        status == 200 and data == file_content,
        f"got {status}, len={len(data)}",
    )

    # 4. DELETE uploaded file -> 204
    status, _, _ = http_req("DELETE", file_url)
    record("DELETE existing file returns 204", status == 204, f"got {status}")

    # 5. DELETE again -> 404
    status, _, _ = http_req("DELETE", file_url)
    record("DELETE already deleted file returns 404", status == 404, f"got {status}")


def test_chunked_and_body_limit():
    # Chunked POST upload
    boundary = "----WebservChunkedBoundary12345"
    filename = f"chunked_{int(time.time())}.txt"
    plain_body = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
        f"Content-Type: text/plain\r\n\r\n"
        f"Chunked transfer encoding works!"
        f"\r\n--{boundary}--\r\n"
    ).encode()

    # Բաժանում ենք 10 բայթանոց chunk-երի
    chunked_payload = bytearray()
    for i in range(0, len(plain_body), 10):
        part = plain_body[i:i + 10]
        chunked_payload.extend(f"{len(part):x}\r\n".encode())
        chunked_payload.extend(part + b"\r\n")
    chunked_payload.extend(b"0\r\n\r\n")

    req = (
        f"POST {ROUTES['upload']} HTTP/1.1\r\n"
        f"Host: {HOST}:{PORT}\r\n"
        f"Content-Type: multipart/form-data; boundary={boundary}\r\n"
        f"Transfer-Encoding: chunked\r\n\r\n"
    ).encode() + bytes(chunked_payload)

    raw = raw_request(req)
    first_line = raw.split(b"\r\n", 1)[0].decode(errors="replace")
    record("Chunked POST upload returns 201", "201" in first_line, f"got {first_line}")
    # Մաքրում ենք ստեղծված ֆայլը
    http_req("DELETE", f"{ROUTES['delete_dir'].rstrip('/')}/{filename}")

    # Payload Too Large (413)
    huge_body = b"X" * 2000000
    status, _, _ = http_req(
        "POST",
        ROUTES["body_limit"],
        body=huge_body,
        headers={"Content-Type": "text/plain"},
    )
    record("Oversized body returns 413", status == 413, f"got {status}")


def test_cgi():
    # GET CGI with query string
    status, _, data = http_req("GET", f"{ROUTES['cgi']}?name=42Yerevan&val=100")
    record(
        "CGI GET with QUERY_STRING returns 200",
        status == 200 and b"42Yerevan" in data,
        f"got {status}, body={data[:120]!r}",
    )

    # POST CGI with body
    post_data = b"msg=hello_cgi_post"
    status, _, data = http_req(
        "POST",
        ROUTES["cgi"],
        body=post_data,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )
    record(
        "CGI POST receives stdin body and returns 200",
        status == 200 and b"hello_cgi_post" in data,
        f"got {status}, body={data[:120]!r}",
    )


def test_concurrency_and_disconnect():
    # 1. Abrupt client disconnect mid-request (server must not crash)
    s = socket.create_connection((HOST, PORT), timeout=2)
    s.sendall(b"POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Length: 50000\r\n\r\npartial")
    s.close()

    # 2. 30 concurrent GET requests
    results = []
    lock = threading.Lock()

    def worker():
        try:
            st, _, _ = http_req("GET", ROUTES["index"])
            with lock:
                results.append(st == 200)
        except Exception:
            with lock:
                results.append(False)

    threads = [threading.Thread(target=worker) for _ in range(30)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    record(
        "30 concurrent requests + disconnect resilience",
        len(results) == 30 and all(results),
        f"passed {sum(results)}/30",
    )


if __name__ == "__main__":
    print(f"=== Testing Webserv at http://{HOST}:{PORT} ===\n")
    try:
        test_basic_get_and_errors()
        test_redirect_and_autoindex()
        test_upload_and_delete()
        test_chunked_and_body_limit()
        test_cgi()
        test_concurrency_and_disconnect()
    except ConnectionRefusedError:
        print(f"[ERROR] Cannot connect to {HOST}:{PORT}. Start ./webserv first.")
        exit(1)

    print(f"\n=== SUMMARY: {PASSED} PASSED, {FAILED} FAILED ===")