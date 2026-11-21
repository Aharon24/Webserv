*This project has been created as part of the 42 curriculum by ahapetro, aramarak.*

# Webserv

A lightweight HTTP/1.1 web server written in **C++98** as part of the 42 curriculum. The goal of this project is to understand how a web server receives HTTP requests, routes them to resources or programs, and sends HTTP responses back to clients.

Webserv serves static files, handles the `GET`, `POST`, and `DELETE` methods, supports configurable routes, and can execute CGI programs based on file extensions. It uses non-blocking file descriptors and a single `poll()` event loop to manage listening sockets, clients, and CGI pipes.

## Contents

- [Features](#features)
- [Architecture](#architecture)
- [Requirements](#requirements)
- [Build](#build)
- [Run](#run)
- [Configuration](#configuration)
- [CGI setup](#cgi-setup)
- [Examples](#examples)
- [Testing](#testing)
- [Resources](#resources)
- [AI usage](#ai-usage)

## Features

### HTTP server

- HTTP request parsing and response generation.
- `GET`, `POST`, and `DELETE` method handling.
- Static website and file serving.
- Configurable index files and directory listing (`autoindex`).
- Configurable error pages and redirects.
- Request body size limits through `client_max_body_size`.
- Support for `Content-Length` and chunked request bodies.
- Multiple listening address/port pairs.
- Route-specific roots and allowed methods.

### Non-blocking I/O

- Non-blocking listening and client sockets.
- A single `poll()` event loop monitors listening sockets, client sockets, and CGI pipes.
- Client reads and writes are performed after the corresponding readiness notification.
- Partial socket reads and writes are handled.
- Client disconnects and idle connection timeouts are handled.

### CGI

- CGI execution is selected by file extension in a location configuration.
- CGI programs receive request information through environment variables, including method, URI, query string, content length, content type, script path, and HTTP headers.
- The CGI process is started with `fork()` and `execve()`.
- Request data is passed to CGI standard input; CGI output is read from standard output.
- Chunked request bodies are decoded before being passed to CGI, and end-of-input is signalled by closing the CGI input pipe.
- CGI output can be streamed to the client. When the CGI response does not provide a content length, the end of the CGI output stream marks the end of its body.
- The CGI process runs from the script's directory so relative file access can work as expected.

### File uploads

- Multipart form-data parsing is separated from socket handling.
- Uploaded file data is handled as binary data.
- Filename and destination directory checks are performed.
- New files are created without overwriting an existing destination file.
- Incomplete files are removed when a write fails.

## Architecture

The implementation is divided into modules with separate responsibilities:

| Module | Responsibility |
|---|---|
| `Tokenizer` | Converts configuration text into tokens. |
| `Parser` | Validates configuration directives and builds server/location configuration objects. |
| `Server` | Owns sockets, the `poll()` loop, client lifecycle, request handling, and CGI event coordination. |
| `HttpRequest` | Parses HTTP request lines, headers, body framing, and chunked bodies. |
| `Router` | Normalizes request URIs, selects the best matching location, and maps URIs to filesystem paths. |
| `StaticFileHandler` | Serves static files, directory index files, autoindex pages, redirects, and configured error pages. |
| `HttpResponse` | Builds HTTP status lines, headers, and response bodies. |
| `CgiHandler` | Creates CGI processes and manages their input/output pipes and lifecycle. |
| `Upload` | Parses multipart form-data and saves uploaded files. |

## Requirements

- A Unix-like operating system (Linux or macOS).
- A C++ compiler supporting the C++98 standard (`c++`).
- `make`.
- No Boost or third-party C++ libraries are required.

The project is compiled with:

```text
-Wall -Wextra -Werror -std=c++98
```

## Build

From the repository root:

```bash
make
```

Useful Makefile targets:

```bash
make        # Build the webserv executable
make all    # Build the project
make clean  # Remove object files
make fclean # Remove object files and the executable
make re     # Clean and rebuild everything
```

## Run

Run with an explicit configuration file:

```bash
./webserv config.conf
```

Run with the application's default configuration path:

```bash
./webserv
```

The server runs in the foreground. Stop it with `Ctrl+C`.

Make sure the configured port is available before starting the server. If a CGI interpreter is configured using a relative path, start Webserv from the directory where that path is valid.

## Configuration

The configuration syntax is inspired by the `server` section of NGINX. A configuration contains one or more `server` blocks. Each block can define listening addresses, a document root, index files, error pages, body-size limits, and location-specific rules.

Example:

```nginx
server {
    listen 127.0.0.1:8080;
    server_name localhost;

    root ./www;
    index index.html;

    error_page 400 /errors/400.html;
    error_page 403 /errors/403.html;
    error_page 404 /errors/404.html;
    error_page 500 /errors/500.html;

    client_max_body_size 128M;

    location / {
        allowed_methods GET;
        root ./www;
        index index.html;
        autoindex on;
    }

    location /post_body {
        allowed_methods POST;
        root ./www;
        client_max_body_size 100;
    }

    location /directory/ {
        allowed_methods GET POST;
        root ./YoupiBanane;
        index youpi.bad_extension;
        cgi .bla ./cgi_tester;
    }

    location /cgi-bin {
        allowed_methods GET POST;
        root ./www;
        cgi .py /usr/bin/python3;
        cgi .bla ./cgi_tester;
    }
}
```

### Main directives

| Directive | Purpose |
|---|---|
| `listen` | Defines an IPv4 address and port for the server to listen on. Multiple `listen` directives can be used. |
| `server_name` | Defines the server name. |
| `root` | Sets the filesystem root used to resolve request paths. A location can override the server root. |
| `index` | Defines the default file served when a directory is requested. |
| `error_page` | Maps an HTTP error status to an error-page URI. |
| `client_max_body_size` | Sets the maximum accepted request body size. Size suffixes such as `K`, `M`, and `G` can be used where supported by the parser. |
| `location` | Defines rules for a URI prefix. The most specific matching location is selected. |
| `allowed_methods` | Lists the HTTP methods accepted by a location. |
| `return` | Configures an HTTP redirect for a location. |
| `autoindex` | Enables or disables directory listing. |
| `upload` / `upload_path` | Enables file uploads for a location and defines their destination, where configured. |
| `cgi` | Associates a file extension with a CGI interpreter or executable. |

A location root replaces the matched location prefix when constructing the filesystem path. For example, with `location /images/` and `root ./www`, `/images/logo.png` maps to `./www/logo.png`.

## CGI setup

Before starting the server, make sure that:

1. The configured CGI executable exists.
2. The executable has execute permission.
3. The configured extension matches the requested script's extension.
4. The script is present under the filesystem path produced by the selected location's `root` and the requested URI.

For example, if the configuration contains:

```nginx
location /directory/ {
    root ./YoupiBanane;
    cgi .bla ./cgi_tester;
}
```

make the CGI executable available at the configured relative path and grant execute permission:

```bash
chmod +x ./cgi_tester
```

A request for `/directory/youpi.bla` is then mapped through the `/directory/` location and the `.bla` CGI rule.

## Examples

Start Webserv in one terminal:

```bash
./webserv config.conf
```

Request the home page from another terminal:

```bash
curl -i http://localhost:8080/
```

Request a resource that does not exist:

```bash
curl -i http://localhost:8080/does-not-exist
```

Send a small POST request to a CGI route:

```bash
curl -i \
  -X POST \
  -H 'Content-Type: application/octet-stream' \
  --data-binary 'webserv-cgi-test' \
  http://localhost:8080/directory/youpi.bla
```

Send a binary file as a POST body:

```bash
curl -i \
  -H 'Content-Type: application/octet-stream' \
  --data-binary @payload.bin \
  http://localhost:8080/directory/youpi.bla
```

For a configured multipart upload location, an example request is:

```bash
curl -i -F 'file=@example.txt' http://localhost:8080/uploads/
```

The upload example requires a location with uploads enabled and a valid upload destination configured.

## Testing

### Provided tester

The 42 subject includes a small tester. It is optional, but it is useful for checking common behaviours. With Webserv running on port `8080`, run:

```bash
./tester http://localhost:8080
```

Follow the tester's interactive prompts. Before the CGI tests, ensure its required fixture directory and CGI executable are present and executable, and that the configuration matches the tester's requested routes.

### Manual checks

Use `curl` to inspect status codes, response headers, and response bodies. For large request bodies, compare the sent and received files when the CGI program is expected to echo or transform the body. Also monitor memory usage during concurrent requests:

```bash
free -h
```

A successful compilation does not by itself prove that all runtime tests pass. Run the provided tester and additional tests for concurrent clients, large request bodies, uploads, malformed requests, client disconnects, and CGI failures.

### Basic testing

For a configuration that listens on `127.0.0.1:8080`, example requests are:

```sh
# Retrieve a resource
curl -i http://127.0.0.1:8080/

# Send a POST request (the endpoint must allow POST)
curl -i -X POST --data 'hello' http://127.0.0.1:8080/uploads/

# Delete a file (the endpoint must allow DELETE)
curl -i -X DELETE http://127.0.0.1:8080/uploads/example.txt
```
These are **illustrative commands**, not a guarantee that these exact paths are enabled. The configured routes and allowed HTTP methods determine the results.

## Resources

- [RFC 9110 — HTTP Semantics](https://www.rfc-editor.org/rfc/rfc9110)
- [RFC 9112 — HTTP/1.1](https://www.rfc-editor.org/rfc/rfc9112)
- [RFC 3875 — The Common Gateway Interface (CGI) Version 1.1](https://www.rfc-editor.org/rfc/rfc3875)
- [NGINX Documentation](https://nginx.org/en/docs/)
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)
- The `poll(2)`, `socket(2)`, `accept(2)`, `recv(2)`, `send(2)`, `pipe(2)`, `fork(2)`, `execve(2)`, and `waitpid(2)` manual pages.

## AI usage

AI tools were used as development support for:

- Explaining HTTP request/response structure, sockets, `poll()`, non-blocking I/O, and CGI communication.
- Reviewing selected code paths and helping identify potential edge cases in request parsing, CGI streaming, file uploads, and connection handling.
- Suggesting focused debugging commands and manual test cases.
- Helping organize technical notes and project documentation.

