
#include "../hpp/HttpRequest.hpp"
#include <sstream>
#include <stdexcept>


HttpRequest HttpParser::parse(const std::string &row_reqvest)
{
    HttpRequest request;

    if (row_reqvest.empty())
        throw std::runtime_error("Empty HTTP request");

    std::string request_line;
    std::istringstream stream(row_reqvest);
    if (!(stream >> request.method >> request.path >> request.version))
        throw std::runtime_error("Invalid HTTP request line");
    if (request.version != "HTTP/1.0" && request.version != "HTTP/1.1")
        throw std::runtime_error("Unsupported HTTP version");
    if (request.path.empty() || request.path[0] != '/')
        throw std::runtime_error("Invalid request path");

    return request;
}