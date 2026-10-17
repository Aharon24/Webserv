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

void Server::read_request(int client_fd)
{
    char buffer[4096];

    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);

    if (bytes_read > 0)
    {
        buffer[bytes_read] = '\0';

        std::cout << "HTTP request received:\n" << buffer << std::endl;
    }
    else if (bytes_read == 0)
        std::cout << "Client disconnected" << std::endl;
    else
        throw std::runtime_error("Failed to read HTTP request");
}

Server::~Server()
{
    if (_server_fd != -1)
        close(_server_fd);
}