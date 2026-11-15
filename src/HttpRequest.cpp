
#include "../hpp/HttpRequest.hpp"
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <cctype>




bool  HttpParser::parse_hex_size(const std::string &size_line, std::size_t &out_chunk_size)
{
    std::size_t chunk_size = 0;

    for (std::size_t j = 0; j < size_line.size(); ++j)
    {
        unsigned int digit;

        if (size_line[j] >= '0' && size_line[j] <= '9')
            digit = size_line[j] - '0';
        else if (size_line[j] >= 'a' && size_line[j] <= 'f')
            digit = size_line[j] - 'a' + 10;
        else if (size_line[j] >= 'A' && size_line[j] <= 'F')
            digit = size_line[j] - 'A' + 10;
        else
            return false;

        if (chunk_size > (static_cast<std::size_t>(-1) - digit) / 16)
            return false;

        chunk_size = chunk_size * 16 + digit;
    }

    out_chunk_size = chunk_size;
    return true;
}

ChunkedStatus  HttpParser::skip_trailers(const std::string &chunked_body, std::string::size_type &i)
{
    while (true)
    {
        std::string::size_type trailer_end = chunked_body.find("\r\n", i);

        if (trailer_end == std::string::npos)
            return CHUNKED_INCOMPLETE;

        if (trailer_end == i)
            return CHUNKED_COMPLETE;

        std::string trailer = chunked_body.substr(i, trailer_end - i);

        if (trailer.find(':') == std::string::npos)
            return CHUNKED_INVALID;

        i = trailer_end + 2;
    }
}

ChunkedStatus HttpParser::unchunk_body(const std::string &chunked_body, std::string &decoded_body, std::size_t max_body_size)
{
    decoded_body.clear();
    std::string::size_type i = 0;

    while (true)
    {
        std::string::size_type crlf = chunked_body.find("\r\n", i);

        if (crlf == std::string::npos)
            return CHUNKED_INCOMPLETE;

        std::string size_line = chunked_body.substr(i, crlf - i);

        std::string::size_type extension = size_line.find(';');

        if (extension != std::string::npos)
            size_line.erase(extension);

        if (size_line.empty())
            return CHUNKED_INVALID;

        std::size_t chunk_size = 0;
        if (!parse_hex_size(size_line, chunk_size))
            return CHUNKED_INVALID;

        i = crlf + 2;

        if (chunk_size == 0)
        {
            return skip_trailers(chunked_body, i);
        }

        if (max_body_size > 0 &&(decoded_body.size() > max_body_size || chunk_size > max_body_size - decoded_body.size()))
            return CHUNKED_TOO_LARGE;

        if (chunk_size > chunked_body.size() - i)
            return CHUNKED_INCOMPLETE;


        decoded_body.append(chunked_body, i, chunk_size);
        i += chunk_size;

        if (chunked_body.size() - i < 2)
            return CHUNKED_INCOMPLETE;

        if (chunked_body.compare(i, 2, "\r\n") != 0)
            return CHUNKED_INVALID;

        i += 2;
    }
}
void HttpParser::parse_head(const std::string &line, HttpRequest &request, bool &host_found)
{
    std::string::size_type colon = line.find(':');

    if (colon == std::string::npos || colon == 0)
        throw std::runtime_error("Invalid HTTP header");

    std::string name = line.substr(0, colon);
    std::string value = line.substr(colon + 1);

    for (std::size_t i = 0; i < name.size(); ++i)
    {
        name[i] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(name[i]))
        );
    }

    while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
        value.erase(0, 1);

    while (!value.empty() &&
           (value[value.size() - 1] == ' ' ||
            value[value.size() - 1] == '\t'))
        value.erase(value.size() - 1);

    if (name == "host")
    {
        if (host_found)
            throw std::runtime_error("Duplicate Host header");

        if (value.empty())
            throw std::runtime_error("Empty Host header");
    }

    if (request.headers.find(name) != request.headers.end())
        throw std::runtime_error("Duplicate HTTP header: " + name);

    if (name == "host")
    {
        request.host = value;
        host_found = true;
    }

    request.headers.insert(
        std::map<std::string, std::string>::value_type(name, value)
    );
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
    bool has_transfer_encoding = request.headers.find("transfer-encoding") != request.headers.end();

    bool has_content_length = request.headers.find("content-length") != request.headers.end();

    if (has_transfer_encoding && has_content_length)
        throw std::runtime_error("Transfer-Encoding and Content-Length cannot be used together");
    std::string::size_type body_pos = raw_request.find("\r\n\r\n");
    if (body_pos != std::string::npos)
        request.body = raw_request.substr(body_pos + 4);
    else
    {
        body_pos = raw_request.find("\n\n");
        if (body_pos != std::string::npos)
            request.body = raw_request.substr(body_pos + 2);
    }

    std::map<std::string, std::string>::iterator it =
    request.headers.find("transfer-encoding");

    if (it != request.headers.end())
    {
        std::string transfer_encoding = it->second;

        // Make Transfer-Encoding value lowercase
        for (std::size_t i = 0; i < transfer_encoding.size(); ++i)
        {
            transfer_encoding[i] = static_cast<char>(
                std::tolower(static_cast<unsigned char>(transfer_encoding[i]))
            );
        }

        if (transfer_encoding != "chunked")
            throw std::runtime_error("Unsupported Transfer-Encoding");

        std::string decoded_body;

        ChunkedStatus status = unchunk_body(request.body, decoded_body);

        if (status == CHUNKED_INCOMPLETE)
            throw std::runtime_error("Incomplete chunked body");

        if (status == CHUNKED_INVALID)
            throw std::runtime_error("Invalid chunked body");

        if (status == CHUNKED_TOO_LARGE)
            throw std::runtime_error("Chunked body too large");

        request.body = decoded_body;
    }
    return request;
}