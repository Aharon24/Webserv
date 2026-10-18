#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include <string>

struct HttpRequest
{
    std::string method;
    std::string path;
    std::string version;
};

class HttpParser
{
public:
    HttpRequest parse(const std::string &raw_request);
};

#endif