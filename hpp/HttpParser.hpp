#ifndef HTTP_PARSER_HPP
#define HTTP_PARSER_HPP

#include "HttpRequest.hpp"
#include <string>

class HttpParser
{
public:
    enum ParseResult
    {
        PARSE_INCOMPLETE,
        PARSE_COMPLETE
    };

    ParseResult parse(const std::string &raw_request, HttpRequest &request);
    void parse_request_line(const std::string &line, HttpRequest &request);
    void parse_method(const std::string &method);
    void parse_url(const std::string &raw_url, HttpRequest &request);
    void parse_header(const std::string &line, HttpRequest &request, bool &host_found);
    void parse_content_length(HttpRequest &request);
    void parse_body(const std::string &raw_body, HttpRequest &request);

private:
    std::string decode_path(const std::string &input) const;
    bool is_valid_header_name(const std::string &name) const;
    bool parse_chunked_body(const std::string &raw_body, HttpRequest &request) const;
};

#endif
