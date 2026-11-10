#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include <string>
#include <map>

struct HttpRequest
{
    std::string method;
    std::string path;
    std::string version;
    std::string host;
    std::string query_string;
    std::map<std::string, std::string> headers;
    std::string body;
};

class HttpParser
{
public:
    HttpRequest parse(const std::string &raw_request);
    void parse_head(const std::string &line, HttpRequest &request, bool &host_found);
    static std::string unchunk_body(const std::string &chunked_body);
};

#endif