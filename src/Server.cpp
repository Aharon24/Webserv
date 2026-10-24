#include "Server.hpp"


Server::Server(const ServerConfig &conf)
    : _server_fd(-1), _conf(conf)
{
}

void Server::create_socket()
{
    _server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (_server_fd == -1)
        throw std::runtime_error("Failed to create socket");
}


void Server::listen_socket()
{
    if (listen(_server_fd, 128) == -1)
        throw std::runtime_error("Failed to listen on socket");
}

void Server::bind_socket()
{
    struct sockaddr_in address;

    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(_conf.port);
    if (inet_pton(AF_INET, _conf.ip.c_str(), &address.sin_addr) != 1)
        throw std::runtime_error("Invalid IPv4 address");
    if (bind(_server_fd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) == -1)
    {
        throw std::runtime_error("Failed to bind socket");
    }
}

int Server::accept_connection()
{
    int client_fd = accept(_server_fd, NULL, NULL);

    if (client_fd == -1)
        throw std::runtime_error("Failed to accept connection");
    return client_fd;
}

std::string Server::read_request(int client_fd)
{
    char buffer[4096];

    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);

    if (bytes_read > 0)
    {
        buffer[bytes_read] = '\0';
        std::cout << "HTTP request received:\n" << buffer << std::endl;
        return std::string(buffer, bytes_read);
    }
    else if (bytes_read == 0)
        throw std::runtime_error("Client disconnected");
    else
        throw std::runtime_error("Failed to read HTTP request");
    return "";
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

void Server::send_response(int client_fd, const HttpRequest &request)
{
    std::string body;
    std::string response;
    std::string status;

    if (request.path.empty() || request.path[0] != '/' || request.path.find("..") != std::string::npos)
    {
        status = "403 Forbidden";
        body = "403 Forbidden\n";
    }
    else if(request.method == "GET")
        handle_get(request, status, body);
    else if(request.method == "POST")
        handle_post(request, status, body);
    else if(request.method == "DELETE")
        handle_delete(request, status, body);
    else
    {
        status = "405 Method Not Allowed";
        body = "405 Method Not Allowed\n";
    }
    response = create_response_data(request, status, body) + body;
    send_all(client_fd, response);
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


void Server::remove_client(int client_fd, std::vector<int> &clients)
{
    close(client_fd);
    for (std::vector<int>::iterator it = clients.begin(); it != clients.end(); ++it)
    {
        if (*it == client_fd)
        {
            clients.erase(it);
            break;
        }
    }
}

void Server::handle_client_event(int client_fd, std::vector<int> &clients, HttpParser &parser)
{
    try
    {
        std::string raw_request = read_request(client_fd);
        HttpRequest request = parser.parse(raw_request);
        send_response(client_fd, request);
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() <<  std::endl;
    }
    remove_client(client_fd, clients);
}

void Server::run(HttpParser &parser)
{
    std::vector<int> clients;

    while(true)
    {
        std::vector<struct pollfd> fds;
        struct pollfd server_poll;

        server_poll.fd = _server_fd;
        server_poll.events = POLLIN;
        server_poll.revents = 0;
        fds.push_back(server_poll);
        for (std::size_t i = 0; i < clients.size(); ++i)
        {
            struct pollfd client_poll;
            client_poll.fd = clients[i];
            client_poll.events = POLLIN;
            client_poll.revents = 0;

            fds.push_back(client_poll);
        }
        std::cout << "Waiting for events..." << std::endl;
        int result = poll(&fds[0], fds.size(), -1);
        std::cout << "poll returned: " << result << std::endl;
        if (result == -1)
            throw std::runtime_error("poll failed");
        for (std::size_t i = 0; i < fds.size(); ++i)
        {
            if (fds[i].revents != 0)
            {
                std::cout << "FD: " << fds[i].fd
                          << " Events: " << fds[i].revents
                          << std::endl;
            }
        }
        if (fds[0].revents & POLLIN)
        {
            int client_fd = accept_connection();
            clients.push_back(client_fd);
        }
        for (std::size_t i = 1; i < fds.size(); ++i)
        {
            int client_fd = fds[i].fd;
            short events = fds[i].revents;
            if (fds[i].revents & POLLIN)
                handle_client_event(client_fd, clients, parser);
            else if (events &  POLLNVAL)
                remove_client(client_fd, clients);
            else if (events & (POLLHUP | POLLERR))
                remove_client(client_fd, clients);
        }
    }
}

Server::~Server()
{
    if (_server_fd != -1)
        close(_server_fd);
}