#include "Server.hpp"


Server::Server(const std::vector<ServerConfig> &configs) : _configs(configs)
{
    std::signal(SIGPIPE, SIG_IGN);

    if (_configs.empty())
        throw std::runtime_error("No server configurations");

    collect_listens();
}

void Server::set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1)
        throw std::runtime_error("Failed to get socket flags");

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        throw std::runtime_error("Failed to set non-blocking mode");
}

void Server::create_socket()
{
    for (std::size_t i = 0; i < _listens.size(); ++i)
    {
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1)
            throw std::runtime_error("Failed to create socket");
        int reuse = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1)
        {
            close(server_fd);
            throw std::runtime_error("Failed to set SO_REUSEADDR");
        }
        try
        {
            set_nonblocking(server_fd);
        }
        catch (...)
        {
            close(server_fd);
            server_fd = -1;
            throw;
        }
        _server_fds.push_back(server_fd);
        std::cout << "Created socket FD: " << server_fd << " for port: " << _listens[i].port << std::endl;
    }

}


void Server::listen_socket()
{
    for (std::size_t i = 0; i < _server_fds.size(); ++i)
    {
        if (listen(_server_fds[i], 128) == -1)
            throw std::runtime_error("Failed to listen on socket");
    }
}

void Server::bind_socket()
{
    for (std::size_t i = 0; i < _server_fds.size(); ++i)
    {
        struct sockaddr_in address;

        std::memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons(_listens[i].port);
        if (inet_pton(AF_INET,_listens[i].ip.c_str(), &address.sin_addr) != 1)
            throw std::runtime_error("Invalid IPv4 address");
        if (bind(_server_fds[i], reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) == -1)
            throw std::runtime_error("Failed to bind socket");
        std::cout << "Bind successful: " << _listens[i].ip << ":" << _listens[i].port << std::endl;
    }
}

int Server::accept_connection(int server_fd)
{
    int client_fd = accept(server_fd, NULL, NULL);

    if (client_fd == -1)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return -1;
        throw std::runtime_error("Failed to accept connection");
    }
    try
    {
        set_nonblocking(client_fd);
    }
    catch (...)
    {
        close(client_fd);
        throw;
    }
    return client_fd;
}

const LocationConfig* Server::find_location(const std::string &path, const ServerConfig &conf) const
{
    const LocationConfig *best = NULL;

    for (std::size_t i = 0; i < conf.locations.size(); ++i)
    {
        const std::string &prefix = conf.locations[i].path;
        bool matches = false;

        if (prefix.empty())
            continue;

        if (path.size() >= prefix.size() && path.compare(0, prefix.size(), prefix) == 0)
        {
            if (path.size() == prefix.size())
                matches = true;
            else if (prefix == "/")
                matches = true;
            else if (path[prefix.size()] == '/')
                matches = true;
        }
        if (matches &&(best == NULL || prefix.size() > best->path.size()))
            best = &conf.locations[i];
    }
    return best;
}



void Server::collect_listens()
{
    _listens.clear();
    _listen_config_indices.clear();


    for(std::size_t i = 0; i < _configs.size(); i++)
    {
        for(std::size_t j = 0; j < _configs[i].listens.size(); j++)
        {
            const ListenConfig &current = _configs[i].listens[j];
            for(std::size_t l = 0; l < _listens.size(); l++)
            {
                if(current.ip == _listens[l].ip && current.port == _listens[l].port)
                    throw std::runtime_error("Duplicate listen address");
            }
            _listens.push_back(current);
            _listen_config_indices.push_back(i);
        }
    }
    std::cout << "\n========== COLLECTED LISTENS ==========" << std::endl;
    for (std::size_t i = 0; i < _listens.size(); ++i)
    {
        std::cout << "Listen index: " << i << std::endl;
        std::cout << "IP: " << _listens[i].ip << std::endl;
        std::cout << "Port: " << _listens[i].port << std::endl;
        std::cout << "_listen_config_indices[" << i << "] = "
              << _listen_config_indices[i] << std::endl;
        std::cout << "-----------------------------" << std::endl;
    }
}

void Server::handle_get(const HttpRequest &request,const ServerConfig &conf,std::string &status,std::string &body)
{
    std::string file_path;
    const LocationConfig *location = find_location(request.path, conf);
    file_path = build_file_path(request, location, conf);
    struct stat info;
    if (stat(file_path.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
    {
        std::string index;
    
        if (location != NULL && !location->index.empty())
            index = location->index;
        else
            index = conf.index;
        if (!file_path.empty() && file_path[file_path.size() - 1] != '/')
            file_path += '/';
        file_path += index;
    }
    std::ifstream file(file_path.c_str(), std::ios::in | std::ios::binary);
    if(!file)
    {
        status = "404 Not Found";
        body = "404 Not Found\n";
    }
    else
    {
        std::ostringstream content;
        content << file.rdbuf();
        body = content.str();
        status = "200 OK";
    }
}
void Server::handle_post(const HttpRequest &request, std::string &status, std::string &body)
{
        
    std::cout << "POST method received!" << std::endl;
    std::cout << "Request path: " << request.path << std::endl;

    status = "501 Not Implemented";
    body = "POST is not implemented yet\n";
}

/*
** ============================================================
** HTTP DELETE - TODO
** 42 Webserv | Programmer 4
** ============================================================
**
** TODO 1: Configuration
** [ ] Receive the current ServerConfig.
** [ ] Find the matching LocationConfig.
** [ ] Check whether DELETE is allowed.
**
** TODO 2: Resolve target
** [ ] Use the existing build_file_path().
** [ ] Prevent path traversal.
** [ ] Prevent deletion outside the allowed root.
**
** TODO 3: Validate target
** [ ] Check whether the target exists.
** [ ] Reject directories.
** [ ] Handle inaccessible targets.
**
** TODO 4: Delete file
** [ ] Call unlink().
** [ ] Check the return value.
** [ ] Handle errno correctly.
**
** TODO 5: HTTP response
** [ ] Return 204 No Content on success.
** [ ] Return 404 for a missing resource.
** [ ] Return 403 for a forbidden operation.
** [ ] Handle other filesystem errors.
**
** TODO 6: Tests
** [ ] Delete an existing file.
** [ ] Delete a nonexistent file.
** [ ] Attempt to delete a directory.
** [ ] Attempt path traversal.
** [ ] Attempt DELETE in a forbidden location.
** [ ] Verify that other files remain untouched.
**
** RULES:
** - C++98 only.
** - Preserve existing code where possible.
** - No recursive directory deletion.
** - Do not modify unrelated modules.
** ============================================================
*/
/*Webserv
This is when you finally understand why URLs start
with HTTP
Summary:
This project is about writing your own HTTP server.
You will be able to test it with an actual browser.
HTTP is one of the most widely used protocols on the internet.
Understanding its intricacies will be useful, even if you won’t be working on a website.
Version: 24.1
Contents
I Introduction 2
II General rules 3
III AI Instructions 4
IV Mandatory part 6
IV.1 Requirements . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . 8
IV.2 For MacOS only . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . 10
IV.3 Configuration file . . . . . . . . . . . . . . . . . . . . . . . . . . . . . 10
V Readme Requirements 12
VI Bonus part 13
VII Submission and peer-evaluation 14
1
Chapter I
Introduction
The Hypertext Transfer Protocol (HTTP) is an application protocol for distributed,
collaborative, hypermedia information systems.
HTTP is the foundation of data communication for the World Wide Web, where hypertext documents include hyperlinks to other resources that the user can easily access. For
example, by clicking a mouse button or tapping the screen on a web browser.
HTTP was developed to support hypertext functionality and the growth of the World
Wide Web.
The primary function of a web server is to store, process, and deliver web pages to clients.
Client-server communication occurs through the Hypertext Transfer Protocol (HTTP).
Pages delivered are most frequently HTML documents, which may include images, style
sheets, and scripts in addition to the text content.
Multiple web servers may be used for a high-traffic website, splitting traffic between multiple physical machines.
A user agent, commonly a web browser or web crawler, initiates communication by requesting a specific resource using HTTP, and the server responds with the content of that
resource or an error message if unable to do so. The resource is typically a real file on
the server’s storage, or the result of a program. But this is not always the case and can
actually be many other things.
Although its primary function is to serve content, HTTP also enables clients to send
data. This feature is used for submitting web forms, including the uploading of files.
2
Chapter II
General rules
• Your program must not crash under any circumstances (even if it runs out of memory) or terminate unexpectedly.
If this occurs, your project will be considered non-functional and your grade will
be 0.
• You must submit a Makefile that compiles your source files. It must not perform
unnecessary relinking.
• Your Makefile must at least contain the rules:
$(NAME), all, clean, fclean and re.
• Compile your code with c++ and the flags -Wall -Wextra -Werror
• Your code must comply with the C++ 98 standard and should still compile when
adding the flag -std=c++98.
• Make sure to leverage as many C++ features as possible (e.g., choose <cstring>
over <string.h>). You are allowed to use C functions, but always prefer their C++
versions if possible.
• Any external library and Boost libraries are forbidden.
3
Chapter III
AI Instructions
● Context
During your learning journey, AI can assist with many different tasks. Take the time to
explore the various capabilities of AI tools and how they can support your work. However, always approach them with caution and critically assess the results. Whether it’s
code, documentation, ideas, or technical explanations, you can never be completely sure
that your question was well-formed or that the generated content is accurate. Your peers
are a valuable resource to help you avoid mistakes and blind spots.
● Main message
☛ Use AI to reduce repetitive or tedious tasks.
☛ Develop prompting skills — both coding and non-coding — that will benefit your
future career.
☛ Learn how AI systems work to better anticipate and avoid common risks, biases,
and ethical issues.
☛ Continue building both technical and power skills by working with your peers.
☛ Only use AI-generated content that you fully understand and can take responsibility
for.
● Learner rules:
• You should take the time to explore AI tools and understand how they work, so
you can use them ethically and reduce potential biases.
• You should reflect on your problem before prompting — this helps you write clearer,
more detailed, and more relevant prompts using accurate vocabulary.
• You should develop the habit of systematically checking, reviewing, questioning,
and testing anything generated by AI.
• You should always seek peer review — don’t rely solely on your own validation.
4
Webserv This is when you finally understand why URLs start with HTTP
● Phase outcomes:
• Develop both general-purpose and domain-specific prompting skills.
• Boost your productivity with effective use of AI tools.
• Continue strengthening computational thinking, problem-solving, adaptability, and
collaboration.
● Comments and examples:
• You’ll regularly encounter situations — exams, evaluations, and more — where
you must demonstrate real understanding. Be prepared, keep building both your
technical and interpersonal skills.
• Explaining your reasoning and debating with peers often reveals gaps in your understanding. Make peer learning a priority.
• AI tools often lack your specific context and tend to provide generic responses. Your
peers, who share your environment, can offer more relevant and accurate insights.
• Where AI tends to generate the most likely answer, your peers can provide alternative perspectives and valuable nuance. Rely on them as a quality checkpoint.
✓ Good practice:
I ask AI: “How do I test a sorting function?” It gives me a few ideas. I try them out
and review the results with a peer. We refine the approach together.
✗ Bad practice:
I ask AI to write a whole function, copy-paste it into my project. During peerevaluation, I can’t explain what it does or why. I lose credibility — and I fail my
project.
✓ Good practice:
I use AI to help design a parser. Then I walk through the logic with a peer. We catch
two bugs and rewrite it together — better, cleaner, and fully understood.
✗ Bad practice:
I let Copilot generate my code for a key part of my project. It compiles, but I can’t
explain how it handles pipes. During the evaluation, I fail to justify and I fail my
project.
5
Chapter IV
Mandatory part
Program Name webserv
Files to Submit Makefile, *.{h, hpp}, *.cpp, *.tpp, *.ipp,
configuration files
Makefile NAME, all, clean, fclean, re
Arguments [A configuration file]
External Function All functionality must be implemented in C++ 98.
execve, pipe, strerror, gai_strerror, errno, dup,
dup2, fork, socketpair, htons, htonl, ntohs, ntohl,
select, poll, epoll (epoll_create, epoll_ctl,
epoll_wait), kqueue (kqueue, kevent), socket,
accept, listen, send, recv, shutdown, chdir, bind,
connect, getaddrinfo, freeaddrinfo, setsockopt,
getsockname, getprotobyname, fcntl, close, read,
write, waitpid, kill, signal, access, stat, open,
opendir, readdir and closedir.
Libft authorized n/a
Description An HTTP server in C++ 98
You must write an HTTP server in C++ 98.
Your executable should be executed as follows:
./webserv [configuration file]
Even though poll() is mentioned in the subject and evaluation sheet,
you can use any equivalent function such as select(), kqueue(), or
epoll().
6
Webserv This is when you finally understand why URLs start with HTTP
Please read the RFCs defining the HTTP protocol, and perform tests
with telnet and NGINX before starting this project.
Although you are not required to implement the entire RFCs, reading
it will help you develop the required features.
The HTTP 1.0 is suggested as a reference point, but not enforced.
7
Webserv This is when you finally understand why URLs start with HTTP
IV.1 Requirements
• Your program must use a configuration file, provided as an argument on the command line, or available in a default path.
• You cannot execve another web server.
• Your server must remain non-blocking at all times and properly handle client disconnections when necessary.
• It must be non-blocking and use only 1 poll() (or equivalent) for all the I/O
operations between the clients and the server (listen included).
• poll() (or equivalent) must monitor both reading and writing simultaneously.
• You must never do a read or a write operation without going through poll() (or
equivalent).
• Checking the value of errno to adjust the server behaviour is strictly forbidden
after performing a read or write operation.
• You are not required to use poll() (or an equivalent function) for regular disk files;
read() and write() on them do not require readiness notifications.
I/O that can wait for data (sockets, pipes/FIFOs, etc.) must be
non-blocking and driven by a single poll() (or equivalent). Calling
read/recv or write/send on these descriptors without prior readiness
will result in a grade of 0. Regular disk files are exempt.
• When using poll() or any equivalent call, you can use every associated macro or
helper function (e.g., FD_SET for select()).
• A request to your server should never hang indefinitely.
• Your server must be compatible with standard web browsers of your choice.
• NGINX may be used to compare headers and answer behaviours (pay attention to
differences between HTTP versions).
• Your HTTP response status codes must be accurate.
• Your server must have default error pages if none are provided.
• You can’t use fork for anything other than CGI (like PHP, or Python, and so forth).
• You must be able to serve a fully static website.
• Clients must be able to upload files.
• You need at least the GET, POST, and DELETE methods.
8
Webserv This is when you finally understand why URLs start with HTTP
• Stress test your server to ensure it remains available at all times.
• Your server must be able to listen to multiple ports to deliver different content (see
Configuration file).
We deliberately chose to offer only a subset of the HTTP RFC. In this
context, the virtual host feature is considered out of scope. But
you are allowed to implement it if you want.
9
Webserv This is when you finally understand why URLs start with HTTP
IV.2 For MacOS only
Since macOS handles write() differently from other Unix-based OSes,
you are allowed to use fcntl().
You must use file descriptors in non-blocking mode to achieve
behaviour similar to that of other Unix OSes.
However, you are allowed to use fcntl() only with the following
flags:
F_SETFL, O_NONBLOCK and, FD_CLOEXEC.
Any other flag is forbidden.
IV.3 Configuration file
You can take inspiration from the ’server’ section of the NGINX
configuration file.
In the configuration file, you should be able to:
• Define all the interface:port pairs on which your server will listen to (defining multiple websites served by your program).
• Set up default error pages.
• Set the maximum allowed size for client request bodies.
• Specify rules or configurations on a URL/route (no regex required here), for a
website, among the following:
◦ List of accepted HTTP methods for the route.
◦ HTTP redirection.
◦ Directory where the requested file should be located (e.g., if URL /kapouet
is rooted to /tmp/www, URL /kapouet/pouic/toto/pouet will search for
/tmp/www/pouic/toto/pouet).
◦ Enabling or disabling directory listing.
◦ Default file to serve when the requested resource is a directory.
◦ Uploading files from the clients to the server is authorized, and storage location
is provided.
10
Webserv This is when you finally understand why URLs start with HTTP
◦ Execution of CGI, based on file extension (for example .php). Here are some
specific remarks regarding CGIs:
∗ Do you wonder what a CGI is?
∗ Have a careful look at the environment variables involved in the web
server-CGI communication. The full request and arguments provided by
the client must be available to the CGI.
∗ Just remember that, for chunked requests, your server needs to un-chunk
them, the CGI will expect EOF as the end of the body.
∗ The same applies to the output of the CGI. If no content_length is
returned from the CGI, EOF will mark the end of the returned data.
∗ The CGI should be run in the correct directory for relative path file access.
∗ Your server should support at least one CGI (php-CGI, Python, and so
forth).
You must provide configuration files and default files to test and demonstrate that
every feature works during the evaluation.
You can have other rules or configuration information in your file (e.g., a server name
for a website if you plan to implement virtual hosts).
If you have a question about a specific behaviour, you can compare
your program’s behaviour with NGINX’s.
We have provided a small tester. Using it is not mandatory if
everything works fine with your browser and tests, but it can help
you find and fix bugs.
Resilience is key. Your server must remain operational at all times.
Do not test with only one program. Write your tests in a more
suitable language, such as Python or Golang, among others, even
in C or C++ if you prefer.
11
Chapter V
Readme Requirements
A README.md file must be provided at the root of your Git repository. Its purpose is
to allow anyone unfamiliar with the project (peers, staff, recruiters, etc.) to quickly
understand what the project is about, how to run it, and where to find more information
on the topic.
The README.md must include at least:
• The very first line must be italicized and read: This project has been created as part
of the 42 curriculum by <login1>[, <login2>[, <login3>[...]]].
• A “Description” section that clearly presents the project, including its goal and a
brief overview.
• An “Instructions” section containing any relevant information about compilation,
installation, and/or execution.
• A “Resources” section listing classic references related to the topic (documentation, articles, tutorials, etc.), as well as a description of how AI was used —
specifying for which tasks and which parts of the project.
➠ Additional sections may be required depending on the project (e.g., usage
examples, feature list, technical choices, etc.).
Any required additions will be explicitly listed below.
Your README must be written in English.
12
Chapter VI
Bonus part
Here are some additional features you can implement:
• Support cookies and session management (provide simple examples).
• Handle multiple CGI types.
The bonus part will only be assessed if the mandatory part is fully
completed without issues. If you fail to meet all the mandatory
requirements, your bonus part will not be evaluated.
13
Chapter VII
Submission and peer-evaluation
Submit your assignment in your Git repository as usual. Only the content of your repository will be evaluated during the defense. Be sure to double-check the names of your
v
*/
void Server::handle_delete(const HttpRequest &request, const ServerConfig &conf,std::string &status, std::string &body)
{
    const LocationConfig *location = find_location(request.path, conf);

    if (location != NULL && !location->allowed_methods.empty())
    {
        bool allowed = false;

        for (std::size_t i = 0; i < location->allowed_methods.size(); ++i)
        {
            if (location->allowed_methods[i] == "DELETE")
                allowed = true;
        }
        if (!allowed)
        {
            status = "405 Method Not Allowed";
            body = "405 Method Not Allowed\n";
            return;
        }
    }

    if (request.path.empty() || request.path[0] != '/'
        || request.path.find("..") != std::string::npos)
    {
        status = "403 Forbidden";
        body = "403 Forbidden\n";
        return;
    }

    std::string root = conf.root;
    if (location != NULL && !location->root.empty())
        root = location->root;

    if (root.empty())
    {
        status = "500 Internal Server Error";
        body = "Invalid server root\n";
        return;
    }

    const std::string file_path = build_file_path(request, location, conf);

    std::string normalized_root = root;
    if (normalized_root[normalized_root.size() - 1] != '/')
        normalized_root += '/';

    if (file_path.compare(0, normalized_root.size(), normalized_root) != 0)
    {
        status = "403 Forbidden";
        body = "403 Forbidden\n";
        return;
    }

    struct stat info;
    if (stat(file_path.c_str(), &info) == -1)
    {
        if (errno == ENOENT || errno == ENOTDIR)
            status = "404 Not Found";
        else if (errno == EACCES || errno == EPERM)
            status = "403 Forbidden";
        else
            status = "500 Internal Server Error";
        body = status + "\n";
        return;
    }

    if (!S_ISREG(info.st_mode))
    {
        status = "403 Forbidden";
        body = "403 Forbidden\n";
        return;
    }

    if (unlink(file_path.c_str()) == -1)
    {
        if (errno == ENOENT || errno == ENOTDIR)
            status = "404 Not Found";
        else if (errno == EACCES || errno == EPERM || errno == EROFS)
            status = "403 Forbidden";
        else
            status = "500 Internal Server Error";

        body = status + "\n";
        return;
    }

    status = "204 No Content";
    body.clear();
}


std::string Server::create_response_data(const HttpRequest &request, std::string &status, std::string &body)
{
    std::ostringstream header;
    header << request.version << " " << status << "\r\n";
    header << "Content-Length: " << body.size() << "\r\n";
    header << "Content-Type: text/html; charset=UTF-8\r\n";
    header << "Connection: close\r\n";
    header << "\r\n";
    return (header.str());
}

std::string Server::build_response(const HttpRequest &request,const ServerConfig &conf)
{
    std::string body;
    std::string status;

    if (request.path.empty() ||
        request.path[0] != '/' ||
        request.path.find("..") != std::string::npos)
    {
        status = "403 Forbidden";
        body = "403 Forbidden\n";
    }
    else if (request.method == "GET")
        handle_get(request, conf, status, body);
    else if (request.method == "POST")
        handle_post(request, status, body);
    else if (request.method == "DELETE")
        handle_delete(request, conf, status, body);
    else
    {
        status = "405 Method Not Allowed";
        body = "405 Method Not Allowed\n";
    }

    return create_response_data(request, status, body) + body;
}



std::string Server::build_file_path(const HttpRequest &request,const LocationConfig *location,const ServerConfig &conf) const
{
    std::string root;
    std::string relative_path;

    if (location != NULL)
    {
        if (location->root.empty())
            root = conf.root;
        else
            root = location->root;
        relative_path = request.path.substr(location->path.size());
    }
    else
    {
        root = conf.root;
        relative_path = request.path;   
    }
    if (!relative_path.empty() && relative_path[0] == '/')
        relative_path.erase(0, 1);
    if (!root.empty() && root[root.size() - 1] != '/')
        root += '/';
    return root + relative_path;
}


void Server::remove_client(int client_fd, std::vector<ClientConnection> &clients)
{
    for (std::vector<ClientConnection>::iterator it = clients.begin(); it != clients.end(); ++it)
    {
        if (it->fd == client_fd)
        {
            std::cout << "Closing client FD: " << it->fd << std::endl;
            close(it->fd);
            clients.erase(it);
            return;
        }
    }
}


std::size_t Server::get_content_length(const std::string &headers_part) const
{
   std::string::size_type cl_pos = headers_part.find("Content-Length:");
    if (cl_pos == std::string::npos)
        cl_pos = headers_part.find("content-length:");

    if (cl_pos != std::string::npos)
    {
        std::string::size_type colon = headers_part.find(':', cl_pos);
        if (colon != std::string::npos)
        {
            std::string length_str = headers_part.substr(colon + 1);
            return static_cast<std::size_t>(std::atoi(length_str.c_str()));
        }
    }
    return 0;
}


bool Server::check_max_body_size(ClientConnection &client, std::size_t content_length, const ServerConfig &conf)
{
    if (conf.client_max_body_size > 0 && content_length > conf.client_max_body_size)
    {
        std::cerr << "Error: Request body exceeds client_max_body_size" << std::endl;
        HttpRequest dummy_req;
        dummy_req.version = "HTTP/1.1";
        std::string status = "413 Payload Too Large";
        std::string body = "413 Payload Too Large\n";
        client.response_buffer = create_response_data(dummy_req, status, body) + body;
        client.request_complete = true;
        client.response_ready = true;
        return true;
    }
    return false;
}


void Server::parse_and_respond(ClientConnection &client, HttpParser &http_parser, const ServerConfig &conf)
{
    try
    {
        HttpRequest request = http_parser.parse(client.request_buffer);
        std::cout << "===== HTTP REQUEST =====" << std::endl;
        std::cout << "Method:  " << request.method << std::endl;
        std::cout << "Path:    " << request.path << std::endl;
        std::cout << "Version: " << request.version << std::endl;
        std::cout << "Host:    " << request.host << std::endl;
        std::cout << "========================" << std::endl;

        client.response_buffer = build_response(request, conf);
        client.request_complete = true;
        client.response_ready = true;
    }
    catch(const std::exception &e)
    {
        std::cerr << "Parse error: " << e.what() << std::endl;
        HttpRequest request;
        request.version = "HTTP/1.1";
        
        std::string err_msg = e.what();
        std::string status = "400 Bad Request";
        
        if (err_msg.size() >= 3 && std::isdigit(err_msg[0]) && std::isdigit(err_msg[1]) && std::isdigit(err_msg[2]))
        {
            status = err_msg;
        }
        
        std::string body = status + "\n";
        client.response_buffer = create_response_data(request, status, body) + body;
        client.request_complete = true;
        client.response_ready = true;
    }
}


bool Server::read_client_data(ClientConnection &client, bool &stop_processing)
{
    char buffer[4096];

    ssize_t bytes_read = recv(client.fd, buffer, sizeof(buffer), 0);

    if (bytes_read == 0)
        return false;

    if (bytes_read < 0)
    {
        stop_processing = true;
        return true;
    }

    client.last_activity = time(NULL);

    client.request_buffer.append(
        buffer,
        static_cast<std::string::size_type>(bytes_read)
    );

    stop_processing = false;
    return true;
}


bool Server::handle_client_event(ClientConnection &client, HttpParser &http_parser)
{
    bool stop_processing = false;
    if (!read_client_data(client, stop_processing))
        return false;
    if (stop_processing)
        return true;

    std::string::size_type header_end = client.request_buffer.find("\r\n\r\n");
    if (header_end == std::string::npos)
        return true; 
    const ServerConfig &conf = get_server_config(client);

    std::string headers_part = client.request_buffer.substr(0, header_end);
    std::size_t content_length = get_content_length(headers_part);

    if (check_max_body_size(client, content_length, conf))
        return true;

    std::string::size_type body_start = header_end + 4;
    if (client.request_buffer.size() - body_start < content_length)
        return true;

    parse_and_respond(client, http_parser, conf);
    return true;
}

const ServerConfig &Server::get_server_config(const ClientConnection &client) const
{
    if (client.listener_index >= _listen_config_indices.size())
        throw std::runtime_error("Invalid listener index");

    std::size_t config_index = _listen_config_indices[client.listener_index];
    if (config_index >= _configs.size())
        throw std::runtime_error("Invalid config index");

    return _configs[config_index];
}



bool Server::handle_client_write(ClientConnection &client)
{
    if (client.sent_bytes >= client.response_buffer.size())
        return false;

    ssize_t bytes_sent = send(client.fd, client.response_buffer.c_str() + client.sent_bytes,
        client.response_buffer.size() - client.sent_bytes,
        0);

    if (bytes_sent < 0)
        return true;

    if (bytes_sent == 0)
        return false;

    client.sent_bytes += static_cast<std::size_t>(bytes_sent);
    client.last_activity = time(NULL);

    if (client.sent_bytes < client.response_buffer.size())
        return true;
    std::cout << "Response fully sent to client FD: " << client.fd << std::endl;

    bool close_connection =(client.response_buffer.find("Connection: close") != std::string::npos);
    if (close_connection)
        return false;
    client.reset();
    return true;
}  

const ServerConfig *Server::find_server_config(const std::string &host) const
{
    if (_configs.empty())
        return NULL;
    
    std::string host_name = host;

    std::string::size_type colon = host_name.find(':');
    if (colon != std::string::npos)
        host_name.erase(colon);
    for (std::size_t i = 0; i < host_name.size(); ++i)
    {
        host_name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(host_name[i])));
    }
    for (std::size_t i = 0; i < _configs.size(); ++i)
    {
        if (_configs[i].server_name.empty())
            continue;

        std::string configured_name = _configs[i].server_name;

        for (std::size_t j = 0; j < configured_name.size(); ++j)
        {
            configured_name[j] = static_cast<char>(std::tolower(static_cast<unsigned char>(configured_name[j])));
        }
        if (configured_name == host_name)
            return &_configs[i];
    }
    return  NULL;
}

std::vector<struct pollfd> Server::build_poll_fds(const std::vector<ClientConnection> &clients) const
{
    std::vector<struct pollfd> fds;

    for (std::size_t i = 0; i < _server_fds.size(); ++i)
    {
        struct pollfd server_poll;
        server_poll.fd = _server_fds[i];
        server_poll.events = POLLIN;
        server_poll.revents = 0;
        fds.push_back(server_poll);
    }

    for (std::size_t i = 0; i < clients.size(); ++i)
    {
        struct pollfd client_poll;
        client_poll.fd = clients[i].fd;
        client_poll.events = clients[i].response_ready ? POLLOUT : POLLIN;
        client_poll.revents = 0;
        fds.push_back(client_poll);
    }
    return fds;
}


void Server::accept_ready_connections(const std::vector<struct pollfd> &fds, std::vector<ClientConnection> &clients)
{
    for (std::size_t i = 0; i < _server_fds.size(); ++i)
    {
        if (fds[i].revents & POLLIN)
        {
            int client_fd = accept_connection(_server_fds[i]);
        
            if (client_fd != -1)
            {
                std::cout << "Accepted client: " << client_fd << std::endl;
                clients.push_back(ClientConnection(client_fd, i));
            }
        }
    
        if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
            throw std::runtime_error("Listening socket error");
    }
}


int Server::wait_for_events(std::vector<struct pollfd> &fds, int timeout_ms) const
{
    if (fds.empty())
        return 0;

    int result = poll(&fds[0], fds.size(),timeout_ms);

    if (result == -1)
    {
        if (errno == EINTR)
            return -1;

        throw std::runtime_error("poll failed");
    }
    return result;
}

void Server::process_client_events(const std::vector<struct pollfd> &fds, std::vector<ClientConnection> &clients, HttpParser &parser)
{
    for (std::size_t i = _server_fds.size(); i < fds.size(); ++i)
    {
        int client_fd = fds[i].fd;
        short events = fds[i].revents;
        if (events == 0)
            continue;
        std::vector<ClientConnection>::iterator it = clients.begin();

        while (it != clients.end() && it->fd != client_fd)
            ++it;

        if (it == clients.end())
            continue;

        if (events & POLLNVAL)
        {
            remove_client(client_fd, clients);
            continue;
        }
        bool keep_client = true;
        if (events & POLLIN)
            keep_client = handle_client_event(*it, parser);

        if (keep_client && (events & POLLOUT))
            keep_client = handle_client_write(*it);

        if (events & POLLERR)
            keep_client = false;

        if ((events & POLLHUP) && !(events & POLLIN) && !it->response_ready)
            keep_client = false;

        if (!keep_client)
            remove_client(client_fd, clients);
    }
}


void Server::check_timeouts(std::vector<ClientConnection> &clients, time_t timeout_seconds)
{
    time_t current_time = time(NULL);
    for(std::size_t i = 0; i < clients.size(); )
    {
        if(current_time - clients[i].last_activity > timeout_seconds)
        {
            std::cout << "Client FD " << clients[i].fd << " timed out. Closing connection." << std::endl;
            close(clients[i].fd);
            clients.erase(clients.begin() + i);
        }
        else
            ++i;
    }
}

void Server::run(HttpParser &parser)
{
    std::vector<ClientConnection> clients;
    const time_t TIMEOUT_SECONDS = 5;
    while (true)
    {
        check_timeouts(clients, TIMEOUT_SECONDS);
        std::vector<struct pollfd> fds;

        fds = build_poll_fds(clients);

        int result = wait_for_events(fds, 1000);
        if (result <= 0)
            continue;       
        accept_ready_connections(fds, clients);
        
        process_client_events(fds, clients, parser);
    }
}


Server::~Server()
{
    for (std::size_t i = 0; i < _server_fds.size(); ++i)
    {
        if (_server_fds[i] != -1)
            close(_server_fds[i]);
    }
}