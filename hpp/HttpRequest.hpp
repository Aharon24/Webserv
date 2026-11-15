#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include <string>
#include <map>
#include <cstddef>

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

enum ChunkedStatus
{
    CHUNKED_INCOMPLETE,
    CHUNKED_COMPLETE,
    CHUNKED_INVALID,
    CHUNKED_TOO_LARGE
};

class HttpParser
{
public:
    HttpRequest parse(const std::string &raw_request);
    void parse_head(const std::string &line, HttpRequest &request, bool &host_found);


    static ChunkedStatus unchunk_body(const std::string &chunked_body, std::string &decoded_body, std::size_t max_body_size=0);
    static  bool parse_hex_size(const std::string &size_line, std::size_t &out_chunk_size);
    static ChunkedStatus skip_trailers(const std::string &chunked_body, std::string::size_type &i);
};

#endif