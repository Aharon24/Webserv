
#include "../hpp/HttpRequest.hpp"
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <cctype>


void  HttpParser::parse_head_host(const std::string &line, HttpRequest &request, bool &host_found)
{
    std::string::size_type colon = line.find(':');
    if (colon == std::string::npos || colon == 0)
        throw std::runtime_error("Invalid HTTP header");
    std::string name = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
    }
    while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
        value.erase(0, 1);
    while (!value.empty() && (value[value.size() - 1] == ' ' || value[value.size() - 1] == '\t'))
        value.erase(value.size() - 1);
    if (name == "host")
    {
        if (host_found)
            throw std::runtime_error("Duplicate Host header");

        if (value.empty())
            throw std::runtime_error("Empty Host header");

        request.host = value;
        host_found = true;
    }

}

HttpRequest HttpParser::parse(const std::string &raw_request)
{
    HttpRequest request;
    std::istringstream stream(raw_request);
    std::string line;
    std::string extra;
    bool host_found = false;

    std::cout << "line ->" << raw_request << std::endl;
    if (!std::getline(stream, line))
        throw std::runtime_error("Missing request line");
    if (!line.empty() && line[line.size() - 1] == '\r')
        line.erase(line.size() - 1);
    std::istringstream request_line(line);
    if (!(request_line >> request.method >> request.path >> request.version))
        throw std::runtime_error("Invalid HTTP request line");
    if (request_line >> extra)
        throw std::runtime_error("Invalid HTTP request line");
    if (request.version != "HTTP/1.0" && request.version != "HTTP/1.1")
        throw std::runtime_error("Unsupported HTTP version");
    if (request.path.empty() || request.path[0] != '/')
        throw std::runtime_error("Invalid request path");
    while (std::getline(stream, line))
    {
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        if (line.empty())
            break;
        if (line[0] == ' ' || line[0] == '\t')
            throw std::runtime_error("Invalid HTTP header");
        parse_head_host(line, request, host_found);
    }
    if (request.version == "HTTP/1.1" && !host_found)
        throw std::runtime_error("Missing Host header");
    return request;
}