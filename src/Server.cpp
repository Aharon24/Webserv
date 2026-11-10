#include "Server.hpp"
#include <limits.h> // PATH_MAX for resolved filesystem paths


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
        if (errno == EAGAIN || errno == EWOULDBLOCK)
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
        throw;
    }
    return client_fd;
}



void Server::send_all(int client_fd, const std::string &data)
{
    std::string::size_type sent = 0;
    while (sent < data.size())
    {
        ssize_t result = send(
            client_fd,
            data.c_str() + sent,
            data.size() - sent,
            0
        );

        if (result <= 0)
            throw std::runtime_error("Failed to send HTTP response");

        sent += static_cast<std::string::size_type>(result);
    }
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
void Server::handle_post(const HttpRequest &request, std::string &status, std::string &body)
{
        
    std::cout << "POST method received!" << std::endl;
    std::cout << "Request path: " << request.path << std::endl;

    status = "501 Not Implemented";
    body = "POST is not implemented yet\n";
}

/*
** ============================================================
** HTTP DELETE - TODO
** 42 Webserv | Programmer 4
** ============================================================
**
** TODO 1: Configuration
** [ ] Receive the current ServerConfig.
** [ ] Find the matching LocationConfig.
** [ ] Check whether DELETE is allowed.
**
** TODO 2: Resolve target
** [ ] Use the existing build_file_path().
** [ ] Prevent path traversal.
** [ ] Prevent deletion outside the allowed root.
**
** TODO 3: Validate target
** [ ] Check whether the target exists.
** [ ] Reject directories.
** [ ] Handle inaccessible targets.
**
** TODO 4: Delete file
** [ ] Call unlink().
** [ ] Check the return value.
** [ ] Handle errno correctly.
**
** TODO 5: HTTP response
** [ ] Return 204 No Content on success.
** [ ] Return 404 for a missing resource.
** [ ] Return 403 for a forbidden operation.
** [ ] Handle other filesystem errors.
**
** TODO 6: Tests
** [ ] Delete an existing file.
** [ ] Delete a nonexistent file.
** [ ] Attempt to delete a directory.
** [ ] Attempt path traversal.
** [ ] Attempt DELETE in a forbidden location.
** [ ] Verify that other files remain untouched.
**
** RULES:
** - C++98 only.
** - Preserve existing code where possible.
** - No recursive directory deletion.
** - Do not modify unrelated modules.
** ============================================================
*/

void Server::handle_delete(const HttpRequest &request, const ServerConfig &conf,
                           std::string &status, std::string &body)
{
    // Find the location responsible for this request.
    const LocationConfig *location = find_location(request.path, conf);

    // Check whether DELETE is allowed in this location.
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

    // Reject invalid or potentially unsafe request paths.
    if (request.path.empty() || request.path[0] != '/'
        || request.path.find("..") != std::string::npos)
    {
        status = "403 Forbidden";
        body = "403 Forbidden\n";
        return;
    }

    // Reuse the existing URI-to-filesystem mapping.
    const std::string file_path = build_file_path(request, location, conf);

    // Select the configured filesystem root.
    std::string root = conf.root;
    if (location != NULL && !location->root.empty())
        root = location->root;

    // Resolve paths to prevent deletion outside the configured root.
    char resolved_root[PATH_MAX];
    char resolved_file[PATH_MAX];

    if (root.empty() || realpath(root.c_str(), resolved_root) == NULL)
    {
        status = "500 Internal Server Error";
        body = "Invalid server root\n";
        return;
    }

    // Check the target without following a final symbolic link.
    struct stat info;
    if (lstat(file_path.c_str(), &info) == -1)
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

    // Never delete directories, symbolic links or special files.
    if (!S_ISREG(info.st_mode))
    {
        status = "403 Forbidden";
        body = "403 Forbidden\n";
        return;
    }

    if (realpath(file_path.c_str(), resolved_file) == NULL)
    {
        status = "403 Forbidden";
        body = "403 Forbidden\n";
        return;
    }

    const std::string root_path(resolved_root);
    const std::string target_path(resolved_file);

    // The target must be strictly inside the configured root.
    std::string prefix = root_path;
    if (prefix[prefix.size() - 1] != '/')
        prefix += '/';

    if (target_path.compare(0, prefix.size(), prefix) != 0)
    {
        status = "403 Forbidden";
        body = "403 Forbidden\n";
        return;
    }

    // Delete the file and translate filesystem errors to HTTP status.
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

    // Successful DELETE does not return a response body.
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


std::size_t Server::get_content_length(const std::string &headers_part) const
{
   std::string::size_type cl_pos = headers_part.find("Content-Length:");
    if (cl_pos == std::string::npos)
        cl_pos = headers_part.find("content-length:");

    if (cl_pos != std::string::npos)
    {
        std::string::size_type colon = headers_part.find(':', cl_pos);
        if (colon != std::string::npos)
        {
            std::string length_str = headers_part.substr(colon + 1);
            return static_cast<std::size_t>(std::atoi(length_str.c_str()));
        }
    }
    return 0;
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
    char buffer[4096];
    ssize_t bytes_read;

    do
    {
        bytes_read = recv(client.fd, buffer, sizeof(buffer), 0);
    }
    while (bytes_read < 0 && errno == EINTR);

    if (bytes_read == 0)
        return false; 
    if (bytes_read < 0)
    {
        std::cerr << "Failed to read HTTP request" << std::endl;
        return false;
    }
    client.last_activity = time(NULL);
    client.request_buffer.append(buffer, static_cast<std::string::size_type>(bytes_read));
    stop_processing = false;
    return true;
}


bool Server::handle_client_event(ClientConnection &client, HttpParser &http_parser)
{
    bool stop_processing = false;
    if (!read_client_data(client, stop_processing))
        return false;
    if (stop_processing)
        return true;

    std::string::size_type header_end = client.request_buffer.find("\r\n\r\n");
    if (header_end == std::string::npos)
        return true; 
    const ServerConfig &conf = get_server_config(client);

    std::string headers_part = client.request_buffer.substr(0, header_end);
    std::size_t content_length = get_content_length(headers_part);

    if (check_max_body_size(client, content_length, conf))
        return true;

    std::string::size_type body_start = header_end + 4;
    if (client.request_buffer.size() - body_start < content_length)
        return true;

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
        client.response_buffer.size() - client.sent_bytes, 0);
    if (bytes_sent < 0)
        return false ;
    if(bytes_sent == 0)
        return false;
    client.sent_bytes += static_cast<std::size_t>(bytes_sent);
    client.last_activity = time(NULL);
    if (client.sent_bytes < client.response_buffer.size())
        return true;
    std::cout << "Response fully sent to client FD: " << client.fd << std::endl;
    bool close_connection = (client.response_buffer.find("Connection: close") != std::string::npos);
    if (close_connection)
        return false;
    else
    {
        client.reset();
        return true;
    }
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
            throw std::runtime_error("Listening socket error");
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

        if ((events & POLLHUP) && !it->response_ready)
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