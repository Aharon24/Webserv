#ifndef SERVER_HPP
#define SERVER_HPP

#include "ServerConfig.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>

class Server
{
private:
    int _server_fd;
    ServerConfig _conf;

public:
    Server();
    Server(const ServerConfig &conf);
    Server(const Server &other);
    Server& operator=(const Server &other);
    ~Server();

    void create_socket();
    void bind_socket();
};

#endif