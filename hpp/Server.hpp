#ifndef SERVER_HPP
#define SERVER_HPP

#include "ServerConfig.hpp"
#include "HttpRequest.hpp"
#include "CgiHandler.hpp"

#include <cerrno>

#include <cstring>
#include <cctype>
#include <limits>

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
#include <cstdio>

#include <csignal>
#include <ctime>
#include <cstdlib>

struct ClientConnection
{
    int fd;
    std::size_t listener_index;

    std::string request_buffer;
    std::string response_buffer;

    std::string cgi_header_buffer;

    std::size_t sent_bytes;

    bool request_complete;
    bool response_ready;
    bool header_too_large;

    // CGI streaming state.
    bool cgi_streaming;
    bool cgi_headers_prepared;
    bool cgi_stream_finished;
    bool cgi_pending;
    bool cgi_body_allowed;
    bool cgi_input_streaming;
    std::size_t request_content_length;
    std::size_t request_body_received;

    time_t last_activity;

    ClientConnection()
        : fd(-1),
        listener_index(0),
        sent_bytes(0),
        request_complete(false),
        response_ready(false),
        header_too_large(false),
        cgi_streaming(false),
        cgi_headers_prepared(false),
        cgi_stream_finished(false),
        cgi_pending(false),
        cgi_body_allowed(false),
        cgi_input_streaming(false),
        request_content_length(0),
        request_body_received(0),
        last_activity(time(NULL))
    {
    }

    explicit ClientConnection(int client_fd, std::size_t index = 0)
        : fd(client_fd),
        listener_index(index),
        sent_bytes(0),
        request_complete(false),
        response_ready(false),
        header_too_large(false),
        cgi_streaming(false),
        cgi_headers_prepared(false),
        cgi_stream_finished(false),
        cgi_pending(false),
        cgi_body_allowed(false),
        cgi_input_streaming(false),
        request_content_length(0),
        request_body_received(0),
        last_activity(time(NULL))
    {
    }

    void reset()
    {
        request_buffer.clear();
        response_buffer.clear();
        cgi_header_buffer.clear();

        sent_bytes = 0;
        request_complete = false;
        response_ready = false;
        header_too_large = false;

        cgi_streaming = false;
        cgi_headers_prepared = false;
        cgi_stream_finished = false;
        cgi_pending = false;
        cgi_body_allowed = false;
        cgi_input_streaming = false;
        request_content_length = 0;
        request_body_received = 0;

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

    std::vector<CgiHandler *> _cgi_processes;


    const LocationConfig *find_location(const std::string &path, const ServerConfig &conf) const;
    std::string build_file_path(const HttpRequest &request,const LocationConfig *location,const ServerConfig &conf) const;
    Server(const Server &other);
    Server &operator=(const Server &other);
    void set_nonblocking(int fd);
    std::string build_response(ClientConnection &client,HttpRequest &request,const ServerConfig &conf);
    const ServerConfig *find_server_config(const std::string &host) const;

    // CGI
        // CGI
    bool start_cgi(ClientConnection &client,HttpRequest &request,const std::string &script_path,const std::string &interpreter_path,const std::string &path_info,std::string &error_status,bool streaming_input = false);

    CgiHandler *find_cgi_handler(int client_fd) const;

    bool pump_cgi_input(ClientConnection &client,CgiHandler &cgi);

    void process_cgi_events(const std::vector<struct pollfd> &fds,std::vector<ClientConnection> &clients,std::size_t polled_client_count,std::size_t polled_cgi_count);

    void remove_cgi_for_client(int client_fd);

    const CgiConfig *find_cgi_config(const std::string &uri,const LocationConfig *location) const;

public:
    public:
    Server(const std::vector<ServerConfig> &configs);
    ~Server();

    // POST / DELETE
    void handle_post(const HttpRequest &request,const ServerConfig &conf,std::string &status,std::string &body);

    void handle_delete(const HttpRequest &request,const ServerConfig &conf,std::string &status,std::string &body);

    std::string create_response_data(const HttpRequest &request,std::string &status,std::string &body);

    void remove_client(int client_fd,std::vector<ClientConnection> &clients);

    bool handle_client_event(ClientConnection &client,HttpParser &parser);

    bool get_content_length(const std::string &headers_part,std::size_t &content_length) const;

    bool check_max_body_size(ClientConnection &client,std::size_t content_length,std::size_t max_body_size);

    void parse_and_respond(ClientConnection &client,HttpParser &http_parser,const ServerConfig &conf);

    bool read_client_data(ClientConnection &client,bool &stop_processing);

    const ServerConfig &get_server_config(const ClientConnection &client) const;

    bool handle_client_write(ClientConnection &client);

    // Poll
    void run(HttpParser &parser);

    std::vector<struct pollfd> build_poll_fds(const std::vector<ClientConnection> &clients) const;

    void accept_ready_connections(const std::vector<struct pollfd> &fds,std::vector<ClientConnection> &clients);

    int wait_for_events(std::vector<struct pollfd> &fds,int timeout_ms) const;

    void process_client_events(const std::vector<struct pollfd> &fds,std::vector<ClientConnection> &clients,HttpParser &parser,std::size_t polled_client_count);

    void collect_listens();

    void create_socket();
    void bind_socket();
    void listen_socket();

    int accept_connection(int server_fd);

    void check_timeouts(std::vector<ClientConnection> &clients,time_t timeout_seconds);

    bool is_chunked(const std::string &headers_part) const;

    void handle_large_header(ClientConnection &client);

    std::size_t get_request_body_limit(const std::string &headers_part,const ServerConfig &conf) const;
};

#endif