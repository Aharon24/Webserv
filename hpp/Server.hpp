#ifndef SERVER_HPP
#define SERVER_HPP

#include "ServerConfig.hpp"
#include "HttpRequest.hpp"

#include <cerrno>

#include <cstring>

#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <cstddef>
#include <poll.h> 
#include <vector>


struct ClientConnection
{
    int fd;
    std::string request_buffer;
    std::string response_buffer;
    std::size_t sent_bytes;
    bool request_complete;
    bool response_ready;

    ClientConnection(): fd(-1),sent_bytes(0), request_complete(false), response_ready(false)
    {
    }

    explicit ClientConnection(int client_fd): fd(client_fd),sent_bytes(0),request_complete(false), response_ready(false)
    {
    }
};


class Server
{
private:
    std::vector<int> _server_fds;
    std::vector<ServerConfig> _configs;
    ServerConfig _conf;


    const LocationConfig *find_location(const std::string &path) const;
    std::string build_file_path(const HttpRequest &request,const LocationConfig *location) const;
    Server(const Server &other);
    Server &operator=(const Server &other);
    void set_nonblocking(int fd);
    std::string build_response(const HttpRequest &request);
    const ServerConfig *find_server_config(const std::string &host) const;


public:
    Server(const std::vector<ServerConfig> &configs);
    ~Server();

    // GET POST DELTETE
    void handle_get(const HttpRequest &request, std::string &status, std::string &body);
    void handle_post(const HttpRequest &request, std::string &status, std::string &body);
    void handle_delete(const HttpRequest &request, std::string &status, std::string &body); 
    std::string create_response_data(const HttpRequest &request, std::string &status, std::string &body);
    void remove_client(int client_fd, std::vector<ClientConnection> &clients);
    bool handle_client_event(ClientConnection &client, HttpParser &parser);
    bool handle_client_write(ClientConnection &client);
    //

    // Poll
    void run(HttpParser &parser);


    void create_socket();
    void bind_socket();
    void listen_socket();
    int accept_connection(int server_fd);
    void send_all(int client_fd, const std::string &data);

};

#endif