#!/usr/bin/env python3
"""Small integration tester for a 42 Webserv project.

Uses only Python's standard library. Run the server before running this script.
See --help for configurable paths and --install-cgi-fixtures for test scripts.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import http.client
import os
import sys
import time
import urllib.parse
from dataclasses import dataclass
from typing import Dict, Optional, Tuple


@dataclass
class Response:
    status: int
    reason: str
    headers: Dict[str, str]
    body: bytes


class Tester:
    def __init__(self, base_url: str, timeout: float) -> None:
        parsed = urllib.parse.urlparse(base_url)
        if parsed.scheme not in ("http", "https") or not parsed.hostname:
            raise ValueError("Base URL must look like http://127.0.0.1:8080")
        self.scheme = parsed.scheme
        self.host = parsed.hostname
        self.port = parsed.port or (443 if parsed.scheme == "https" else 80)
        self.base_path = parsed.path.rstrip("/")
        self.timeout = timeout

    def request(
        self,
        method: str,
        path: str,
        body: Optional[bytes] = None,
        headers: Optional[Dict[str, str]] = None,
    ) -> Response:
        if not path.startswith("/"):
            path = "/" + path
        full_path = self.base_path + path
        connection_class = (
            http.client.HTTPSConnection
            if self.scheme == "https"
            else http.client.HTTPConnection
        )
        connection = connection_class(self.host, self.port, timeout=self.timeout)
        try:
            connection.request(method, full_path, body=body, headers=headers or {})
            response = connection.getresponse()
            response_body = response.read()
            response_headers = {key.lower(): value for key, value in response.getheaders()}
            return Response(
                response.status,
                response.reason,
                response_headers,
                response_body,
            )
        finally:
            connection.close()


def body_text(response: Response) -> str:
    return response.body.decode("utf-8", errors="replace")


def install_fixtures(directory: str) -> None:
    os.makedirs(directory, exist_ok=True)
    fixtures = {
        "hello.py": '''#!/usr/bin/env python3\nimport sys\nsys.stdout.write("Content-Type: text/plain\\r\\n\\r\\n")\nsys.stdout.write("Hello from CGI!\\n")\n''',
        "env.py": '''#!/usr/bin/env python3\nimport os\nimport sys\nsys.stdout.write("Content-Type: text/plain\\r\\n\\r\\n")\nsys.stdout.write("QUERY_STRING=" + os.environ.get("QUERY_STRING", "") + "\\n")\nsys.stdout.write("REQUEST_METHOD=" + os.environ.get("REQUEST_METHOD", "") + "\\n")\nsys.stdout.write("CONTENT_LENGTH=" + os.environ.get("CONTENT_LENGTH", "") + "\\n")\n''',
        "echo.py": '''#!/usr/bin/env python3\nimport os\nimport sys\nlength_text = os.environ.get("CONTENT_LENGTH", "0") or "0"\ntry:\n    length = int(length_text)\nexcept ValueError:\n    length = 0\ndata = sys.stdin.buffer.read(length)\nsys.stdout.buffer.write(b"Content-Type: application/octet-stream\\r\\n\\r\\n")\nsys.stdout.buffer.write(data)\n''',
    }
    for filename, content in fixtures.items():
        path = os.path.join(directory, filename)
        with open(path, "w", encoding="utf-8", newline="") as file:
            file.write(content)
        try:
            os.chmod(path, 0o644)
        except OSError:
            pass
        print("[CREATED]", path)


def main() -> int:
    parser = argparse.ArgumentParser(description="Integration tests for Webserv and CGI")
    parser.add_argument("--base-url", default="http://127.0.0.1:8080")
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--root-path", default="/")
    parser.add_argument("--missing-path", default="/__webserv_tester_file_that_should_not_exist_83917")
    parser.add_argument("--static-path", help="Optional known static file path, e.g. /index.html")
    parser.add_argument("--cgi-path", default="/cgi/hello.py", help="CGI hello.py URL path")
    parser.add_argument("--query-cgi-path", default="/cgi/env.py", help="CGI env.py URL path")
    parser.add_argument("--post-cgi-path", default="/cgi/echo.py", help="CGI echo.py URL path")
    parser.add_argument("--install-cgi-fixtures", metavar="DIR", help="Create hello.py, env.py and echo.py in DIR, then exit")
    parser.add_argument("--skip-cgi", action="store_true", help="Run only ordinary HTTP tests")
    parser.add_argument("--skip-concurrency", action="store_true")
    parser.add_argument("--workers", type=int, default=5)
    args = parser.parse_args()

    if args.install_cgi_fixtures:
        install_fixtures(args.install_cgi_fixtures)
        print("\nPut these files under your configured document root and make sure your config maps .py to Python CGI.")
        return 0

    try:
        tester = Tester(args.base_url, args.timeout)
    except ValueError as error:
        print("[FATAL]", error)
        return 2

    passed = 0
    failed = 0
    skipped = 0

    def run_test(name: str, callback) -> None:
        nonlocal passed, failed
        try:
            detail = callback()
            passed += 1
            print("[PASS]", name, "-", detail)
        except Exception as error:  # Keep running the remaining tests.
            failed += 1
            print("[FAIL]", name, "-", error)

    def skip_test(name: str) -> None:
        nonlocal skipped
        skipped += 1
        print("[SKIP]", name)

    def test_server_reachable() -> str:
        response = tester.request("GET", args.root_path)
        if not (200 <= response.status < 500):
            raise AssertionError(f"Unexpected HTTP status {response.status} {response.reason}")
        return f"HTTP {response.status} {response.reason}"

    def test_missing_path() -> str:
        response = tester.request("GET", args.missing_path)
        if response.status != 404:
            raise AssertionError(f"Expected 404, received {response.status}; body={body_text(response)[:160]!r}")
        return "returned 404 Not Found"

    run_test("server accepts an HTTP request", test_server_reachable)
    run_test("unknown path returns 404", test_missing_path)

    if args.static_path:
        def test_static_file() -> str:
            response = tester.request("GET", args.static_path)
            if response.status != 200:
                raise AssertionError(f"Expected 200, received {response.status}; body={body_text(response)[:160]!r}")
            if not response.body:
                raise AssertionError("Static file response body is empty")
            return f"returned {len(response.body)} bytes"
        run_test("static file GET", test_static_file)
    else:
        skip_test("static file GET (pass --static-path to enable)")

    if not args.skip_cgi:
        def test_cgi_hello() -> str:
            response = tester.request("GET", args.cgi_path)
            text = body_text(response)
            if response.status != 200:
                raise AssertionError(f"Expected 200, received {response.status}; body={text[:200]!r}")
            if text.strip() != "Hello from CGI!":
                raise AssertionError(f"CGI marker not found in response body: {text[:240]!r}")
            return "CGI script executed and returned its marker"

        def test_cgi_query_string() -> str:
            query = urllib.parse.urlencode({"name": "Webserv", "check": "query"})
            separator = "&" if "?" in args.query_cgi_path else "?"
            response = tester.request("GET", args.query_cgi_path + separator + query)
            text = body_text(response)
            if response.status != 200:
                raise AssertionError(f"Expected 200, received {response.status}; body={text[:200]!r}")
            expected = "QUERY_STRING=" + query
            if expected not in text:
                raise AssertionError(f"Expected {expected!r} in CGI output; received {text[:240]!r}")
            if "REQUEST_METHOD=GET" not in text:
                raise AssertionError("CGI did not receive REQUEST_METHOD=GET")
            return "QUERY_STRING and REQUEST_METHOD passed to CGI"

        def test_cgi_post_body() -> str:
            payload = b"Webserv CGI POST body test: 12345"
            response = tester.request(
                "POST",
                args.post_cgi_path,
                body=payload,
                headers={"Content-Type": "text/plain", "Content-Length": str(len(payload))},
            )
            if response.status != 200:
                raise AssertionError(f"Expected 200, received {response.status}; body={body_text(response)[:200]!r}")
            if response.body != payload:
                raise AssertionError(
                    f"CGI did not echo exact POST body; expected {payload!r}, got {response.body!r}"
                )
            return f"CGI received and echoed all {len(payload)} bytes"

        run_test("CGI GET execution", test_cgi_hello)
        run_test("CGI query string environment", test_cgi_query_string)
        run_test("CGI POST body via stdin", test_cgi_post_body)
    else:
        skip_test("CGI GET execution (--skip-cgi)")
        skip_test("CGI query string environment (--skip-cgi)")
        skip_test("CGI POST body via stdin (--skip-cgi)")

    if not args.skip_concurrency:
        workers = max(1, args.workers)
        path = args.cgi_path if not args.skip_cgi else args.root_path

        def one_parallel_request(index: int) -> Tuple[int, bool]:
            response = tester.request("GET", path)
            if args.skip_cgi:
                marker_ok = 200 <= response.status < 500
            else:
                marker_ok = body_text(response).strip() == "Hello from CGI!"
            return response.status, marker_ok

        def test_concurrency() -> str:
            with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
                results = list(executor.map(one_parallel_request, range(workers)))
            if args.skip_cgi:
                bad = [item for item in results if not (200 <= item[0] < 500) or not item[1]]
            else:
                bad = [item for item in results if item[0] != 200 or not item[1]]
            if bad:
                raise AssertionError(f"{len(bad)}/{workers} parallel requests failed; results={bad}")
            return f"{workers} simultaneous requests completed successfully"

        run_test("concurrent client requests", test_concurrency)
    else:
        skip_test("concurrent client requests (--skip-concurrency)")

    print("\n========== SUMMARY ==========")
    print(f"Passed: {passed}")
    print(f"Failed: {failed}")
    print(f"Skipped: {skipped}")
    print("=============================")

    if failed:
        print("Some tests failed. Check your Webserv terminal output and the configured routes.")
        return 1
    print("All enabled tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
