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

#include <csignal>
#include <ctime>
#include <cstdlib>

struct ClientConnection
{
    int fd;
    std::size_t listener_index;

    std::string request_buffer;
    std::string response_buffer;

    std::size_t sent_bytes;
    bool request_complete;
    bool response_ready;
    time_t last_activity;

    ClientConnection(): fd(-1), listener_index(0),sent_bytes(0), request_complete(false), response_ready(false)
    {
    }

    explicit ClientConnection(int client_fd, std::size_t index = 0)
        : fd(client_fd),
          listener_index(index),
          sent_bytes(0),
          request_complete(false),
          response_ready(false),
          last_activity(time(NULL))
    {
    }
    void reset()
    {
        request_buffer.clear();
        response_buffer.clear();
        sent_bytes = 0;
        request_complete = false;
        response_ready = false;
        last_activity = time(NULL);
    }
};


class Server
{
private:
    std::vector<int> _server_fds;
    std::vector<ServerConfig> _configs;

    std::vector<ListenConfig> _listens;
    std::vector<std::size_t> _listen_config_indices;


    const LocationConfig *find_location(const std::string &path, const ServerConfig &conf) const;
    std::string build_file_path(const HttpRequest &request,const LocationConfig *location,const ServerConfig &conf) const;
    Server(const Server &other);
    Server &operator=(const Server &other);
    void set_nonblocking(int fd);
    std::string build_response(const HttpRequest &request,const ServerConfig &conf);
    const ServerConfig *find_server_config(const std::string &host) const;


public:
    Server(const std::vector<ServerConfig> &configs);
    ~Server();

    // GET POST DELTETE
    void handle_get(const HttpRequest &request,const ServerConfig &conf,std::string &status,std::string &body);
    void handle_post(const HttpRequest &request, std::string &status, std::string &body);
    void handle_delete(const HttpRequest &request, const ServerConfig &conf, std::string &status, std::string &body);
    std::string create_response_data(const HttpRequest &request, std::string &status, std::string &body);
    void remove_client(int client_fd, std::vector<ClientConnection> &clients);
    bool handle_client_event(ClientConnection &client, HttpParser &parser);

    std::size_t get_content_length(const std::string &headers_part) const;
    bool check_max_body_size(ClientConnection &client, std::size_t content_length, const ServerConfig &conf);
    void parse_and_respond(ClientConnection &client, HttpParser &http_parser, const ServerConfig &conf);
    bool read_client_data(ClientConnection &client, bool &stop_processing);
    const ServerConfig &get_server_config(const ClientConnection &client) const;

    bool handle_client_write(ClientConnection &client);
    //



    // Poll
    void run(HttpParser &parser);
    std::vector<struct pollfd> build_poll_fds(const std::vector<ClientConnection> &clients) const;
    void accept_ready_connections(const std::vector<struct pollfd> &fds, std::vector<ClientConnection> &clients);
    int wait_for_events(std::vector<struct pollfd> &fds, int timeout_ms) const;
    void process_client_events(const std::vector<struct pollfd> &fds, std::vector<ClientConnection> &clients, HttpParser &parser);

    void collect_listens();

    void create_socket();
    void bind_socket();
    void listen_socket();
    int accept_connection(int server_fd);
    void send_all(int client_fd, const std::string &data);


    void check_timeouts(std::vector<ClientConnection> &clients, time_t timeout_seconds);
    


};

#endif