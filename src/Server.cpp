#include "Server.hpp"


Server::Server(const ServerConfig &conf)
    :  _conf(conf)
{
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
    for (std::size_t i = 0; i < _conf.listens.size(); ++i)
    {
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1)
            throw std::runtime_error("Failed to create socket");
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
        address.sin_port = htons(_conf.listens[i].port);
        if (inet_pton(AF_INET,_conf.listens[i].ip.c_str(), &address.sin_addr) != 1)
            throw std::runtime_error("Invalid IPv4 address");
        if (bind(_server_fds[i], reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) == -1)
            throw std::runtime_error("Failed to bind socket");
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

const LocationConfig *Server::find_location(const std::string &path) const
{
    const LocationConfig *best = NULL;

    for (std::size_t i = 0; i < _conf.locations.size(); ++i)
    {
        const std::string &prefix = _conf.locations[i].path;
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
            best = &_conf.locations[i];
    }
    return best;
}



void Server::handle_get(const HttpRequest &request, std::string &status, std::string &body)
{
    std::string file_path;
    const LocationConfig *location = find_location(request.path);
    file_path = build_file_path(request, location);
    struct stat info;
    if (stat(file_path.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
    {
        std::string index;
    
        if (location != NULL && !location->index.empty())
            index = location->index;
        else
            index = _conf.index;
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
void Server::handle_delete(const HttpRequest &request, std::string &status, std::string &body)
{
    std::cout << "DELETE method received!" << std::endl;
    std::cout << "Request path: " << request.path << std::endl;

    status = "501 Not Implemented";
    body = "DELETE is not implemented yet\n";
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

std::string Server::build_response(const HttpRequest &request)
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
        handle_get(request, status, body);
    else if (request.method == "POST")
        handle_post(request, status, body);
    else if (request.method == "DELETE")
        handle_delete(request, status, body);
    else
    {
        status = "405 Method Not Allowed";
        body = "405 Method Not Allowed\n";
    }

    return create_response_data(request, status, body) + body;
}



std::string Server::build_file_path(const HttpRequest &request,const LocationConfig *location) const
{
    std::string root;
    std::string relative_path;

    if (location != NULL)
    {
        if (location->root.empty())
            root = _conf.root;
        else
            root = location->root;
        relative_path = request.path.substr(location->path.size());
    }
    else
    {
        root = _conf.root;
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
            close(it->fd);
            clients.erase(it);
            return;
        }
    }
}



bool Server::handle_client_event(ClientConnection &client, HttpParser &http_parser)
{
    char buffer[4096];
    std::cout << "Reading from client: " << client.fd << std::endl;
    ssize_t bytes_read = read(client.fd, buffer, sizeof(buffer));

    if (bytes_read == 0)
        return false;

    if (bytes_read < 0)
    {
        std::cerr << "Failed to read HTTP request" << std::endl;
        return false;
    }

    client.request_buffer.append(buffer, static_cast<std::string::size_type>(bytes_read));

    std::cout << "HTTP request received:\n" << client.request_buffer << std::endl;

    if (client.request_buffer.find("\r\n\r\n") == std::string::npos)
        return true;

    try
    {
        HttpRequest request = http_parser.parse(client.request_buffer);

        client.response_buffer = build_response(request);
        client.request_complete = true;
        client.response_ready = true;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;

        HttpRequest request;
        request.version = "HTTP/1.1";

        std::string status = "400 Bad Request";
        std::string body = "400 Bad Request\n";

        client.response_buffer = create_response_data(request, status, body) + body;
        client.request_complete = true;
        client.response_ready = true;
    }

    return true;
}


bool Server::handle_client_write(ClientConnection &client)
{
    if (client.sent_bytes >= client.response_buffer.size())
        return false;

    ssize_t bytes_sent = send(client.fd, client.response_buffer.c_str() + client.sent_bytes,
        client.response_buffer.size() - client.sent_bytes,
        0
    );

    if (bytes_sent <= 0)
        return false;

    client.sent_bytes += static_cast<std::size_t>(bytes_sent);

    return client.sent_bytes < client.response_buffer.size();
}

void Server::run(HttpParser &parser)
{
    std::vector<ClientConnection> clients;

    while (true)
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

        int result = poll(&fds[0], fds.size(), -1);
        for (std::size_t i = 0; i < fds.size(); ++i)
        {
            if (fds[i].revents != 0)
            {
                std::cout << "fd: " << fds[i].fd
                          << " events: " << fds[i].revents
                          << std::endl;
            }
        }

        if (result == -1)
        {
            if (errno == EINTR)
                continue;
            throw std::runtime_error("poll failed");
        }
        std::cout << "poll returned: " << result << std::endl;
        for (std::size_t i = 0; i < _server_fds.size(); ++i)
        {
            if (fds[i].revents & POLLIN)
            {
                int client_fd = accept_connection(_server_fds[i]);
            
                if (client_fd != -1)
                {
                    std::cout << "Accepted client: "
                              << client_fd << std::endl;
                
                    clients.push_back(ClientConnection(client_fd));
                }
            }
        
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                throw std::runtime_error("Listening socket error");
        }
        
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

            if (events & (POLLNVAL | POLLHUP | POLLERR))
            {
                remove_client(client_fd, clients);
                continue;
            }

            bool keep_client = true;

            if (events & POLLIN)
                keep_client = handle_client_event(*it, parser);

            if (keep_client && (events & POLLOUT))
                keep_client = handle_client_write(*it);

            if (events & (POLLHUP | POLLERR))
                keep_client = false;

            if (!keep_client)
                remove_client(client_fd, clients);
        }
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