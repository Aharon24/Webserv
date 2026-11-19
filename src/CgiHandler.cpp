
#include "CgiHandler.hpp"

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <cerrno>
#include <vector>
#include <sstream>
#include <cerrno>

namespace
{
    void close_fd(int &fd)
    {
        if (fd != -1)
        {
            close(fd);
            fd = -1;
        }
    }

    bool set_nonblocking(int fd)
    {
        int flags = fcntl(fd, F_GETFL, 0);

        if (flags == -1)
            return false;

        return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
    }
    bool set_cloexec(int fd)
    {
        int flags = fcntl(fd, F_GETFD, 0);

        if (flags == -1)
            return false;

        return fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != -1;
    }

    std::string number_to_string(std::size_t number)
    {
        std::ostringstream ss;
        ss << number;
        return ss.str();
    }

    std::vector<std::string> build_environment(
        const HttpRequest &request,
        const std::string &script_path,
        const std::string &path_info)
    {
        std::vector<std::string> env;

        env.push_back("GATEWAY_INTERFACE=CGI/1.1");
        env.push_back("SERVER_SOFTWARE=webserv/1.0");
        env.push_back("SERVER_PROTOCOL=" + request.version);
        env.push_back("REQUEST_METHOD=" + request.method);
        env.push_back("QUERY_STRING=" + request.query_string);
        env.push_back("SCRIPT_NAME=" + request.path);
        env.push_back("SCRIPT_FILENAME=" + script_path);
        env.push_back("PATH_INFO=" + path_info);
        env.push_back("CONTENT_LENGTH=" + number_to_string(request.body.size()));

        std::map<std::string, std::string>::const_iterator it;

        it = request.headers.find("content-type");

        if (it != request.headers.end())
            env.push_back("CONTENT_TYPE=" + it->second);
        else
            env.push_back("CONTENT_TYPE=");

        env.push_back(
            "REQUEST_URI=" + request.path +
            (request.query_string.empty() ? "" : "?" + request.query_string)
        );

        for (it = request.headers.begin(); it != request.headers.end(); ++it)
        {
            if (it->first == "content-type" ||
                it->first == "content-length")
                continue;

            std::string name = "HTTP_";

            for (std::size_t i = 0; i < it->first.size(); ++i)
            {
                char c = it->first[i];

                if (c == '-')
                    name += '_';
                else if (c >= 'a' && c <= 'z')
                    name += static_cast<char>(c - 'a' + 'A');
                else
                    name += c;
            }

            env.push_back(name + "=" + it->second);
        }

        return env;
    }
}

CgiHandler::CgiHandler(
    int client_fd,
    const HttpRequest &request,
    const std::string &script_path,
    const std::string &interpreter_path,
    const std::string &path_info)
    : _request(request),
      _script_path(script_path),
      _interpreter_path(interpreter_path),
      _path_info(path_info),
      _input_buffer(request.body),
      _output_buffer(""),
      _input_offset(0),
      _pid(-1),
      _client_fd(client_fd),
      _stdin_fd(-1),
      _stdout_fd(-1),
      _stdin_closed(false),
      _stdout_closed(false),
      _child_exited(false),
      _wait_status(0),
      _started_at(0)
{
}

const std::string &CgiHandler::get_http_version() const
{
    return _request.version;
}

bool CgiHandler::was_successful() const
{
    return _child_exited &&
           WIFEXITED(_wait_status) &&
           WEXITSTATUS(_wait_status) == 0;
}

CgiHandler::~CgiHandler()
{
    terminate();

    if (_pid > 0 && !_child_exited)
    {
        pid_t result;

        do
        {
            result = waitpid(_pid, &_wait_status, 0);
        }
        while (result == -1 && errno == EINTR);

        if (result == _pid ||
            (result == -1 && errno == ECHILD))
        {
            _pid = -1;
            _child_exited = true;
        }
    }
}

bool CgiHandler::start(std::string &error_status)
{
    error_status = "500 Internal Server Error";

    if (_pid != -1)
        return false;

    if (_script_path.empty() || _interpreter_path.empty() ||
        _script_path.find('\0') != std::string::npos ||
        _interpreter_path.find('\0') != std::string::npos)
        return false;

    struct stat info;

    if (stat(_script_path.c_str(), &info) == -1)
    {
        error_status = "404 Not Found";
        return false;
    }

    if (!S_ISREG(info.st_mode))
    {
        error_status = "405 Method Not Allowed";
        return false;
    }

    if (access(_script_path.c_str(), R_OK) == -1)
    {
        error_status = "405 Method Not Allowed";
        return false;
    }

    std::string::size_type slash = _script_path.find_last_of('/');

    std::string script_dir = ".";
    std::string script_name = _script_path;

    if (slash != std::string::npos)
    {
        if (slash == 0)
            script_dir = "/";
        else
            script_dir = _script_path.substr(0, slash);

        script_name = _script_path.substr(slash + 1);
    }

    if (script_name.empty())
        return false;

    std::vector<std::string> env =
        build_environment(_request, _script_path, _path_info);

    std::vector<char *> envp;

    for (std::size_t i = 0; i < env.size(); ++i)
        envp.push_back(const_cast<char *>(env[i].c_str()));

    envp.push_back(NULL);

    char *argv[3];

    argv[0] = const_cast<char *>(_interpreter_path.c_str());
    argv[1] = const_cast<char *>(script_name.c_str());
    argv[2] = NULL;

    int input_pipe[2] = {-1, -1};
    int output_pipe[2] = {-1, -1};

    if (pipe(input_pipe) == -1)
    {
        error_status = "500 Internal Server Error";
        return false;
    }

    if (pipe(output_pipe) == -1)
    {
        close_fd(input_pipe[0]);
        close_fd(input_pipe[1]);
        return false;
    }

    for (int i = 0; i < 2; ++i)
    {
        if (!set_cloexec(input_pipe[i]) ||
            !set_cloexec(output_pipe[i]))
        {
            close_fd(input_pipe[0]);
            close_fd(input_pipe[1]);
            close_fd(output_pipe[0]);
            close_fd(output_pipe[1]);
            return false;
        }
    }

    _pid = fork();

    if (_pid == -1)
    {
        close_fd(input_pipe[0]);
        close_fd(input_pipe[1]);
        close_fd(output_pipe[0]);
        close_fd(output_pipe[1]);
        return false;
    }

    if (_pid == 0)
    {
        close(input_pipe[1]);
        close(output_pipe[0]);

        if (dup2(input_pipe[0], STDIN_FILENO) == -1)
            _exit(127);

        if (dup2(output_pipe[1], STDOUT_FILENO) == -1)
            _exit(127);

        if (input_pipe[0] != STDIN_FILENO)
            close(input_pipe[0]);

        if (output_pipe[1] != STDOUT_FILENO)
            close(output_pipe[1]);

        if (_client_fd >= 0)
            close(_client_fd);

        signal(SIGPIPE, SIG_DFL);
        if (chdir(script_dir.c_str()) == -1)
            _exit(127);

        execve(_interpreter_path.c_str(), argv, &envp[0]);

        _exit(127);
    }

    close_fd(input_pipe[0]);
    close_fd(output_pipe[1]);

    _stdin_fd = input_pipe[1];
    _stdout_fd = output_pipe[0];

    if (!set_nonblocking(_stdin_fd) ||
        !set_nonblocking(_stdout_fd))
    {
        kill(_pid, SIGKILL);
        waitpid(_pid, &_wait_status, 0);

        _child_exited = true;
        _pid = -1;

        close_fd(_stdin_fd);
        close_fd(_stdout_fd);

        return false;
    }

    _started_at = time(NULL);

    if (_input_buffer.empty())
    {
        close_fd(_stdin_fd);
        _stdin_closed = true;
    }

    return true;
}

int CgiHandler::get_client_fd() const
{
    return _client_fd;
}

int CgiHandler::get_stdin_fd() const
{
    return _stdin_fd;
}

int CgiHandler::get_stdout_fd() const
{
    return _stdout_fd;
}

const std::string &CgiHandler::get_output() const
{
    return _output_buffer;
}

void CgiHandler::terminate()
{
    close_fd(_stdin_fd);
    _stdin_closed = true;

    close_fd(_stdout_fd);
    _stdout_closed = true;

    if (_pid > 0 && !_child_exited)
    {
        int result;

        do
        {
            result = kill(_pid, SIGKILL);
        }
        while (result == -1 && errno == EINTR);

        return;
    }

    _pid = -1;
    _child_exited = true;
}


bool CgiHandler::handle_stdin_write(std::string &error_status)
{
    if (_stdin_closed || _stdin_fd == -1)
        return true;

    if (_input_offset >= _input_buffer.size())
    {
        close_fd(_stdin_fd);
        _stdin_closed = true;
        return true;
    }

    std::size_t remaining = _input_buffer.size() - _input_offset;
    std::size_t chunk_size = remaining;

    if (chunk_size > 4096)
        chunk_size = 4096;

    ssize_t bytes_written = write(
        _stdin_fd,
        _input_buffer.data() + _input_offset,
        chunk_size
    );

    if (bytes_written <= 0)
    {
        error_status = "502 Bad Gateway";
        close_fd(_stdin_fd);
        _stdin_closed = true;
        return false;
    }

    _input_offset += static_cast<std::size_t>(bytes_written);

    if (_input_offset >= _input_buffer.size())
    {
        close_fd(_stdin_fd);
        _stdin_closed = true;
    }

    return true;
}

bool CgiHandler::handle_stdout_read(std::string &error_status)
{
    if (_stdout_closed || _stdout_fd == -1)
        return true;

    char buffer[4096];

    ssize_t bytes_read = read(_stdout_fd,buffer,sizeof(buffer));

    if (bytes_read < 0)
    {
        error_status = "502 Bad Gateway";
        close_fd(_stdout_fd);
        _stdout_closed = true;
        return false;
    }

    if (bytes_read == 0)
    {
        close_fd(_stdout_fd);
        _stdout_closed = true;
        return true;
    }

    _output_buffer.append(
        buffer,
        static_cast<std::size_t>(bytes_read)
    );

    return true;
}

void CgiHandler::close_stdin()
{
    close_fd(_stdin_fd);
    _stdin_closed = true;
}

void CgiHandler::close_stdout()
{
    close_fd(_stdout_fd);
    _stdout_closed = true;
}
bool CgiHandler::check_child(std::string &error_status)
{
    if (_child_exited)
        return true;

    if (_pid <= 0)
    {
        error_status = "500 Internal Server Error";
        return false;
    }

    pid_t result;

    do
    {
        result = waitpid(_pid, &_wait_status, WNOHANG);
    }
    while (result == -1 && errno == EINTR);

    if (result == 0)
        return true;

    if (result == _pid)
    {
        _child_exited = true;
        _pid = -1;

        close_fd(_stdin_fd);
        _stdin_closed = true;

        return true;
    }

    error_status = "500 Internal Server Error";
    return false;
}

bool CgiHandler::is_finished() const
{
    return _child_exited &&
           _stdin_closed &&
           _stdout_closed;
}


