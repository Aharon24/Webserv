#include "Server.hpp"

Server::Server()
{

}

Server::Server(const ServerConfig &conf)
    : _server_fd(-1), _conf(conf)
{
}


Server::~Server()
{
    
}