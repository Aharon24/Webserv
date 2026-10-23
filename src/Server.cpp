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


void Server::send_response(int client_fd, const HttpRequest &request)
{
    std::string file_path;
    std::string body;
    std::string response;
    std::string status;

    if(request.method != "GET")
    {
        body = "405 Method Not Allowed\n";
        status = "405 Method Not Allowed";
    }
    else if (request.path.find("..") != std::string::npos)
    {
        body = "403 Forbidden\n";
        status = "403 Forbidden";
    }
    else
    {
        const LocationConfig *location = find_location(request.path);
        file_path = build_file_path(request, location);
        struct stat info;

        if (stat(file_path.c_str(), &info) == 0
            && S_ISDIR(info.st_mode))
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
    std::ostringstream header;
    header << request.version << " " << status << "\r\n";
    header << "Content-Length: " << body.size() << "\r\n";
    header << "Content-Type: text/html; charset=UTF-8\r\n";
    header << "Connection: close\r\n";
    header << "\r\n";

    response = header.str() + body;
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


Server::~Server()
{
    if (_server_fd != -1)
        close(_server_fd);
}