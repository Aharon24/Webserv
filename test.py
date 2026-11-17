#!/usr/bin/env python3
"""Black-box Webserv test runner (Python standard library only).

Run from the Webserv project root:
    python3 /path/to/webserv_python_tester.py --group parser
    python3 /path/to/webserv_python_tester.py --group runtime
    python3 /path/to/webserv_python_tester.py --group compliance
    python3 /path/to/webserv_python_tester.py --group all

The runner creates temporary configs/files under /tmp and does not edit project files.
"""

import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


class WebservTester:
    def __init__(self, project_root: Path, binary: Path, only_id=None):
        self.project_root = project_root.resolve()
        self.binary = binary.resolve()
        self.only_id = only_id
        self.temp = tempfile.TemporaryDirectory(prefix="webserv_pytests_")
        self.tmp = Path(self.temp.name)
        self.root = self.tmp / "www"
        self.root.mkdir()
        (self.root / "index.html").write_text("WEBSERV_TEST_INDEX", encoding="utf-8")
        (self.root / "old.txt").write_text("DELETE_ME", encoding="utf-8")
        (self.root / "restricted.txt").write_text("RESTRICTED_FILE", encoding="utf-8")
        self.uploads = self.tmp / "uploads"
        self.uploads.mkdir()
        self.results = []
        self.config_counter = 0

    def close(self):
        self.temp.cleanup()

    @staticmethod
    def free_port():
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.bind(("127.0.0.1", 0))
            return sock.getsockname()[1]

    def base_config(self, port=None, directives="", locations="", root=None):
        if port is None:
            port = self.free_port()
        if root is None:
            root = self.root
        return (
            "server {\n"
            f"    listen 127.0.0.1:{port};\n"
            f"    root {root};\n"
            "    index index.html;\n"
            f"    {directives}\n"
            f"    {locations}\n"
            "}\n"
        ), port

    def write_config(self, content):
        self.config_counter += 1
        path = self.tmp / f"case_{self.config_counter}.conf"
        path.write_text(content, encoding="utf-8")
        return path

    def stop_process(self, proc):
        if proc.poll() is None:
            proc.terminate()
        try:
            out, _ = proc.communicate(timeout=1.5)
        except subprocess.TimeoutExpired:
            proc.kill()
            out, _ = proc.communicate(timeout=1.5)
        return out or ""

    def run_invalid_config(self, content, expected_error, timeout=2.0):
        config_path = self.write_config(content)
        proc = subprocess.Popen(
            [str(self.binary), str(config_path)],
            cwd=str(self.project_root),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        try:
            output, _ = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            output = self.stop_process(proc)
            return False, "server stayed running; expected config rejection"
        if proc.returncode == 0:
            return False, "process exited successfully; expected config error"
        if expected_error not in output:
            return False, f"expected '{expected_error}', got output:\n{output[-1200:]}"
        return True, expected_error

    def start_server(self, content, ports, timeout=3.0):
        config_path = self.write_config(content)
        proc = subprocess.Popen(
            [str(self.binary), str(config_path)],
            cwd=str(self.project_root),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        ready = set()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and len(ready) < len(ports):
            if proc.poll() is not None:
                break
            for port in ports:
                if port in ready:
                    continue
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=0.15):
                        ready.add(port)
                except OSError:
                    pass
            if len(ready) < len(ports):
                time.sleep(0.04)
        if len(ready) == len(ports):
            return proc, ""
        output = self.stop_process(proc)
        return None, f"listener startup failed ({sorted(ready)}/{sorted(ports)} ready):\n{output[-1600:]}"

    @staticmethod
    def http_request(port, request_bytes, timeout=4.0):
        chunks = []
        with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
            sock.settimeout(timeout)
            sock.sendall(request_bytes)
            while True:
                try:
                    data = sock.recv(65536)
                except socket.timeout:
                    raise RuntimeError("timed out waiting for HTTP response")
                if not data:
                    break
                chunks.append(data)
        raw = b"".join(chunks)
        head, sep, body = raw.partition(b"\r\n\r\n")
        if not sep:
            raise RuntimeError(f"invalid HTTP response (missing header separator): {raw[:300]!r}")
        first_line = head.split(b"\r\n", 1)[0].decode("latin-1", "replace")
        fields = first_line.split()
        if len(fields) < 2 or not fields[1].isdigit():
            raise RuntimeError(f"invalid HTTP status line: {first_line!r}")
        headers = {}
        for line in head.split(b"\r\n")[1:]:
            if b":" in line:
                name, value = line.split(b":", 1)
                headers[name.decode("latin-1").lower()] = value.decode("latin-1").strip()
        return int(fields[1]), headers, body, raw

    def add(self, test_id, name, fn):
        if self.only_id and test_id != self.only_id:
            return
        try:
            ok, detail = fn()
        except Exception as exc:  # keep the full suite running after a failure
            ok, detail = False, f"{type(exc).__name__}: {exc}"
        self.results.append((test_id, name, ok, str(detail)))

    def run_parser_group(self):
        def invalid(test_id, name, directives="", location="", expected="", raw=None):
            def test():
                port = self.free_port()
                if raw is None:
                    content, _ = self.base_config(port, directives=directives, locations=location)
                else:
                    content = raw.format(port=port, root=self.root)
                return self.run_invalid_config(content, expected)
            self.add(test_id, name, test)

        invalid("P01", "Invalid IPv4 octet", raw="server {{\n listen 999.1.1.1:{port};\n root {root};\n}}", expected="Invalid IPv4 address")
        invalid("P02", "Non-numeric port", raw="server {{\n listen 127.0.0.1:abc;\n root {root};\n}}", expected="Invalid port")
        invalid("P03", "Port zero", raw="server {{\n listen 127.0.0.1:0;\n root {root};\n}}", expected="Invalid port")
        invalid("P04", "Port above 65535", raw="server {{\n listen 127.0.0.1:65536;\n root {root};\n}}", expected="Invalid port")
        invalid("P05", "Duplicate listen in one server", raw="server {{\n listen 127.0.0.1:{port};\n listen 127.0.0.1:{port};\n root {root};\n}}", expected="Duplicate listen address")
        invalid("P06", "Duplicate server root", directives=f"root {self.root};", expected="Duplicate 'root' directive")
        invalid("P07", "Duplicate server index", directives="index home.html;", expected="Duplicate 'index' directive")
        invalid("P08", "Duplicate server_name", directives="server_name one.test;\n server_name two.test;", expected="Duplicate 'server_name' directive")
        invalid("P09", "Duplicate error_page status", directives="error_page 404 /404.html;\n error_page 404 /missing.html;", expected="Duplicate error_page status code")
        invalid("P10", "Non-error status used as error_page", directives="error_page 200 /200.html;", expected="between 400 and 599")
        invalid("P11", "Malformed error_page code", directives="error_page abc /404.html;", expected="Invalid HTTP error code")
        invalid("P12", "Error page URI without leading slash", directives="error_page 404 errors/404.html;", expected="Error page URI must start with '/'")
        invalid("P13", "Non-numeric body size", directives="client_max_body_size abc;", expected="Invalid client_max_body_size value")
        invalid("P14", "Negative body size", directives="client_max_body_size -1;", expected="Invalid client_max_body_size value")
        invalid("P15", "Unsupported body-size suffix", directives="client_max_body_size 10T;", expected="Invalid size suffix")
        invalid("P16", "Body-size integer overflow", directives="client_max_body_size 999999999999999999999999999999999999;", expected="overflow")
        invalid("P17", "Duplicate client_max_body_size", directives="client_max_body_size 10;\n client_max_body_size 20;", expected="Duplicate 'client_max_body_size' directive")
        invalid("P18", "Unsupported HTTP method", location="location /x { allowed_methods GET PUT; }", expected="Invalid method HTTP")
        invalid("P19", "Duplicate HTTP method", location="location /x { allowed_methods GET GET; }", expected="Duplicate HTTP method")
        invalid("P20", "Empty allowed_methods", location="location /x { allowed_methods; }", expected="HTTP methood is required")
        invalid("P21", "Unsupported redirect status", location="location /x { return 200 /new; }", expected="Only return 301")
        invalid("P22", "Duplicate return directive", location="location /x { return 301 /a; return 302 /b; }", expected="Duplicate 'return' directives")
        invalid("P23", "Invalid autoindex value", location="location /x { autoindex maybe; }", expected="Invalid autoindex value")
        invalid("P24", "Location path must start with slash", raw="server {{\n listen 127.0.0.1:{port};\n root {root};\n location x {{ root {root}; }}\n}}", expected="Location path must start with '/'")
        invalid("P25", "Duplicate location path", location="location /x { root " + str(self.root) + "; }\n location /x { root " + str(self.root) + "; }", expected="Duplicate location: /x")
        invalid("P26", "Upload on without upload_path", location="location /upload { upload on; }", expected="requires 'upload_path'")
        invalid("P27", "Nonexistent upload directory", location="location /upload { upload on; upload_path /tmp/this_upload_dir_must_not_exist_42; }", expected="Cannot access upload path")
        invalid("P28", "Invalid CGI extension", location="location /cgi { cgi py " + sys.executable + "; }", expected="Invalid CGI extension")
        invalid("P29", "Duplicate CGI extension", location="location /cgi { cgi .py " + sys.executable + "; cgi .py /bin/sh; }", expected="Duplicate CGI extension")
        invalid("P30", "Nonexistent CGI interpreter", location="location /cgi { cgi .py /tmp/no_such_python_interpreter_42; }", expected="Cannot access CGI interpreter")
        invalid("P31", "Missing semicolon after listen", raw="server {{\n listen 127.0.0.1:{port}\n root {root};\n}}", expected="Expected ';' after 'listen'")
        invalid("P32", "Missing server root (current parser rule)", raw="server {{\n listen 127.0.0.1:{port};\n location /x {{ root {root}; }}\n}}", expected="Missing required directive: root")
        invalid("P33", "Nonexistent server root", raw="server {{\n listen 127.0.0.1:{port};\n root /tmp/webserv_root_does_not_exist_42;\n}}", expected="Cannot access server root")

        def valid_single_listener():
            port = self.free_port()
            content, _ = self.base_config(port)
            proc, error = self.start_server(content, [port])
            if proc is None:
                return False, error
            output = self.stop_process(proc)
            return f"Bind successful: 127.0.0.1:{port}" in output, "single listen parsed and bound" if f"Bind successful: 127.0.0.1:{port}" in output else output[-1000:]
        self.add("P34", "Valid basic server config", valid_single_listener)

        def valid_multiple_listens():
            p1, p2 = self.free_port(), self.free_port()
            while p2 == p1:
                p2 = self.free_port()
            content = f"server {{\n listen 127.0.0.1:{p1};\n listen 127.0.0.1:{p2};\n root {self.root};\n}}\n"
            proc, error = self.start_server(content, [p1, p2])
            if proc is None:
                return False, error
            output = self.stop_process(proc)
            ok = f"Bind successful: 127.0.0.1:{p1}" in output and f"Bind successful: 127.0.0.1:{p2}" in output
            return ok, "both listen sockets bound" if ok else output[-1200:]
        self.add("P35", "Multiple listen directives in one server", valid_multiple_listens)

        def valid_multiple_servers():
            p1, p2 = self.free_port(), self.free_port()
            while p2 == p1:
                p2 = self.free_port()
            content = (
                f"server {{ listen 127.0.0.1:{p1}; root {self.root}; }}\n"
                f"server {{ listen 127.0.0.1:{p2}; root {self.root}; }}\n"
            )
            proc, error = self.start_server(content, [p1, p2])
            if proc is None:
                return False, error
            output = self.stop_process(proc)
            ok = f"Bind successful: 127.0.0.1:{p1}" in output and f"Bind successful: 127.0.0.1:{p2}" in output
            return ok, "both server blocks bound distinct ports" if ok else output[-1200:]
        self.add("P36", "Multiple server blocks", valid_multiple_servers)

        def valid_autoindex_values():
            port = self.free_port()
            content, _ = self.base_config(port, locations="location /list { autoindex on; }")
            proc, error = self.start_server(content, [port])
            if proc is None:
                return False, error
            output = self.stop_process(proc)
            return f"Bind successful: 127.0.0.1:{port}" in output, "autoindex on accepted by parser" if f"Bind successful: 127.0.0.1:{port}" in output else output[-1000:]
        self.add("P37", "Valid autoindex on", valid_autoindex_values)

        def valid_cgi_config():
            port = self.free_port()
            content, _ = self.base_config(port, locations=f"location /cgi {{ cgi .py {sys.executable}; }}")
            proc, error = self.start_server(content, [port])
            if proc is None:
                return False, error
            output = self.stop_process(proc)
            return f"Bind successful: 127.0.0.1:{port}" in output, "CGI configuration accepted and interpreter validated" if f"Bind successful: 127.0.0.1:{port}" in output else output[-1000:]
        self.add("P38", "Valid CGI config", valid_cgi_config)

    def run_runtime_group(self):
        def runtime_server(config, ports, action):
            proc, error = self.start_server(config, ports)
            if proc is None:
                return False, error
            try:
                return action()
            finally:
                self.stop_process(proc)

        def make_config(port, directives="", locations="", root=None):
            return self.base_config(port, directives, locations, root)[0]

        def get_index():
            port = self.free_port()
            config = make_config(port)
            def action():
                status, headers, body, _ = self.http_request(port, b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                return status == 200 and b"WEBSERV_TEST_INDEX" in body, f"HTTP {status}; body={body[:120]!r}"
            return runtime_server(config, [port], action)
        self.add("R01", "GET / serves the configured index", get_index)

        def get_missing():
            port = self.free_port()
            config = make_config(port)
            def action():
                status, _, body, _ = self.http_request(port, b"GET /does-not-exist HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                return status == 404, f"HTTP {status}; body={body[:120]!r}"
            return runtime_server(config, [port], action)
        self.add("R02", "GET missing resource returns 404", get_missing)

        def redirect():
            port = self.free_port()
            config = make_config(port, locations="location /old { return 301 /new; }")
            def action():
                status, headers, _, _ = self.http_request(port, b"GET /old HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                return status == 301 and headers.get("location") == "/new", f"HTTP {status}; Location={headers.get('location')}"
            return runtime_server(config, [port], action)
        self.add("R03", "Redirect sends status and Location", redirect)

        def multiple_listener_routing():
            p1, p2 = self.free_port(), self.free_port()
            while p1 == p2:
                p2 = self.free_port()
            root1 = self.tmp / "site_one"
            root2 = self.tmp / "site_two"
            root1.mkdir(exist_ok=True)
            root2.mkdir(exist_ok=True)
            (root1 / "index.html").write_text("SITE_ONE_CONTENT", encoding="utf-8")
            (root2 / "index.html").write_text("SITE_TWO_CONTENT", encoding="utf-8")
            config = f"server {{ listen 127.0.0.1:{p1}; root {root1}; }}\nserver {{ listen 127.0.0.1:{p2}; root {root2}; }}\n"
            def action():
                s1, _, b1, _ = self.http_request(p1, b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                s2, _, b2, _ = self.http_request(p2, b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                ok = s1 == 200 and s2 == 200 and b"SITE_ONE_CONTENT" in b1 and b"SITE_TWO_CONTENT" in b2
                return ok, f"port1: HTTP {s1} {b1[:60]!r}; port2: HTTP {s2} {b2[:60]!r}"
            return runtime_server(config, [p1, p2], action)
        self.add("R04", "Two listener ports route to their own server config", multiple_listener_routing)

        def get_method_restriction():
            port = self.free_port()
            config = make_config(port, locations="location /restricted { allowed_methods POST; }")
            def action():
                status, _, body, _ = self.http_request(port, b"GET /restricted/restricted.txt HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                return status == 405, f"expected 405 per allowed_methods; received HTTP {status}, body={body[:100]!r}"
            return runtime_server(config, [port], action)
        self.add("R05", "GET is rejected when not in allowed_methods", get_method_restriction)

        def post_upload():
            port = self.free_port()
            upload_root = self.tmp / "upload_runtime"
            upload_root.mkdir(exist_ok=True)
            config = make_config(port, locations=f"location /uploads {{ allowed_methods POST; upload on; upload_path {upload_root}; }}")
            boundary = "----webserv-test-boundary"
            body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"hello.txt\"\r\n"
                    "Content-Type: text/plain\r\n\r\nhello-upload\r\n"
                    f"--{boundary}--\r\n").encode("ascii")
            request = (f"POST /uploads HTTP/1.1\r\nHost: localhost\r\nContent-Type: multipart/form-data; boundary={boundary}\r\n"
                       f"Content-Length: {len(body)}\r\nConnection: close\r\n\r\n").encode("ascii") + body
            def action():
                status, _, response_body, _ = self.http_request(port, request)
                saved = upload_root / "hello.txt"
                return status == 201 and saved.exists() and saved.read_bytes() == b"hello-upload", f"expected 201 and saved file; got HTTP {status}, body={response_body[:100]!r}"
            return runtime_server(config, [port], action)
        self.add("R06", "POST multipart uploads a file", post_upload)

        def delete_allowed():
            port = self.free_port()
            config = make_config(port, locations="location /files { allowed_methods DELETE; }")
            target = self.root / "old.txt"
            if not target.exists():
                target.write_text("DELETE_ME", encoding="utf-8")
            def action():
                status, _, body, _ = self.http_request(port, b"DELETE /files/old.txt HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                return status == 204 and not target.exists(), f"HTTP {status}; file_exists={target.exists()}; body={body[:80]!r}"
            return runtime_server(config, [port], action)
        self.add("R07", "DELETE removes a file when allowed", delete_allowed)

        def delete_blocked():
            port = self.free_port()
            config = make_config(port, locations="location /files { allowed_methods GET; }")
            (self.root / "old.txt").write_text("DELETE_ME", encoding="utf-8")
            def action():
                status, _, _, _ = self.http_request(port, b"DELETE /files/old.txt HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                return status == 405, f"expected 405; got HTTP {status}"
            return runtime_server(config, [port], action)
        self.add("R08", "DELETE is rejected when method is not allowed", delete_blocked)

        def autoindex_runtime():
            port = self.free_port()
            listing = self.root / "listing"
            listing.mkdir(exist_ok=True)
            (listing / "visible.txt").write_text("VISIBLE", encoding="utf-8")
            config = make_config(port, locations=f"location /listing {{ root {listing}; autoindex on; }}")
            def action():
                status, _, body, _ = self.http_request(port, b"GET /listing/ HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                return status == 200 and b"visible.txt" in body, f"expected directory listing; got HTTP {status}, body={body[:120]!r}"
            return runtime_server(config, [port], action)
        self.add("R09", "autoindex on returns a directory listing", autoindex_runtime)

        def custom_error_page():
            port = self.free_port()
            (self.root / "custom404.html").write_text("CUSTOM_404_PAGE", encoding="utf-8")
            config = make_config(port, directives="error_page 404 /custom404.html;")
            def action():
                status, _, body, _ = self.http_request(port, b"GET /missing-custom HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                return status == 404 and b"CUSTOM_404_PAGE" in body, f"expected configured error page; got HTTP {status}, body={body[:120]!r}"
            return runtime_server(config, [port], action)
        self.add("R10", "Configured error_page content is served", custom_error_page)

        def body_limit():
            port = self.free_port()
            config = make_config(port, directives="client_max_body_size 5;")
            body = b"0123456789"
            request = (b"POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 10\r\nConnection: close\r\n\r\n" + body)
            def action():
                status, _, response_body, _ = self.http_request(port, request)
                return status == 413, f"expected 413; got HTTP {status}, body={response_body[:100]!r}"
            return runtime_server(config, [port], action)
        self.add("R11", "Request exceeding client_max_body_size returns 413", body_limit)

        def partial_header():
            port = self.free_port()
            config = make_config(port)
            def action():
                request1 = b"GET / HTTP/1.1\r\nHost: local"
                request2 = b"host\r\nConnection: close\r\n\r\n"
                with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
                    sock.settimeout(3)
                    sock.sendall(request1)
                    time.sleep(0.12)
                    sock.sendall(request2)
                    raw = bytearray()
                    while True:
                        data = sock.recv(65536)
                        if not data:
                            break
                        raw.extend(data)
                status_line = bytes(raw).split(b"\r\n", 1)[0]
                ok = status_line.startswith(b"HTTP/1.1 200") and b"WEBSERV_TEST_INDEX" in raw
                return ok, f"response status line={status_line!r}"
            return runtime_server(config, [port], action)
        self.add("R12", "Partial header read is completed after more bytes arrive", partial_header)

        def concurrent_clients():
            port = self.free_port()
            config = make_config(port)
            def action():
                results = []
                errors = []
                def worker():
                    try:
                        status, _, body, _ = self.http_request(port, b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n", timeout=5)
                        results.append(status == 200 and b"WEBSERV_TEST_INDEX" in body)
                    except Exception as exc:
                        errors.append(str(exc))
                threads = [threading.Thread(target=worker) for _ in range(20)]
                for thread in threads:
                    thread.start()
                for thread in threads:
                    thread.join(timeout=6)
                ok = len(results) == 20 and all(results) and not errors and all(not t.is_alive() for t in threads)
                return ok, f"successful={sum(results)}/{len(results)}, errors={errors[:3]}"
            return runtime_server(config, [port], action)
        self.add("R13", "20 concurrent GET clients", concurrent_clients)

        def cgi_execution():
            port = self.free_port()
            cgi_root = self.root / "cgi"
            cgi_root.mkdir(exist_ok=True)
            script = cgi_root / "hello.py"
            script.write_text('print("Content-Type: text/plain")\nprint()\nprint("CGI_OUTPUT_MARKER")\n', encoding="utf-8")
            config = make_config(port, locations=f"location /cgi {{ root {cgi_root}; cgi .py {sys.executable}; }}")
            def action():
                status, _, body, _ = self.http_request(port, b"GET /cgi/hello.py HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                ok = status == 200 and b"CGI_OUTPUT_MARKER" in body and b"print(" not in body
                return ok, f"expected CGI output; got HTTP {status}, body={body[:180]!r}"
            return runtime_server(config, [port], action)
        self.add("R14", "CGI script is executed and its output is returned", cgi_execution)

    def run_compliance_group(self):
        server_cpp = self.project_root / "src" / "Server.cpp"
        makefile = self.project_root / "Makefile"
        server_text = server_cpp.read_text(encoding="utf-8", errors="replace")
        make_text = makefile.read_text(encoding="utf-8", errors="replace")
        poll_calls = len(re.findall(r"\bpoll\s*\(", server_text))
        self.add("C01", "Exactly one poll() call in Server.cpp", lambda: (poll_calls == 1, f"found {poll_calls} poll() call(s)"))
        flags_ok = all(flag in make_text for flag in ("-Wall", "-Wextra", "-Werror", "-std=c++98"))
        self.add("C02", "Makefile has required C++98 warning flags", lambda: (flags_ok, "required compile flags present" if flags_ok else "missing one or more required flags"))
        forbidden_calls = []
        for source in (self.project_root / "src").glob("*.cpp"):
            text = source.read_text(encoding="utf-8", errors="replace")
            for func in ("lstat", "unlink"):
                if re.search(r"\b" + func + r"\s*\(", text):
                    forbidden_calls.append(f"{source.name}:{func}")
        self.add("C03", "Only subject-listed filesystem calls are used", lambda: (not forbidden_calls, "unlisted call(s): " + ", ".join(forbidden_calls) if forbidden_calls else "no lstat/unlink calls found"))

    def print_results(self, group):
        print(f"\n========== WEBSERV {group.upper()} TESTS ==========")
        for test_id, name, ok, detail in self.results:
            print(f"{'PASS' if ok else 'FAIL':4} {test_id}  {name}")
            if not ok:
                print(f"     {detail}")
        passed = sum(1 for _, _, ok, _ in self.results if ok)
        total = len(self.results)
        print("-----------------------------------------------")
        print(f"RESULT: {passed}/{total} PASS ({(passed / total * 100.0) if total else 0.0:.1f}%)")
        print("===============================================")


def main():
    parser = argparse.ArgumentParser(description="Black-box tester for the 42 Webserv project")
    parser.add_argument("--project", default=".", help="Webserv project root (default: current directory)")
    parser.add_argument("--binary", default="webserv", help="server executable relative to project root")
    parser.add_argument("--group", choices=("parser", "runtime", "compliance", "all"), default="all")
    parser.add_argument("--build", action="store_true", help="run make re before testing")
    parser.add_argument("--case", help="run only one case ID, e.g. P33 or R04")
    args = parser.parse_args()

    project_root = Path(args.project).resolve()
    binary = Path(args.binary)
    if not binary.is_absolute():
        binary = project_root / binary
    if args.build:
        build = subprocess.run(["make", "re"], cwd=str(project_root), text=True, capture_output=True)
        if build.returncode != 0:
            print(build.stdout)
            print(build.stderr, file=sys.stderr)
            print("BUILD FAILED")
            return 2
        print("Build: PASS (make re)")
    if not binary.exists():
        print(f"Executable not found: {binary}. Run 'make' or use --build.")
        return 2

    tester = WebservTester(project_root, binary, args.case)
    try:
        if args.group in ("parser", "all"):
            tester.run_parser_group()
        if args.group in ("runtime", "all"):
            tester.run_runtime_group()
        if args.group in ("compliance", "all"):
            tester.run_compliance_group()
        tester.print_results(args.group)
        return 0 if all(result[2] for result in tester.results) else 1
    finally:
        tester.close()


if __name__ == "__main__":
    sys.exit(main())
