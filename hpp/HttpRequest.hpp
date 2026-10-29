#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include <string>

struct HttpRequest
{
    std::string method;
    std::string path;
    std::string version;
    std::string host;
};

class HttpParser
{
public:
    HttpRequest parse(const std::string &raw_request);
    void parse_head_host(const std::string &line, HttpRequest &request, bool &host_found);
};

#endif