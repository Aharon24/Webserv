#ifndef SERVER_HPP
#define SERVER_HPP

#include "ServerConfig.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <iostream>

class Server
{
private:
    int _server_fd;
    ServerConfig _conf;
    Server(const Server &other);
    Server &operator=(const Server &other);

public:
    Server(const ServerConfig &conf);
    ~Server();

    void create_socket();
    void bind_socket();
    void listen_socket();
    int accept_connection();
    void read_request(int client_fd);
};

#endif