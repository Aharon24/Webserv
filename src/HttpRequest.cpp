
#include "../hpp/HttpRequest.hpp"
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <cctype>


std::string HttpParser::unchunk_body(const std::string &chunked_body)
{
    std::string decoded_body = "";
    std::string::size_type i = 0;

    while (i < chunked_body.size())
    {
        std::string::size_type crlf = chunked_body.find("\r\n", i);
        if (crlf == std::string::npos)
            break;

        std::string hex_size_str = chunked_body.substr(i, crlf - i);
        std::size_t chunk_size = 0;

        std::stringstream ss;
        ss << std::hex << hex_size_str;
        ss >> chunk_size;

        if (chunk_size == 0)
            break;
        i = crlf + 2;
        if (i + chunk_size > chunked_body.size())
            break;
        decoded_body.append(chunked_body, i, chunk_size);
        i += chunk_size + 2;
    }
    return decoded_body;
}


void  HttpParser::parse_head(const std::string &line, HttpRequest &request, bool &host_found)
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
    request.headers[name] = value;
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
    
    std::string::size_type query_pos = request.path.find('?');
    if (query_pos != std::string::npos)
    {
        request.query_string = request.path.substr(query_pos + 1);
        request.path = request.path.substr(0, query_pos);
    }
    if (request.path.size() > 2048)
        throw std::runtime_error("414 URI Too Long");
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
        parse_head(line, request, host_found);
    }
    if (request.version == "HTTP/1.1" && !host_found)
        throw std::runtime_error("Missing Host header");
    std::string::size_type body_pos = raw_request.find("\r\n\r\n");
    if (body_pos != std::string::npos)
        request.body = raw_request.substr(body_pos + 4);
    else
    {
        body_pos = raw_request.find("\n\n");
        if (body_pos != std::string::npos)
            request.body = raw_request.substr(body_pos + 2);
    }

    std::map<std::string, std::string>::iterator it = request.headers.find("transfer-encoding");
    if (it != request.headers.end() && it->second.find("chunked") != std::string::npos)
    {
        request.body = unchunk_body(request.body);
    }
    if (request.method == "POST")
    {
        bool has_cl = (request.headers.find("content-length") != request.headers.end());
        bool has_te = (request.headers.find("transfer-encoding") != request.headers.end());
        if (!has_cl && !has_te)
            throw std::runtime_error("411 Length Required");
    }

    return request;
}