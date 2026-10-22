
#ifndef SERVER_CONFIG_HPP
#define SERVER_CONFIG_HPP

#include <string>
#include <vector>


struct LocationConfig
{
    std::string path;
    std::string root;
    std::string index;

    LocationConfig()
        : path("/"),
          root(""),
          index("index.html")
    {
    }
};

struct ServerConfig
{
    std::string ip;
    int port;
    std::string root;
    std::string index;
    std::vector<LocationConfig> locations;

    ServerConfig()
        : ip(""),
          port(0),
          root(""),
          index("index.html")
    {
    }
};

#endif
