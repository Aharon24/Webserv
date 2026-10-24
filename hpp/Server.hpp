#ifndef SERVER_HPP
#define SERVER_HPP

#include "ServerConfig.hpp"
#include "HttpRequest.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>

#include <poll.h> 
#include <vector>

class Server
{
private:
    int _server_fd;
    ServerConfig _conf;
    const LocationConfig *find_location(const std::string &path) const;
    std::string build_file_path(const HttpRequest &request,const LocationConfig *location) const;
    Server(const Server &other);
    Server &operator=(const Server &other);


public:
    Server(const ServerConfig &conf);
    ~Server();

    // GET POST DELTETE
    void handle_get(const HttpRequest &request, std::string &status, std::string &body);
    void handle_post(const HttpRequest &request, std::string &status, std::string &body);
    void handle_delete(const HttpRequest &request, std::string &status, std::string &body); 
    std::string create_response_data(const HttpRequest &request, std::string &status, std::string &body);
    void remove_client(int client_fd, std::vector<int> &clients);
    //

    // Poll
    void run(HttpParser &parser);
    void handle_client_event(int client_fd, std::vector<int> &clients, HttpParser &parser);


    void create_socket();
    void bind_socket();
    void listen_socket();
    int accept_connection();
    std::string read_request(int client_fd);
    void send_response(int client_fd, const HttpRequest &request);
    void send_all(int client_fd, const std::string &data);

};

#endif