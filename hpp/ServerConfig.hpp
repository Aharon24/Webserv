#ifndef SERVER_CONFIG_HPP
#define SERVER_CONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include <cstddef>

struct CgiConfig
{
    std::string extension;
    std::string interpreter;
};

struct ListenConfig
{
    std::string ip;
    int port;

    ListenConfig() : port(0)
    {
    }
};

struct LocationConfig
{
    std::string path;
    std::string root;
    std::string index;

    std::vector<std::string> allowed_methods;

    int return_code;
    std::string return_url;

    bool autoindex;

    bool upload_enabled;
    std::string upload_path;

    std::vector<CgiConfig> cgi;

    LocationConfig()
        : path("/"),
          root(""),
          index(""),
          return_code(0),
          autoindex(false),
          upload_enabled(false)
    {
    }
};

struct ServerConfig
{
    std::string ip;
    int port;
    std::string server_name;

    std::vector<ListenConfig> listens;

    std::string root;
    std::string index;

    std::size_t client_max_body_size;
    std::map<int, std::string> error_pages;

    std::vector<LocationConfig> locations;

    ServerConfig()
        : ip(""),
          port(0),
          server_name(""),
          root(""),
          index("index.html"),
          client_max_body_size(1024 * 1024)
    {
    }
};

#endif