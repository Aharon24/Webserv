
#ifndef CGI_HANDLER_HPP
#define CGI_HANDLER_HPP

#include <string>
#include <cstddef>
#include <ctime>
#include <sys/types.h>
#include "HttpRequest.hpp"
#include <limits.h>
#include <vector>

class CgiHandler
{
private:
    std::string _http_version;
    std::vector<std::string> _environment;

    std::string _script_path;
    std::string _interpreter_path;
    std::string _path_info;

    std::string _input_buffer;
    std::string _output_buffer;

    std::size_t _input_offset;
    bool _input_complete;

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
    CgiHandler(int client_fd,HttpRequest &request,const std::string &script_path,const std::string &interpreter_path, const std::string &path_info);

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
    bool read_stdout_chunk(std::string &chunk,std::string &error_status);



    void enable_streaming_input();
    bool append_stdin_data(const char *data, std::size_t size);
    void finish_stdin_input();
    std::size_t pending_stdin_bytes() const;
    std::size_t stdin_buffer_space() const;

    bool check_child(std::string &error_status);

    bool is_finished() const;

    std::string &get_output();
    const std::string &get_output() const;

    void terminate();
};

#endif
