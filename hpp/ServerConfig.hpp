
#ifndef SERVER_CONFIG_HPP
#define SERVER_CONFIG_HPP

#include <string>

struct ServerConfig
{
    std::string ip;
    int port;
    std::string root;
    std::string index;

    ServerConfig()
        : ip(""),
          port(0),
          root(""),
          index("index.html")
    {
    }
};

#endif
