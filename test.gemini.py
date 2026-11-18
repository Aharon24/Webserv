#!/usr/bin/env python3
import os
import sys
import time
import socket
import shutil
import signal
import tempfile
import subprocess
import concurrent.futures

WEBSERV_BIN = "./webserv"


class Colors:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    CYAN = "\033[96m"
    RESET = "\033[0m"


passed = 0
failed = 0


def log_pass(name):
    global passed
    passed += 1
    print(f"{Colors.GREEN}[PASS]{Colors.RESET} {name}")


def log_fail(name, reason=""):
    global failed
    failed += 1
    print(f"{Colors.RED}[FAIL]{Colors.RESET} {name} -> {reason}")


def get_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def wait_for_port(port, timeout=3.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return True
        except OSError:
            time.sleep(0.05)
    return False


def recv_until_close(sock, timeout=3.0):
    sock.settimeout(timeout)
    chunks = []
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                break
            chunks.append(data)
    except socket.timeout:
        pass
    return b"".join(chunks)


# ==============================================================================
# PART 1: CONFIG PARSER & CLI TESTS (Tokenizer + Parser + webserv.cpp)
# ==============================================================================
def run_config_error_test(name, args, expected_exit_nonzero=True):
    try:
        res = subprocess.run(
            [WEBSERV_BIN] + args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=2.0,
        )
        if expected_exit_nonzero:
            if res.returncode != 0:
                log_pass(name)
            else:
                log_fail(name, f"Expected non-zero exit code, got 0")
        else:
            if res.returncode == 0:
                log_pass(name)
            else:
                log_fail(name, f"Expected 0 exit code, got {res.returncode}: {res.stderr.decode()}")
    except subprocess.TimeoutExpired:
        if expected_exit_nonzero:
            log_fail(name, "Server started and hung instead of rejecting invalid config!")
        else:
            log_pass(name)


def test_config_parser(tmp_dir):
    print(f"\n{Colors.CYAN}=== 1. CONFIG PARSER & CLI TESTS ==={Colors.RESET}")

    valid_root = os.path.join(tmp_dir, "www")
    os.makedirs(valid_root, exist_ok=True)
    with open(os.path.join(valid_root, "index.html"), "w") as f:
        f.write("<h1>OK</h1>")

    # 1. CLI arguments
    run_config_error_test("CLI: Too many arguments (argc > 2)", ["a.conf", "b.conf"])
    run_config_error_test("CLI: Non-existent config file", [os.path.join(tmp_dir, "nope.conf")])
    run_config_error_test("CLI: Directory passed instead of file", [tmp_dir])

    # 2. Empty file
    empty_conf = os.path.join(tmp_dir, "empty.conf")
    open(empty_conf, "w").close()
    run_config_error_test("Config: Empty config file", [empty_conf])

    # 3. Comments-only file
    comment_conf = os.path.join(tmp_dir, "comments.conf")
    with open(comment_conf, "w") as f:
        f.write("# Only comments here\n   # another comment\n")
    run_config_error_test("Config: Comments-only config file", [comment_conf])

    # 4. Syntax & Brace errors
    bad_configs = {
        "Missing semicolon after root": f"""
            server {{
                listen 127.0.0.1:8080;
                root {valid_root}
            }}
        """,
        "Unclosed server brace": f"""
            server {{
                listen 127.0.0.1:8080;
                root {valid_root};
        """,
        "Extra closing brace": f"""
            server {{
                listen 127.0.0.1:8080;
                root {valid_root};
            }} }}
        """,
        "Missing listen directive": f"""
            server {{
                root {valid_root};
            }}
        """,
        "Missing root directive": """
            server {
                listen 127.0.0.1:8080;
            }
        """,
        "Invalid IPv4 (999.1.1.1)": f"""
            server {{
                listen 999.1.1.1:8080;
                root {valid_root};
            }}
        """,
        "Invalid Port (70000)": f"""
            server {{
                listen 127.0.0.1:70000;
                root {valid_root};
            }}
        """,
        "Duplicate listen in same server": f"""
            server {{
                listen 127.0.0.1:8080;
                listen 127.0.0.1:8080;
                root {valid_root};
            }}
        """,
        "Duplicate root in server": f"""
            server {{
                listen 127.0.0.1:8080;
                root {valid_root};
                root {valid_root};
            }}
        """,
        "Non-existent root directory": """
            server {
                listen 127.0.0.1:8080;
                root /this/path/definitely/does/not/exist_42;
            }
        """,
        "Invalid client_max_body_size suffix (10X)": f"""
            server {{
                listen 127.0.0.1:8080;
                root {valid_root};
                client_max_body_size 10X;
            }}
        """,
        "Invalid error_page code (200)": f"""
            server {{
                listen 127.0.0.1:8080;
                root {valid_root};
                error_page 200 /err.html;
            }}
        """,
        "Error_page path without leading '/'": f"""
            server {{
                listen 127.0.0.1:8080;
                root {valid_root};
                error_page 404 err.html;
            }}
        """,
        "Location path without leading '/'": f"""
            server {{
                listen 127.0.0.1:8080;
                root {valid_root};
                location images {{
                    autoindex on;
                }}
            }}
        """,
        "Duplicate location path": f"""
            server {{
                listen 127.0.0.1:8080;
                root {valid_root};
                location /api {{ autoindex on; }}
                location /api {{ autoindex off; }}
            }}
        """,
        "Invalid HTTP method in allowed_methods": f"""
            server {{
                listen 127.0.0.1:8080;
                root {valid_root};
                location / {{
                    allowed_methods GET HACK;
                }}
            }}
        """,
        "Upload on without upload_path": f"""
            server {{
                listen 127.0.0.1:8080;
                root {valid_root};
                location /up {{
                    upload on;
                }}
            }}
        """,
        "Invalid return redirect code (200)": f"""
            server {{
                listen 127.0.0.1:8080;
                root {valid_root};
                location /old {{
                    return 200 /new;
                }}
            }}
        """,
        "CGI invalid extension (no dot)": f"""
            server {{
                listen 127.0.0.1:8080;
                root {valid_root};
                location /cgi {{
                    cgi py /usr/bin/python3;
                }}
            }}
        """,
        "CGI non-executable interpreter": f"""
            server {{
                listen 127.0.0.1:8080;
                root {valid_root};
                location /cgi {{
                    cgi .py /non/existent/python3;
                }}
            }}
        """,
        "Unknown directive inside server": f"""
            server {{
                listen 127.0.0.1:8080;
                root {valid_root};
                foobar 123;
            }}
        """,
    }

    for idx, (desc, content) in enumerate(bad_configs.items()):
        cfg_path = os.path.join(tmp_dir, f"bad_{idx}.conf")
        with open(cfg_path, "w") as f:
            f.write(content)
        run_config_error_test(f"Parser rejects: {desc}", [cfg_path])


# ==============================================================================
# PART 2: NETWORK, SOCKETS & POLL() EVENT LOOP TESTS (Server.cpp)
# ==============================================================================
def test_network_and_poll(tmp_dir):
    print(f"\n{Colors.CYAN}=== 2. NETWORK, SOCKETS & POLL() EVENT LOOP TESTS ==={Colors.RESET}")

    port1 = get_free_port()
    port2 = get_free_port()
    port3 = get_free_port()

    root1 = os.path.join(tmp_dir, "site1")
    root2 = os.path.join(tmp_dir, "site2")
    os.makedirs(root1, exist_ok=True)
    os.makedirs(root2, exist_ok=True)

    with open(os.path.join(root1, "index.html"), "w") as f:
        f.write("SITE_1_CONTENT")

    with open(os.path.join(root2, "index.html"), "w") as f:
        f.write("SITE_2_CONTENT")

    # Large file (2 MB) to test partial write() / POLLOUT buffering
    large_payload = "A" * (2 * 1024 * 1024)
    with open(os.path.join(root1, "big.txt"), "w") as f:
        f.write(large_payload)

    conf_path = os.path.join(tmp_dir, "valid_multi.conf")
    with open(conf_path, "w") as f:
        f.write(f"""
        # Multi-port & multi-server configuration
        server {{
            listen 127.0.0.1:{port1};
            listen 127.0.0.1:{port2};
            server_name site1.local;
            root {root1};
            index index.html;
            client_max_body_size 100;

            location / {{
                allowed_methods GET POST DELETE;
            }}
        }}

        server {{
            listen 127.0.0.1:{port3};
            server_name site2.local;
            root {root2};
            index index.html;
            client_max_body_size 1K;
        }}
        """)

    proc = subprocess.Popen(
        [WEBSERV_BIN, conf_path],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    try:
        if not (wait_for_port(port1) and wait_for_port(port2) and wait_for_port(port3)):
            log_fail("Server startup", "Ports did not open in time")
            return
        log_pass(f"Multi-port startup ({port1}, {port2}, {port3})")

        # Test 1: Multiple ports in one server block + separate server block
        for p, expected in [(port1, b"SITE_1_CONTENT"), (port2, b"SITE_1_CONTENT"), (port3, b"SITE_2_CONTENT")]:
            with socket.create_connection(("127.0.0.1", p), timeout=2.0) as s:
                s.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
                resp = recv_until_close(s)
                if b"200 OK" in resp and expected in resp:
                    log_pass(f"Port {p} serves expected content")
                else:
                    log_fail(f"Port {p} content check", f"Got: {resp[:120]}")

        # Test 2: Partial Read (Slowloris-style byte-by-byte / chunked TCP packets)
        with socket.create_connection(("127.0.0.1", port1), timeout=3.0) as s:
            req_parts = [
                b"GET /",
                b"index.html ",
                b"HTTP/1.1\r\n",
                b"Host: ",
                b"localhost\r\n",
                b"\r\n",
            ]
            for part in req_parts:
                s.sendall(part)
                time.sleep(0.05)
            resp = recv_until_close(s)
            if b"200 OK" in resp and b"SITE_1_CONTENT" in resp:
                log_pass("Partial read across multiple recv() calls")
            else:
                log_fail("Partial read", f"Unexpected response: {resp[:120]}")

        # Test 3: Non-blocking behavior (Hanging client must NOT block other clients!)
        hanging_sock = socket.create_connection(("127.0.0.1", port1), timeout=3.0)
        hanging_sock.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n")  # No final \r\n!
        try:
            with socket.create_connection(("127.0.0.1", port1), timeout=1.0) as fast_sock:
                fast_sock.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
                resp = recv_until_close(fast_sock, timeout=1.5)
                if b"200 OK" in resp and b"SITE_1_CONTENT" in resp:
                    log_pass("Non-blocking I/O: slow/incomplete client does not block fast client")
                else:
                    log_fail("Non-blocking I/O", "Fast client did not receive valid response")
        finally:
            hanging_sock.close()

        # Test 4: Partial Write / Large Response (2MB file over small recv buffer)
        with socket.create_connection(("127.0.0.1", port1), timeout=5.0) as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8192)
            s.sendall(b"GET /big.txt HTTP/1.1\r\nHost: localhost\r\n\r\n")
            resp = recv_until_close(s, timeout=5.0)
            if b"200 OK" in resp and resp.count(b"A") == len(large_payload):
                log_pass("Partial write / POLLOUT handles 2MB response completely")
            else:
                log_fail("Partial write (2MB)", f"Received {resp.count(b'A')} bytes instead of {len(large_payload)}")

        # Test 5: Abrupt client disconnect mid-request & mid-response (SIGPIPE check)
        s_abort = socket.create_connection(("127.0.0.1", port1), timeout=2.0)
        s_abort.sendall(b"GET /big.txt HTTP/1.1\r\nHost: localhost\r\n\r\n")
        s_abort.recv(100)
        s_abort.close()  # Abrupt close while server is sending 2MB
        time.sleep(0.1)
        if proc.poll() is None:
            log_pass("Abrupt client disconnect does not crash server (SIGPIPE handled)")
        else:
            log_fail("Abrupt client disconnect", f"Server crashed with code {proc.returncode}")

        # Test 6: client_max_body_size enforcement (port1 limit is 100 bytes)
        with socket.create_connection(("127.0.0.1", port1), timeout=2.0) as s:
            body = b"X" * 150
            req = (
                b"POST / HTTP/1.1\r\n"
                b"Host: localhost\r\n"
                b"Content-Length: 150\r\n\r\n" + body
            )
            s.sendall(req)
            resp = recv_until_close(s)
            if b"413" in resp:
                log_pass("client_max_body_size returns 413 Payload Too Large")
            else:
                log_fail("client_max_body_size (Content-Length)", f"Expected 413, got: {resp[:120]}")

        # Test 7: Chunked body exceeding client_max_body_size
        with socket.create_connection(("127.0.0.1", port1), timeout=2.0) as s:
            chunked = (
                b"POST / HTTP/1.1\r\n"
                b"Host: localhost\r\n"
                b"Transfer-Encoding: chunked\r\n\r\n"
                b"65\r\n" + (b"B" * 101) + b"\r\n0\r\n\r\n"
            )
            s.sendall(chunked)
            resp = recv_until_close(s)
            if b"413" in resp:
                log_pass("Chunked body > client_max_body_size returns 413")
            else:
                log_fail("Chunked max body size", f"Expected 413, got: {resp[:120]}")

        # Test 8: Header too large (> 32KB -> 431)
        with socket.create_connection(("127.0.0.1", port1), timeout=2.0) as s:
            huge_header = b"GET / HTTP/1.1\r\nHost: localhost\r\nX-Big: " + (b"Z" * 35000) + b"\r\n\r\n"
            try:
                s.sendall(huge_header)
            except BrokenPipeError:
                pass
            resp = recv_until_close(s)
            if b"431" in resp:
                log_pass("Header > 32KB returns 431 Request Header Fields Too Large")
            else:
                log_fail("Large header (431)", f"Expected 431, got: {resp[:120]}")

        # Test 9: Timeout check (Server::run has TIMEOUT_SECONDS = 5)
        print("  ...waiting ~6.5s to test idle client timeout...")
        with socket.create_connection(("127.0.0.1", port1), timeout=8.0) as s:
            s.sendall(b"GET / HTTP/1.1\r\n")  # Incomplete request
            time.sleep(6.5)
            data = s.recv(1024)
            if data == b"":
                log_pass("Idle client connection timed out and closed cleanly")
            else:
                log_fail("Idle client timeout", f"Expected EOF, got: {data}")

        # Test 10: Concurrent Stress Test (100 simultaneous connections)
        def worker(idx):
            target_port = port1 if idx % 2 == 0 else port3
            expected = b"SITE_1_CONTENT" if idx % 2 == 0 else b"SITE_2_CONTENT"
            with socket.create_connection(("127.0.0.1", target_port), timeout=4.0) as s:
                s.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
                resp = recv_until_close(s, timeout=4.0)
                return (b"200 OK" in resp) and (expected in resp)

        with concurrent.futures.ThreadPoolExecutor(max_workers=25) as executor:
            results = list(executor.map(worker, range(100)))

        if all(results):
            log_pass("Concurrency stress test: 100 parallel requests succeeded")
        else:
            log_fail("Concurrency stress test", f"Only {sum(results)}/100 succeeded")

    finally:
        proc.send_signal(signal.SIGINT)
        try:
            proc.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            proc.kill()


def main():
    if not os.path.isfile(WEBSERV_BIN) or not os.access(WEBSERV_BIN, os.X_OK):
        print(f"{Colors.RED}Error: '{WEBSERV_BIN}' not found or not executable. Run 'make' first.{Colors.RESET}")
        sys.exit(1)

    tmp_dir = tempfile.mkdtemp(prefix="webserv_p1_test_")
    try:
        test_config_parser(tmp_dir)
        test_network_and_poll(tmp_dir)
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)

    print(f"\n{Colors.CYAN}================ SUMMARY ================{Colors.RESET}")
    print(f"Passed: {Colors.GREEN}{passed}{Colors.RESET} | Failed: {Colors.RED}{failed}{Colors.RESET}")
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()