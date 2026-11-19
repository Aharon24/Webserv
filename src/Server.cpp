#include "Server.hpp"
#include "Router.hpp"
#include "StaticFileHandler.hpp"
#include "HttpResponse.hpp"
#include "Upload.hpp"

#include <set>


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

// Location matching lives in Router (longest prefix on whole path segments).
const LocationConfig* Server::find_location(const std::string &path, const ServerConfig &conf) const
{
    return Router(conf).find_location(path);
}

const CgiConfig *Server::find_cgi_config(const std::string &uri,const LocationConfig *location) const
{
    if (location == NULL)
        return NULL;

    std::string::size_type slash = uri.find_last_of('/');
    std::string::size_type filename_start = 0;

    if (slash != std::string::npos)
        filename_start = slash + 1;

    std::string::size_type dot = uri.find_last_of('.');

    if (dot == std::string::npos ||
        dot <= filename_start ||
        dot + 1 >= uri.size())
        return NULL;

    std::string extension = uri.substr(dot);

    for (std::size_t i = 0; i < location->cgi.size(); ++i)
    {
        if (location->cgi[i].extension == extension)
            return &location->cgi[i];
    }

    return NULL;
}

bool Server::start_cgi(ClientConnection &client,const HttpRequest &request,const std::string &script_path,const std::string &interpreter_path,std::string &error_status)
{
    CgiHandler *cgi = new CgiHandler(
        client.fd,
        request,
        script_path,
        interpreter_path
    );

    if (!cgi->start(error_status))
    {
        delete cgi;
        return false;
    }

    try
    {
        _cgi_processes.push_back(cgi);
    }
    catch (...)
    {
        delete cgi;
        throw;
    }

    client.cgi_pending = true;
    client.last_activity = time(NULL);

    return true;
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

// GET is implemented by StaticFileHandler (see build_response()).

/*
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


/**

    location->allowed_methods; // Must include "POST"
    location->upload_enabled;  // Must be true
    location->upload_path;     // Must be a valid directory path
    location->root;            // Used to resolve relative upload_path
    location->path;            // Used to resolve relative upload_path
    location->return_code;     // Optional redirect for POST
    location->return_url;      // Optional redirect for POST
*/

void Server::handle_post(const HttpRequest &request, const ServerConfig &conf, std::string &status, std::string &body)
{
    // Find the location matching the request URI.
    const LocationConfig *location = find_location(request.path, conf);

    std::cout << "POST method received!" << std::endl;
    std::cout << "Request path: " << request.path << std::endl;

    if (location != NULL)
        std::cout << "Location found: " << location->path << std::endl;
    else
        std::cout << "Location not found" << std::endl;

    if (location != NULL && !location->allowed_methods.empty())
    {
        bool allowed = false;
    
        for(std::size_t i = 0; i < location->allowed_methods.size(); ++i)
        {
            if (location->allowed_methods[i] == "POST")
            {
                allowed = true;
                break;
            }
        }
    
        if (!allowed)
        {
            status = "405 Method Not Allowed";
            body = "405 Method Not Allowed\n";
            return;
        }
    }

    // Check whether file uploads are enabled.
    if (location == NULL || !location->upload_enabled)
    {
        status = "403 Forbidden";
        body = "File uploads are not allowed\n";
        return;
    }

    // Check whether the upload destination is configured.
    if (location->upload_path.empty())
    {
        status = "500 Internal Server Error";
        body = "Upload path is not configured\n";
        return;
    }

    

    const std::map<std::string, std::string>::const_iterator type = request.headers.find("content-type");
    if (type == request.headers.end())
    {
        status = "400 Bad Request";
        body = "Missing Content-Type\n";
        return;
    }
    std::string media_type = type->second.substr(0, type->second.find(';'));
    const std::string::size_type last = media_type.find_last_not_of(" \t");
    media_type.erase(last == std::string::npos ? 0 : last + 1);
    for (std::size_t i = 0; i < media_type.size(); ++i)
        if (media_type[i] >= 'A' && media_type[i] <= 'Z')
            media_type[i] = static_cast<char>(media_type[i] + ('a' - 'A'));
    if (media_type != "multipart/form-data")
    {
        status = "415 Unsupported Media Type";
        body = "Expected multipart/form-data\n";
        return;
    }

    std::vector<Upload::UploadedFile> files;
    std::string error;
    if (!Upload::parse_multipart(type->second, request.body, files, error)
        || files.empty())
    {
        status = "400 Bad Request";
        body = "Invalid multipart upload or no files provided\n";
        return;
    }

    // Detect duplicate names before creating any files.
    std::set<std::string> names;
    for (std::size_t i = 0; i < files.size(); ++i)
    {
        if (!names.insert(files[i].filename).second)
        {
            status = "409 Conflict";
            body = "Duplicate upload filename\n";
            return;
        }
    }
    for (std::size_t i = 0; i < files.size(); ++i)
    {
        if (!Upload::save_file(location->upload_path, files[i].filename,
                               files[i].content))
        {
            const int saved_error = errno;
            bool cleaned = true;
            // Roll back only files successfully created by this request.
            for (std::size_t j = 0; j < i; ++j)
            {
                std::string path = location->upload_path;
                if (path[path.size() - 1] != '/')
                    path += '/';
                path += files[j].filename;
                if (std::remove(path.c_str()) != 0)
                    cleaned = false;
            }
            if (!cleaned)
                status = "500 Internal Server Error";
            else if (saved_error == EEXIST)
                status = "409 Conflict";
            else if (saved_error == EACCES || saved_error == EPERM
                     || saved_error == EROFS)
                status = "403 Forbidden";
            else if (saved_error == EINVAL)
                status = "400 Bad Request";
            else
                status = "500 Internal Server Error";
            body = status + "\n";
            return;
        }
    }
    status = "201 Created";
    body = "Files uploaded\n";
}





/*
** DELETE integration TODO:
** - HttpParser::parse(): validate bytes, decode URI once and normalize paths.
** - Server::find_location(): match trailing-slash locations and normalized paths.
** - Server::build_response()/build_file_path(): use the same normalized path;
**   replace substring ".." checks with component checks in the shared router.
** - Server::create_response_data(): add Allow for 405; omit Content-Length for 204.
** - Confirm std::remove() permission under the subject's external-function list.
** - stat() follows symlinks: lexical root checks do not prevent symlink escapes.
** - TOCTOU remains between stat() and std::remove(); review permitted APIs.
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
    // Match the trailing-slash handling used by Router::build_path().
    while (normalized_root.size() > 1
        && normalized_root[normalized_root.size() - 1] == '/')
        normalized_root.erase(normalized_root.size() - 1);
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

    // stat() follows symlinks; the root prefix above is only a lexical check.
    struct stat info;
    if (stat(file_path.c_str(), &info) == -1)
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

    if (std::remove(file_path.c_str()) != 0)
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


// Builds status line + headers for a status string ("404 Not Found") and a body.
// The caller appends the body itself. Header generation lives in HttpResponse.
std::string Server::create_response_data(const HttpRequest &request, std::string &status, std::string &body)
{
    return HttpResponse::from_status_line(status, body).build_head(request.version);
}

namespace
{
    // POST / DELETE handlers report "<status line>" + body. Success responses
    // are sent as they are; 4xx/5xx ones get the configured (or built-in) error page.
    std::string trim_cgi(const std::string &value)
    {
        std::string::size_type first =
            value.find_first_not_of(" \t\r");

        if (first == std::string::npos)
            return "";

        std::string::size_type last =
            value.find_last_not_of(" \t\r");

        return value.substr(first, last - first + 1);
    }

    std::string lower_cgi(const std::string &value)
    {
        std::string result = value;

        for (std::size_t i = 0; i < result.size(); ++i)
        {
            result[i] = static_cast<char>(
                std::tolower(static_cast<unsigned char>(result[i]))
            );
        }

        return result;
    }

    bool valid_cgi_header_name(const std::string &name)
    {
        if (name.empty())
            return false;

        for (std::size_t i = 0; i < name.size(); ++i)
        {
            unsigned char c = static_cast<unsigned char>(name[i]);

            if (!std::isalnum(c) && c != '-' && c != '_')
                return false;
        }

        return true;
    }

    bool build_cgi_http_response(
        const std::string &output,
        const std::string &version,
        std::string &response_data)
    {
        std::string::size_type separator =
            output.find("\r\n\r\n");

        std::size_t separator_size = 4;

        if (separator == std::string::npos)
        {
            separator = output.find("\n\n");
            separator_size = 2;
        }

        if (separator == std::string::npos)
            return false;

        std::string header_block = output.substr(0, separator);
        std::string body = output.substr(separator + separator_size);

        HttpResponse response(200);

        std::string content_type;
        bool content_type_found = false;
        bool status_found = false;
        bool location_found = false;

        std::string::size_type line_start = 0;

        while (line_start < header_block.size())
        {
            std::string::size_type line_end =
                header_block.find('\n', line_start);

            if (line_end == std::string::npos)
                line_end = header_block.size();

            std::string line =
                header_block.substr(line_start, line_end - line_start);

            if (!line.empty() && line[line.size() - 1] == '\r')
                line.erase(line.size() - 1);

            std::string::size_type colon = line.find(':');

            if (colon == std::string::npos)
                return false;

            std::string name = trim_cgi(line.substr(0, colon));
            std::string value = trim_cgi(line.substr(colon + 1));

            if (!valid_cgi_header_name(name))
                return false;

            name = lower_cgi(name);

            if (name == "status")
            {
                if (status_found || value.size() < 3)
                    return false;

                if (!std::isdigit(static_cast<unsigned char>(value[0])) ||
                    !std::isdigit(static_cast<unsigned char>(value[1])) ||
                    !std::isdigit(static_cast<unsigned char>(value[2])))
                    return false;

                if (value.size() > 3 &&
                    value[3] != ' ' && value[3] != '\t')
                    return false;

                int code = std::atoi(value.substr(0, 3).c_str());

                if (code < 200 || code > 599)
                    return false;

                std::string reason = trim_cgi(value.substr(3));

                if (reason.empty())
                    response.set_status(code);
                else
                    response.set_status(code, reason);

                status_found = true;
            }
            else if (name == "content-type")
            {
                if (content_type_found || value.empty())
                    return false;

                content_type = value;
                content_type_found = true;
            }
            else if (name == "location")
            {
                if (value.empty())
                    return false;

                response.set_header("Location", value);
                location_found = true;
            }
            else if (name != "content-length" &&
                     name != "connection" &&
                     name != "transfer-encoding" &&
                     name != "date" &&
                     name != "server")
            {
                response.set_header(name, value);
            }

            if (line_end == header_block.size())
                break;

            line_start = line_end + 1;
        }

        if (!content_type_found && !location_found)
            return false;

        if (!status_found && location_found)
            response.set_status(302);

        if (content_type.empty())
            content_type = "text/plain; charset=UTF-8";

        response.set_body(body, content_type);
        response_data = response.build(version);

        return true;
    }
    HttpResponse finish_handler_response(const StaticFileHandler &files, const std::string &status, const std::string &body)
    {
        HttpResponse response = HttpResponse::from_status_line(status, body);
    
        if (response.get_status() >= 400)
            return files.make_error(response.get_status());
        return response;
    }
}

std::string Server::build_response(ClientConnection &client,const HttpRequest &request,const ServerConfig &conf)
{
    const Router router(conf);
    const StaticFileHandler files(conf, router);
    const RouteResult route = router.route(request);
    HttpResponse response;

    switch (route.status)
    {
        case ROUTE_BAD_REQUEST:
            response = files.make_error(400);
            break;

        case ROUTE_FORBIDDEN:
            response = files.make_error(403);
            break;

        case ROUTE_REDIRECT:
            response = files.make_redirect(
                route.redirect_code,
                route.redirect_url
            );
            break;

        case ROUTE_METHOD_NOT_ALLOWED:
            response = files.make_error(405);
            response.set_header("Allow", route.allow);
            break;

        case ROUTE_OK:
        {
            HttpRequest routed = request;
            std::string status;
            std::string body;

            routed.path = route.uri;

            if (request.method == "GET" || request.method == "POST")
            {
                const CgiConfig *cgi_config =
                    find_cgi_config(route.uri, route.location);

                if (cgi_config != NULL)
                {
                    std::string error_status;

                    if (start_cgi(
                            client,
                            routed,
                            route.fs_path,
                            cgi_config->interpreter,
                            error_status))
                    {
                        return "";
                    }

                    int error_code = std::atoi(error_status.c_str());

                    if (error_code < 400 || error_code > 599)
                        error_code = 500;

                    response = files.make_error(error_code);
                    break;
                }
            }

            if (request.method == "GET")
                response = files.handle_get(routed, route);
            else if (request.method == "POST")
            {
                handle_post(routed, conf, status, body);
                response = finish_handler_response(files, status, body);
            }
            else
            {
                handle_delete(routed, conf, status, body);
                response = finish_handler_response(files, status, body);
            }
            break;
        }
    }

    return response.build(request.version);
}



// root + URI (without the matched location prefix); see Router::build_path().
std::string Server::build_file_path(const HttpRequest &request,const LocationConfig *location,const ServerConfig &conf) const
{
    return Router(conf).build_path(request.path, location);
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
    bool chunked = false;
    return HttpParser::get_body_framing(headers_part, chunked, content_length);
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
        std::cout << "Path: [" << request.path << "]" << std::endl;
        std::cout << "Query: [" << request.query_string << "]" << std::endl;
        std::cout << "========================" << std::endl;

        client.response_buffer = build_response(client, request, conf);
        client.request_complete = true;
        if (!client.cgi_pending)
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

    bool chunked = false;
    std::size_t content_length = 0;
    if (!HttpParser::get_body_framing(headers_part, chunked, content_length))
    {
        HttpRequest dummy_req;
        dummy_req.version = "HTTP/1.1";
        std::string status = "400 Bad Request";
        std::string body = status + "\n";
        client.response_buffer = create_response_data(dummy_req, status, body) + body;
        client.request_complete = true;
        client.response_ready = true;
        return true;
    }

    if (chunked)
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

        if (clients[i].cgi_pending)
            client_poll.events = 0;
        else if (clients[i].response_ready)
            client_poll.events = POLLOUT;
        else
            client_poll.events = POLLIN;

        client_poll.revents = 0;

        fds.push_back(client_poll);
    }
    for (std::size_t i = 0; i < _cgi_processes.size(); ++i)
    {
        CgiHandler *cgi = _cgi_processes[i];

        int stdin_fd = -1;
        int stdout_fd = -1;

        if (cgi != NULL)
        {
            stdin_fd = cgi->get_stdin_fd();
            stdout_fd = cgi->get_stdout_fd();
        }

        struct pollfd input_poll;

        input_poll.fd = stdin_fd;
        input_poll.events = 0;
        input_poll.revents = 0;

        if (stdin_fd != -1)
            input_poll.events = POLLOUT;

        fds.push_back(input_poll);

        struct pollfd output_poll;

        output_poll.fd = stdout_fd;
        output_poll.events = 0;
        output_poll.revents = 0;

        if (stdout_fd != -1)
            output_poll.events = POLLIN;

        fds.push_back(output_poll);
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

void Server::process_client_events(const std::vector<struct pollfd> &fds,std::vector<ClientConnection> &clients,HttpParser &parser,std::size_t polled_client_count)
{
    for (std::size_t i = _server_fds.size(); i < _server_fds.size() + polled_client_count && i < fds.size(); ++i)
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


void Server::process_cgi_events(const std::vector<struct pollfd> &fds,std::vector<ClientConnection> &clients,std::size_t polled_client_count,std::size_t polled_cgi_count)
{
    const std::size_t cgi_base =
        _server_fds.size() + polled_client_count;

    std::vector<CgiHandler *> finished_cgi;

    for (std::size_t i = 0;
         i < polled_cgi_count && i < _cgi_processes.size();
         ++i)
    {
        const std::size_t input_index = cgi_base + i * 2;
        const std::size_t output_index = input_index + 1;

        if (output_index >= fds.size())
            break;

        CgiHandler *cgi = _cgi_processes[i];

        if (cgi == NULL)
            continue;

        // Find the client that started this CGI process.
        std::vector<ClientConnection>::iterator client_it =
            clients.begin();

        while (client_it != clients.end() &&
               client_it->fd != cgi->get_client_fd())
        {
            ++client_it;
        }

        bool client_exists = (client_it != clients.end());

        if (!client_exists)
            cgi->terminate();

        bool io_ok = true;
        std::string error_status;

        if (client_exists && client_it->cgi_pending)
        {
            // Handle CGI stdin.
            short input_events = fds[input_index].revents;
        
            if (cgi->get_stdin_fd() != -1)
            {
                if (input_events & POLLNVAL)
                    io_ok = false;
                else if (input_events & (POLLERR | POLLHUP))
                    cgi->close_stdin();
                else if (input_events & POLLOUT)
                {
                    if (!cgi->handle_stdin_write(error_status))
                        io_ok = false;
                }
            }
        
            // Handle CGI stdout.
            short output_events = fds[output_index].revents;
        
            if (cgi->get_stdout_fd() != -1)
            {
                if (output_events & (POLLNVAL | POLLERR))
                    io_ok = false;
                else if (output_events & POLLIN)
                {
                    if (!cgi->handle_stdout_read(error_status))
                        io_ok = false;
                }
                else if (output_events & POLLHUP)
                    cgi->close_stdout();
            }
        }

        // Check whether the child process has exited.
        if (!cgi->check_child(error_status))
            io_ok = false;

        // A pipe or child-process error produces 502.
        if (!io_ok && client_exists && client_it->cgi_pending)
        {
            cgi->terminate();

            const ServerConfig &conf = get_server_config(*client_it);
            const Router router(conf);
            const StaticFileHandler files(conf, router);

            client_it->response_buffer =
                files.make_error(502).build(cgi->get_http_version());

            client_it->cgi_pending = false;
            client_it->response_ready = true;
            client_it->last_activity = time(NULL);
        }

        // CGI is complete only after child exit and both pipes close.
        if (cgi->is_finished())
        {
            if (client_exists && client_it->cgi_pending)
            {
                std::string cgi_response;

                if (cgi->was_successful() &&
                    build_cgi_http_response(
                        cgi->get_output(),
                        cgi->get_http_version(),
                        cgi_response))
                {
                    client_it->response_buffer = cgi_response;
                }
                else
                {
                    const ServerConfig &conf =
                        get_server_config(*client_it);

                    const Router router(conf);
                    const StaticFileHandler files(conf, router);

                    client_it->response_buffer =
                        files.make_error(502).build(cgi->get_http_version());
                }

                client_it->cgi_pending = false;
                client_it->response_ready = true;
                client_it->last_activity = time(NULL);
            }

            finished_cgi.push_back(cgi);
        }
    }

    // Remove completed CGI handlers after processing the original poll array.
    for (std::size_t i = 0; i < finished_cgi.size(); ++i)
    {
        for (std::vector<CgiHandler *>::iterator it =
                 _cgi_processes.begin();
             it != _cgi_processes.end();
             ++it)
        {
            if (*it == finished_cgi[i])
            {
                delete *it;
                _cgi_processes.erase(it);
                break;
            }
        }
    }
}
void Server::run(HttpParser &parser)
{
    std::vector<ClientConnection> clients;
    const time_t TIMEOUT_SECONDS = 5;

    while (true)
    {
        check_timeouts(clients, TIMEOUT_SECONDS);

        const std::size_t polled_client_count = clients.size();
        const std::size_t polled_cgi_count = _cgi_processes.size();

        std::vector<struct pollfd> fds = build_poll_fds(clients);

        int result = wait_for_events(fds, 1000);

        if (result < 0)
            continue;

        if (result > 0)
        {
            accept_ready_connections(fds, clients);

            process_client_events(
                fds,
                clients,
                parser,
                polled_client_count
            );
        }

        process_cgi_events(
            fds,
            clients,
            polled_client_count,
            polled_cgi_count
        );
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