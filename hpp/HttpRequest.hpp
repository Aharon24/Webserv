#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include <string>
#include <map>
#include <cstddef>

struct HttpRequest
{
    std::string method;
    std::string path;
    std::string query;
    std::string version;
    std::string host;
    std::map<std::string, std::string> headers;
    std::string body;
    std::size_t content_length;
    bool has_content_length;

    HttpRequest()
        : content_length(0),
          has_content_length(false)
    {
    }
};

#endif
