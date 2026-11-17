#include "Server.hpp"


Server::Server(const std::vector<ServerConfig> &configs) : _configs(configs)
{
    std::signal(SIGPIPE, SIG_IGN);

    if (_configs.empty())
        throw std::runtime_error("No server configurations");

    collect_listens();
}

void Server::set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1)
        throw std::runtime_error("Failed to get socket flags");

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        throw std::runtime_error("Failed to set non-blocking mode");
}

void Server::create_socket()
{
    for (std::size_t i = 0; i < _listens.size(); ++i)
    {
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1)
            throw std::runtime_error("Failed to create socket");
        int reuse = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1)
        {
            close(server_fd);
            throw std::runtime_error("Failed to set SO_REUSEADDR");
        }
        try
        {
            set_nonblocking(server_fd);
        }
        catch (...)
        {
            close(server_fd);
            server_fd = -1;
            throw;
        }
        _server_fds.push_back(server_fd);
        std::cout << "Created socket FD: " << server_fd << " for port: " << _listens[i].port << std::endl;
    }

}


void Server::listen_socket()
{
    for (std::size_t i = 0; i < _server_fds.size(); ++i)
    {
        if (listen(_server_fds[i], 128) == -1)
            throw std::runtime_error("Failed to listen on socket");
    }
}

void Server::bind_socket()
{
    for (std::size_t i = 0; i < _server_fds.size(); ++i)
    {
        struct sockaddr_in address;

        std::memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons(_listens[i].port);
        if (inet_pton(AF_INET,_listens[i].ip.c_str(), &address.sin_addr) != 1)
            throw std::runtime_error("Invalid IPv4 address");
        if (bind(_server_fds[i], reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) == -1)
            throw std::runtime_error("Failed to bind socket");
        std::cout << "Bind successful: " << _listens[i].ip << ":" << _listens[i].port << std::endl;
    }
}

int Server::accept_connection(int server_fd)
{
    int client_fd = accept(server_fd, NULL, NULL);

    if (client_fd == -1)
    {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNABORTED)
            return -1;
        throw std::runtime_error("Failed to accept connection");
    }
    try
    {
        set_nonblocking(client_fd);
    }
    catch (...)
    {
        close(client_fd);
        return -1;
    }
    return client_fd;
}

const LocationConfig* Server::find_location(const std::string &path, const ServerConfig &conf) const
{
    const LocationConfig *best = NULL;

    for (std::size_t i = 0; i < conf.locations.size(); ++i)
    {
        const std::string &prefix = conf.locations[i].path;
        bool matches = false;

        if (prefix.empty())
            continue;

        if (path.size() >= prefix.size() && path.compare(0, prefix.size(), prefix) == 0)
        {
            if (path.size() == prefix.size())
                matches = true;
            else if (prefix == "/")
                matches = true;
            else if (path[prefix.size()] == '/')
                matches = true;
        }
        if (matches &&(best == NULL || prefix.size() > best->path.size()))
            best = &conf.locations[i];
    }
    return best;
}



void Server::collect_listens()
{
    _listens.clear();
    _listen_config_indices.clear();


    for(std::size_t i = 0; i < _configs.size(); i++)
    {
        for(std::size_t j = 0; j < _configs[i].listens.size(); j++)
        {
            const ListenConfig &current = _configs[i].listens[j];
            for(std::size_t l = 0; l < _listens.size(); l++)
            {
                if(current.ip == _listens[l].ip && current.port == _listens[l].port)
                    throw std::runtime_error("Duplicate listen address");
            }
            _listens.push_back(current);
            _listen_config_indices.push_back(i);
        }
    }
    std::cout << "\n========== COLLECTED LISTENS ==========" << std::endl;
    for (std::size_t i = 0; i < _listens.size(); ++i)
    {
        std::cout << "Listen index: " << i << std::endl;
        std::cout << "IP: " << _listens[i].ip << std::endl;
        std::cout << "Port: " << _listens[i].port << std::endl;
        std::cout << "_listen_config_indices[" << i << "] = "
              << _listen_config_indices[i] << std::endl;
        std::cout << "-----------------------------" << std::endl;
    }
}

void Server::handle_get(const HttpRequest &request,const ServerConfig &conf,std::string &status,std::string &body)
{
    std::string file_path;
    const LocationConfig *location = find_location(request.path, conf);
    file_path = build_file_path(request, location, conf);
    struct stat info;
    if (stat(file_path.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
    {
        std::string index;
    
        if (location != NULL && !location->index.empty())
            index = location->index;
        else
            index = conf.index;
        if (!file_path.empty() && file_path[file_path.size() - 1] != '/')
            file_path += '/';
        file_path += index;
    }
    std::ifstream file(file_path.c_str(), std::ios::in | std::ios::binary);
    if(!file)
    {
        status = "404 Not Found";
        body = "404 Not Found\n";
    }
    else
    {
        std::ostringstream content;
        content << file.rdbuf();
        body = content.str();
        status = "200 OK";
    }
}


**
** PURPOSE:
** Handle HTTP POST requests and save uploaded files using
** the existing Upload module.
**
** TODO 1: Upload module
** [x] Implement Upload::save_file().
** [x] Implement Upload::parse_multipart().
** [x] Support binary data and empty files.
** [x] Validate filenames and destination directories.
** [ ] Complete standalone Upload tests.
**
** TODO 2: HTTP request body (TEAM)
** [ ] Ensure exact Content-Length body extraction.
** [ ] Reject invalid Content-Length / Transfer-Encoding combinations.
** [ ] Decode chunked bodies exactly once.
** [ ] Enforce client_max_body_size.
** [ ] Ensure POST receives the complete validated request.body.
**
** TODO 3: POST configuration
** [ ] Pass ServerConfig to handle_post().
** [ ] Find the matching LocationConfig.
** [ ] Check allowed_methods for POST.
** [ ] Check upload_enabled.
** [ ] Validate upload_path.
**
** TODO 4: Multipart integration
** [ ] Read Content-Type from request.headers.
** [ ] Validate multipart/form-data.
** [ ] Call Upload::parse_multipart().
** [ ] Reject malformed multipart requests.
** [ ] Reject multipart requests without files.
** [ ] Detect duplicate filenames within one request.
**
** TODO 5: File storage
** [ ] Call Upload::save_file() for each uploaded file.
** [ ] Allow valid empty files.
** [ ] Never overwrite existing files.
** [ ] Track files created by the current request.
** [ ] Roll back created files if any upload fails.
** [ ] Handle filesystem errors using errno.
** [ ] Confirm file I/O compatibility with the event loop.
**
** TODO 6: HTTP responses
** [ ] Return 201 Created after successful upload.
** [ ] Return 400 Bad Request for invalid multipart data.
** [ ] Return 403 Forbidden when uploading is disabled.
** [ ] Return 405 Method Not Allowed when POST is not allowed.
** [ ] Return 409 Conflict when a filename already exists.
** [ ] Return 413 Payload Too Large when size limits are exceeded.
** [ ] Return 415 Unsupported Media Type for unsupported Content-Type.
** [ ] Return 500 Internal Server Error for unexpected storage errors.
** [ ] Coordinate response headers with the shared Response module.
**
** TODO 7: Integration tests
** [ ] Upload a normal text file.
** [ ] Upload a binary file containing '\0'.
** [ ] Upload an empty file.
** [ ] Upload multiple files.
** [ ] Test malformed multipart data.
** [ ] Test duplicate and unsafe filenames.
** [ ] Test an existing destination filename.
** [ ] Test disabled upload and forbidden POST.
** [ ] Test Content-Length and chunked requests.
** [ ] Test client_max_body_size.
** [ ] Test rollback when the second file fails.
** [ ] Verify that unrelated files remain unchanged.
** [ ] Test concurrent clients during file uploads.
**
** RULES:
** - C++98 only (-Wall -Wextra -Werror -std=c++98).
** - Preserve the existing architecture and coding style.
** - Reuse Upload.cpp; do not duplicate multipart parsing.
** - Do not duplicate HTTP parsing or socket handling.
** - Do not modify teammates' modules without coordination.
** - No temporary workarounds or incomplete solutions.
** - Add meaningful comments in English.
** - Mark TODO items complete only after verification.
** ============================================================
*/


/**
request.method;                  // "POST"
request.path;                    // "/uploads"
request.body;                    // Complete, correctly framed body
request.headers["content-type"]; // Multipart Content-Type
*/
void Server::handle_post(const HttpRequest &request, std::string &status, std::string &body)
{
        
    std::cout << "POST method received!" << std::endl;
    std::cout << "Request path: " << request.path << std::endl;

    status = "501 Not Implemented";
    body = "POST is not implemented yet\n";
}

/*
** DELETE integration TODO:
** - HttpParser::parse(): validate bytes, decode URI once and normalize paths.
** - Server::find_location(): match trailing-slash locations and normalized paths.
** - Server::build_response()/build_file_path(): use the same normalized path;
**   replace substring ".." checks with component checks in the shared router.
** - Server::create_response_data(): add Allow for 405; omit Content-Length for 204.
** - Verify lstat() against the official subject's allowed functions.
** - TOCTOU: lstat() + unlink() requires a stable directory tree. Review permitted
**   descriptor-relative APIs before claiming safety against concurrent changes.
*/

void Server::handle_delete(const HttpRequest &request, const ServerConfig &conf,std::string &status, std::string &body)
{
    const LocationConfig *location = find_location(request.path, conf);

    if (location != NULL && !location->allowed_methods.empty())
    {
        bool allowed = false;

        for (std::size_t i = 0; i < location->allowed_methods.size(); ++i)
        {
            if (location->allowed_methods[i] == "DELETE")
                allowed = true;
        }
        if (!allowed)
        {
            status = "405 Method Not Allowed";
            body = "405 Method Not Allowed\n";
            return;
        }
    }

    if (request.path.empty() || request.path[0] != '/')
    {
        status = "403 Forbidden";
        body = "403 Forbidden\n";
        return;
    }

    std::string root = conf.root;
    if (location != NULL && !location->root.empty())
        root = location->root;

    if (root.empty())
    {
        status = "500 Internal Server Error";
        body = "Invalid server root\n";
        return;
    }

    const std::string file_path = build_file_path(request, location, conf);

    std::string normalized_root = root;
    if (normalized_root[normalized_root.size() - 1] != '/')
        normalized_root += '/';

    if (file_path.compare(0, normalized_root.size(), normalized_root) != 0)
    {
        status = "403 Forbidden";
        body = "403 Forbidden\n";
        return;
    }

    // C filesystem calls must see the complete mapped path.
    if (file_path.find('\0') != std::string::npos)
    {
        status = "400 Bad Request";
        body = "400 Bad Request\n";
        return;
    }

    // Reject parent components in the mapped suffix without decoding the URI.
    const std::string relative_path = file_path.substr(normalized_root.size());
    std::size_t start = 0;
    while (start < relative_path.size())
    {
        std::size_t end = relative_path.find('/', start);
        if (end == std::string::npos)
            end = relative_path.size();
        if (relative_path.substr(start, end - start) == "..")
        {
            status = "403 Forbidden";
            body = "403 Forbidden\n";
            return;
        }
        start = end + 1;
    }

    if (file_path.find_first_not_of('/') == std::string::npos)
    {
        status = "403 Forbidden";
        body = "403 Forbidden\n";
        return;
    }

    // Inspect every prefix, including the configured root, without following links.
    // This check does not prevent another process from replacing a directory.
    struct stat info;
    bool checked = false;
    start = 0;
    while (start < file_path.size())
    {
        std::size_t end = file_path.find('/', start);
        if (end == std::string::npos)
            end = file_path.size();
        if (end == start)
        {
            start = end + 1;
            continue;
        }
        const std::string prefix = file_path.substr(0, end);
        if (lstat(prefix.c_str(), &info) == -1)
        {
            checked = false;
            break;
        }
        if (S_ISLNK(info.st_mode))
        {
            status = "403 Forbidden";
            body = "403 Forbidden\n";
            return;
        }
        if (end < file_path.size() && !S_ISDIR(info.st_mode))
        {
            status = "404 Not Found";
            body = "404 Not Found\n";
            return;
        }
        checked = true;
        start = end + 1;
    }
    if (!checked)
    {
        if (errno == ENOENT || errno == ENOTDIR)
            status = "404 Not Found";
        else if (errno == EACCES || errno == EPERM)
            status = "403 Forbidden";
        else
            status = "500 Internal Server Error";
        body = status + "\n";
        return;
    }

    if (!S_ISREG(info.st_mode))
    {
        status = "403 Forbidden";
        body = "403 Forbidden\n";
        return;
    }

    if (unlink(file_path.c_str()) == -1)
    {
        if (errno == ENOENT || errno == ENOTDIR)
            status = "404 Not Found";
        else if (errno == EACCES || errno == EPERM || errno == EROFS)
            status = "403 Forbidden";
        else
            status = "500 Internal Server Error";

        body = status + "\n";
        return;
    }

    status = "204 No Content";
    body.clear();
}


std::string Server::create_response_data(const HttpRequest &request, std::string &status, std::string &body)
{
    std::ostringstream header;
    header << request.version << " " << status << "\r\n";
    header << "Content-Length: " << body.size() << "\r\n";
    header << "Content-Type: text/html; charset=UTF-8\r\n";
    header << "Connection: close\r\n";
    header << "\r\n";
    return (header.str());
}

std::string Server::build_response(const HttpRequest &request,const ServerConfig &conf)
{
    std::string body;
    std::string status;

    const LocationConfig *location = find_location(request.path, conf);

    if (location != NULL && location->return_code != 0)
    {
        std::ostringstream ss;
        ss << request.version << " " << location->return_code << " Moved Permanently\r\n"
           << "Location: " << location->return_url << "\r\n"
           << "Content-Length: 0\r\n"
           << "Connection: close\r\n\r\n";
        return ss.str();
    }

    if (request.path.empty() ||
        request.path[0] != '/' ||
        request.path.find("..") != std::string::npos)
    {
        status = "403 Forbidden";
        body = "403 Forbidden\n";
    }
    else if (request.method == "GET")
        handle_get(request, conf, status, body);
    else if (request.method == "POST")
        handle_post(request, status, body);
    else if (request.method == "DELETE")
        handle_delete(request, conf, status, body);
    else
    {
        status = "405 Method Not Allowed";
        body = "405 Method Not Allowed\n";
    }

    return create_response_data(request, status, body) + body;
}



std::string Server::build_file_path(const HttpRequest &request,const LocationConfig *location,const ServerConfig &conf) const
{
    std::string root;
    std::string relative_path;

    if (location != NULL)
    {
        if (location->root.empty())
            root = conf.root;
        else
            root = location->root;
        relative_path = request.path.substr(location->path.size());
    }
    else
    {
        root = conf.root;
        relative_path = request.path;   
    }
    if (!relative_path.empty() && relative_path[0] == '/')
        relative_path.erase(0, 1);
    if (!root.empty() && root[root.size() - 1] != '/')
        root += '/';
    return root + relative_path;
}


void Server::remove_client(int client_fd, std::vector<ClientConnection> &clients)
{
    for (std::vector<ClientConnection>::iterator it = clients.begin(); it != clients.end(); ++it)
    {
        if (it->fd == client_fd)
        {
            std::cout << "Closing client FD: " << it->fd << std::endl;
            close(it->fd);
            clients.erase(it);
            return;
        }
    }
}



bool Server::get_content_length(const std::string &headers_part,std::size_t &content_length) const
{
    content_length = 0;
    bool found = false;
    std::string::size_type line_start = 0;

    while (line_start < headers_part.size())
    {
        std::string::size_type line_end = headers_part.find("\r\n", line_start);

        if (line_end == std::string::npos)
            line_end = headers_part.size();

        std::string::size_type colon = headers_part.find(':', line_start);

        if (colon != std::string::npos && colon < line_end)
        {
            std::string name = headers_part.substr(line_start, colon - line_start);

            for (std::string::size_type i = 0; i < name.size(); ++i)
            {
                if (name[i] >= 'A' && name[i] <= 'Z')
                    name[i] = name[i] - 'A' + 'a';
            }

            if (name == "content-length")
            {
                if (found)
                    return false;

                found = true;

                std::string::size_type start = colon + 1;

                while (start < line_end &&
                       (headers_part[start] == ' ' ||
                        headers_part[start] == '\t'))
                    ++start;

                std::string::size_type end = line_end;

                while (end > start && (headers_part[end - 1] == ' ' || headers_part[end - 1] == '\t'))
                    --end;

                if (start == end)
                    return false;

                std::size_t value = 0;

                for (std::string::size_type i = start; i < end; ++i)
                {
                    if (headers_part[i] < '0' ||
                        headers_part[i] > '9')
                        return false;

                    std::size_t digit =
                        static_cast<std::size_t>(headers_part[i] - '0');

                    if (value >
                        (std::numeric_limits<std::size_t>::max() - digit) / 10)
                        return false;

                    value = value * 10 + digit;
                }

                content_length = value;
            }
        }

        if (line_end == headers_part.size())
            break;

        line_start = line_end + 2;
    }

    return true;
}



bool Server::check_max_body_size(ClientConnection &client, std::size_t content_length, const ServerConfig &conf)
{
    if (conf.client_max_body_size > 0 && content_length > conf.client_max_body_size)
    {
        std::cerr << "Error: Request body exceeds client_max_body_size" << std::endl;
        HttpRequest dummy_req;
        dummy_req.version = "HTTP/1.1";
        std::string status = "413 Payload Too Large";
        std::string body = "413 Payload Too Large\n";
        client.response_buffer = create_response_data(dummy_req, status, body) + body;
        client.request_complete = true;
        client.response_ready = true;
        return true;
    }
    return false;
}


void Server::parse_and_respond(ClientConnection &client, HttpParser &http_parser, const ServerConfig &conf)
{
    try
    {
        HttpRequest request = http_parser.parse(client.request_buffer);
        std::cout << "===== HTTP REQUEST =====" << std::endl;
        std::cout << "Method:  " << request.method << std::endl;
        std::cout << "Path:    " << request.path << std::endl;
        std::cout << "Version: " << request.version << std::endl;
        std::cout << "Host:    " << request.host << std::endl;
        std::cout << "========================" << std::endl;

        client.response_buffer = build_response(request, conf);
        client.request_complete = true;
        client.response_ready = true;
    }
    catch(const std::exception &e)
    {
        std::cerr << "Parse error: " << e.what() << std::endl;
        HttpRequest request;
        request.version = "HTTP/1.1";
        
        std::string err_msg = e.what();
        std::string status = "400 Bad Request";
        
        if (err_msg.size() >= 3 && std::isdigit(err_msg[0]) && std::isdigit(err_msg[1]) && std::isdigit(err_msg[2]))
        {
            status = err_msg;
        }
        
        std::string body = status + "\n";
        client.response_buffer = create_response_data(request, status, body) + body;
        client.request_complete = true;
        client.response_ready = true;
    }
}


bool Server::read_client_data(ClientConnection &client, bool &stop_processing)
{
    const std::size_t MAX_HEADER_SIZE = 32 * 1024;

    char buffer[4096];
    stop_processing = false;

    ssize_t bytes_read = recv(client.fd, buffer, sizeof(buffer), 0);

    if (bytes_read == 0)
        return false;

    if (bytes_read < 0)
    {
        stop_processing = true;
        return true;
    }

    client.last_activity = time(NULL);

    client.request_buffer.append(
        buffer,
        static_cast<std::string::size_type>(bytes_read)
    );

    std::string::size_type header_end = client.request_buffer.find("\r\n\r\n");

    if (header_end == std::string::npos)
    {
        if (client.request_buffer.size() > MAX_HEADER_SIZE)
        {
            client.header_too_large = true;
            stop_processing = true;
        }
    }
    else if (header_end + 4 > MAX_HEADER_SIZE)
    {
        client.header_too_large = true;
        stop_processing = true;
    }

    return true;
}

bool Server::is_chunked(const std::string &headers_part) const
{
    std::string::size_type line_start = 0;

    while (line_start < headers_part.size())
    {
        std::string::size_type line_end = headers_part.find("\r\n", line_start);

        if (line_end == std::string::npos)
            line_end = headers_part.size();

        std::string::size_type colon = headers_part.find(':', line_start);

        if (colon != std::string::npos && colon < line_end)
        {
            std::string name = headers_part.substr(line_start, colon - line_start);

            for (std::string::size_type i = 0; i < name.size(); ++i)
            {
                if (name[i] >= 'A' && name[i] <= 'Z')
                    name[i] = name[i] - 'A' + 'a';
            }

            if (name == "transfer-encoding")
            {
                std::string::size_type token_start = colon + 1;

                while (token_start < line_end)
                {
                    std::string::size_type comma_pos =
                        headers_part.find(',', token_start);

                    if (comma_pos == std::string::npos || comma_pos > line_end)
                        comma_pos = line_end;

                    std::string::size_type token_end = comma_pos;

                    while (token_start < token_end && (headers_part[token_start] == ' ' || headers_part[token_start] == '\t'))
                        ++token_start;

                    while (token_end > token_start &&
                           (headers_part[token_end - 1] == ' ' ||
                            headers_part[token_end - 1] == '\t'))
                        --token_end;

                    std::string token =
                        headers_part.substr(
                            token_start, token_end - token_start);

                    for (std::string::size_type i = 0;
                         i < token.size(); ++i)
                    {
                        if (token[i] >= 'A' && token[i] <= 'Z')
                            token[i] = token[i] - 'A' + 'a';
                    }

                    if (token == "chunked")
                        return true;

                    if (comma_pos == line_end)
                        break;

                    token_start = comma_pos + 1;
                }
            }
        }

        if (line_end == headers_part.size())
            break;

        line_start = line_end + 2;
    }

    return false;
}


void Server::handle_large_header(ClientConnection &client)
{
    HttpRequest dummy_req;
    dummy_req.version = "HTTP/1.1";

    std::string status = "431 Request Header Fields Too Large";
    std::string body = status + "\n";

    client.response_buffer =
        create_response_data(dummy_req, status, body) + body;

    client.request_complete = true;
    client.response_ready = true;
}

bool Server::handle_client_event(ClientConnection &client, HttpParser &http_parser)
{
    bool stop_processing = false;
    if (!read_client_data(client, stop_processing))
        return false;
    if (client.header_too_large)
    {
        handle_large_header(client);
        return true;
    }
    if (stop_processing)
        return true;

    std::string::size_type header_end = client.request_buffer.find("\r\n\r\n");
    if (header_end == std::string::npos)
        return true; 
    const ServerConfig &conf = get_server_config(client);

    std::string headers_part = client.request_buffer.substr(0, header_end);
    std::string::size_type body_start = header_end + 4;

    if (is_chunked(headers_part))
    {
        std::string decoded_body;

        ChunkedStatus chunked_status = HttpParser::unchunk_body(client.request_buffer.substr(body_start), decoded_body, conf.client_max_body_size);

        if (chunked_status == CHUNKED_INCOMPLETE)
            return true;

        if (chunked_status == CHUNKED_INVALID)
        {
            parse_and_respond(client, http_parser, conf);
            return true;
        }
        if (chunked_status == CHUNKED_TOO_LARGE)
        {
            HttpRequest dummy_req;
            dummy_req.version = "HTTP/1.1";

            std::string status = "413 Payload Too Large";
            std::string body = status + "\n";

            client.response_buffer =
                create_response_data(dummy_req, status, body) + body;

            client.request_complete = true;
            client.response_ready = true;
            return true;
        }

        if (check_max_body_size(client, decoded_body.size(), conf))
            return true;
    }
    else
    {
        std::size_t content_length = 0;

        if (!get_content_length(headers_part, content_length))
        {
            HttpRequest dummy_req;
            dummy_req.version = "HTTP/1.1";

            std::string status = "400 Bad Request";
            std::string body = status + "\n";

            client.response_buffer =
                create_response_data(dummy_req, status, body) + body;

            client.request_complete = true;
            client.response_ready = true;
            return true;
        }

        if (check_max_body_size(client, content_length, conf))
            return true;

        if (client.request_buffer.size() - body_start < content_length)
            return true;
    }

    parse_and_respond(client, http_parser, conf);
    return true;
}

const ServerConfig &Server::get_server_config(const ClientConnection &client) const
{
    if (client.listener_index >= _listen_config_indices.size())
        throw std::runtime_error("Invalid listener index");

    std::size_t config_index = _listen_config_indices[client.listener_index];
    if (config_index >= _configs.size())
        throw std::runtime_error("Invalid config index");

    return _configs[config_index];
}



bool Server::handle_client_write(ClientConnection &client)
{
    if (client.sent_bytes >= client.response_buffer.size())
        return false;

    ssize_t bytes_sent = send(client.fd, client.response_buffer.c_str() + client.sent_bytes,
        client.response_buffer.size() - client.sent_bytes,
        0);

    if (bytes_sent < 0)
        return false;

    if (bytes_sent == 0)
        return false;

    client.sent_bytes += static_cast<std::size_t>(bytes_sent);
    client.last_activity = time(NULL);

    if (client.sent_bytes < client.response_buffer.size())
        return true;
    std::cout << "Response fully sent to client FD: " << client.fd << std::endl;

    bool close_connection =(client.response_buffer.find("Connection: close") != std::string::npos);
    if (close_connection)
        return false;
    client.reset();
    return true;
}  

const ServerConfig *Server::find_server_config(const std::string &host) const
{
    if (_configs.empty())
        return NULL;
    
    std::string host_name = host;

    std::string::size_type colon = host_name.find(':');
    if (colon != std::string::npos)
        host_name.erase(colon);
    for (std::size_t i = 0; i < host_name.size(); ++i)
    {
        host_name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(host_name[i])));
    }
    for (std::size_t i = 0; i < _configs.size(); ++i)
    {
        if (_configs[i].server_name.empty())
            continue;

        std::string configured_name = _configs[i].server_name;

        for (std::size_t j = 0; j < configured_name.size(); ++j)
        {
            configured_name[j] = static_cast<char>(std::tolower(static_cast<unsigned char>(configured_name[j])));
        }
        if (configured_name == host_name)
            return &_configs[i];
    }
    return  NULL;
}

std::vector<struct pollfd> Server::build_poll_fds(const std::vector<ClientConnection> &clients) const
{
    std::vector<struct pollfd> fds;

    for (std::size_t i = 0; i < _server_fds.size(); ++i)
    {
        struct pollfd server_poll;
        server_poll.fd = _server_fds[i];
        server_poll.events = POLLIN;
        server_poll.revents = 0;
        fds.push_back(server_poll);
    }

    for (std::size_t i = 0; i < clients.size(); ++i)
    {
        struct pollfd client_poll;
        client_poll.fd = clients[i].fd;
        client_poll.events = clients[i].response_ready ? POLLOUT : POLLIN;
        client_poll.revents = 0;
        fds.push_back(client_poll);
    }
    return fds;
}


void Server::accept_ready_connections(const std::vector<struct pollfd> &fds, std::vector<ClientConnection> &clients)
{
    for (std::size_t i = 0; i < _server_fds.size(); ++i)
    {
        if (_server_fds[i] == -1)
            continue;

        if (fds[i].revents & POLLIN)
        {
            int client_fd = accept_connection(_server_fds[i]);

            if (client_fd != -1)
            {
                std::cout << "Accepted client: " << client_fd << std::endl;
                clients.push_back(ClientConnection(client_fd, i));
            }
        }

        if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            std::cerr << "Error: Listening socket FD " << _server_fds[i]
                      << " encountered error/hangup/invalid state." << std::endl;
            close(_server_fds[i]);
            _server_fds[i] = -1;
        }
    }
}


int Server::wait_for_events(std::vector<struct pollfd> &fds, int timeout_ms) const
{
    if (fds.empty())
        return 0;

    int result = poll(&fds[0], fds.size(),timeout_ms);

    if (result == -1)
    {
        if (errno == EINTR)
            return -1;

        throw std::runtime_error("poll failed");
    }
    return result;
}

void Server::process_client_events(const std::vector<struct pollfd> &fds, std::vector<ClientConnection> &clients, HttpParser &parser)
{
    for (std::size_t i = _server_fds.size(); i < fds.size(); ++i)
    {
        int client_fd = fds[i].fd;
        short events = fds[i].revents;
        if (events == 0)
            continue;
        std::vector<ClientConnection>::iterator it = clients.begin();

        while (it != clients.end() && it->fd != client_fd)
            ++it;

        if (it == clients.end())
            continue;

        if (events & POLLNVAL)
        {
            remove_client(client_fd, clients);
            continue;
        }
        bool keep_client = true;
        if (events & POLLIN)
            keep_client = handle_client_event(*it, parser);

        if (keep_client && (events & POLLOUT))
            keep_client = handle_client_write(*it);

        if (events & POLLERR)
            keep_client = false;

        if ((events & POLLHUP) && !(events & POLLIN) && !(events & POLLOUT))
            keep_client = false;

        if (!keep_client)
            remove_client(client_fd, clients);
    }
}


void Server::check_timeouts(std::vector<ClientConnection> &clients, time_t timeout_seconds)
{
    time_t current_time = time(NULL);
    for(std::size_t i = 0; i < clients.size(); )
    {
        if(current_time - clients[i].last_activity > timeout_seconds)
        {
            std::cout << "Client FD " << clients[i].fd << " timed out. Closing connection." << std::endl;
            close(clients[i].fd);
            clients.erase(clients.begin() + i);
        }
        else
            ++i;
    }
}

void Server::run(HttpParser &parser)
{
    std::vector<ClientConnection> clients;
    const time_t TIMEOUT_SECONDS = 5;
    while (true)
    {
        check_timeouts(clients, TIMEOUT_SECONDS);

        std::vector<struct pollfd> fds;

        fds = build_poll_fds(clients);

        int result = wait_for_events(fds, 1000);
        if (result <= 0)
            continue;       
        accept_ready_connections(fds, clients);
        
        process_client_events(fds, clients, parser);
    }
}


Server::~Server()
{
    for (std::size_t i = 0; i < _server_fds.size(); ++i)
    {
        if (_server_fds[i] != -1)
            close(_server_fds[i]);
    }
}