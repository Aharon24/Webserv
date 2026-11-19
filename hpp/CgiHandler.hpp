
#ifndef CGI_HANDLER_HPP
#define CGI_HANDLER_HPP

#include <string>
#include <cstddef>
#include <ctime>
#include <sys/types.h>
#include "HttpRequest.hpp"

class CgiHandler
{
private:
    HttpRequest _request;

    std::string _script_path;
    std::string _interpreter_path;
    std::string _path_info;

    std::string _input_buffer;
    std::string _output_buffer;

    std::size_t _input_offset;

    pid_t _pid;

    int _client_fd;
    int _stdin_fd;
    int _stdout_fd;

    bool _stdin_closed;
    bool _stdout_closed;
    bool _child_exited;

    int _wait_status;
    time_t _started_at;

    CgiHandler(const CgiHandler &other);
    CgiHandler &operator=(const CgiHandler &other);

public:
    CgiHandler(
        int client_fd,
        const HttpRequest &request,
        const std::string &script_path,
        const std::string &interpreter_path,
        const std::string &path_info
    );

    ~CgiHandler();

    bool start(std::string &error_status);

    const std::string &get_http_version() const;
    bool was_successful() const;


    void close_stdin();
    void close_stdout();
    int get_client_fd() const;
    int get_stdin_fd() const;
    int get_stdout_fd() const;

    bool handle_stdin_write(std::string &error_status);
    bool handle_stdout_read(std::string &error_status);

    bool check_child(std::string &error_status);

    bool is_finished() const;

    const std::string &get_output() const;

    void terminate();
};

#endif
