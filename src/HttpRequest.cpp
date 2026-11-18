
#include "../hpp/HttpRequest.hpp"
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <cctype>
#include <limits>




bool  HttpParser::parse_hex_size(const std::string &size_line, std::size_t &out_chunk_size)
{
    std::size_t chunk_size = 0;

    if (size_line.empty())
        return false;
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

// Shared framing rules for socket accumulation and the complete request parser.
bool HttpParser::get_body_framing(const std::string &headers_part,
                                  bool &chunked, std::size_t &content_length)
{
    chunked = false;
    content_length = 0;
    bool has_length = false;
    bool has_encoding = false;
    std::string::size_type pos = headers_part.find("\r\n");
    if (pos == std::string::npos)
        return true;
    pos += 2;
    while (pos < headers_part.size())
    {
        std::string::size_type end = headers_part.find("\r\n", pos);
        if (end == std::string::npos)
            end = headers_part.size();
        if (end == pos)
            break;
        const std::string::size_type colon = headers_part.find(':', pos);
        if (colon == std::string::npos || colon == pos || colon >= end)
            return false;
        std::string name = headers_part.substr(pos, colon - pos);
        for (std::size_t i = 0; i < name.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(name[i]);
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9')
                || std::string("!#$%&'*+-.^_`|~").find(c) != std::string::npos))
                return false;
            if (c >= 'A' && c <= 'Z')
                name[i] = static_cast<char>(c + ('a' - 'A'));
        }
        std::string value = headers_part.substr(colon + 1, end - colon - 1);
        const std::string::size_type first = value.find_first_not_of(" \t");
        if (first == std::string::npos)
            value.clear();
        else
            value = value.substr(first, value.find_last_not_of(" \t") - first + 1);
        if (name == "content-length")
        {
            if (has_length || value.empty())
                return false;
            has_length = true;
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                if (value[i] < '0' || value[i] > '9')
                    return false;
                const std::size_t digit = static_cast<std::size_t>(value[i] - '0');
                if (content_length > (std::numeric_limits<std::size_t>::max() - digit) / 10)
                    return false;
                content_length = content_length * 10 + digit;
            }
        }
        else if (name == "transfer-encoding")
        {
            if (has_encoding)
                return false;
            has_encoding = true;
            for (std::size_t i = 0; i < value.size(); ++i)
                if (value[i] >= 'A' && value[i] <= 'Z')
                    value[i] = static_cast<char>(value[i] + ('a' - 'A'));
            if (value != "chunked")
                return false;
            chunked = true;
        }
        pos = end + 2;
    }
    return !(has_length && has_encoding);
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
    const std::string::size_type body_pos = raw_request.find("\r\n\r\n");
    if (body_pos == std::string::npos)
        throw std::runtime_error("Incomplete HTTP headers");
    bool chunked = false;
    std::size_t content_length = 0;
    if (!get_body_framing(raw_request.substr(0, body_pos), chunked, content_length))
        throw std::runtime_error("Invalid HTTP body framing");
    const std::size_t body_start = body_pos + 4;
    if (chunked)
    {
        std::string decoded_body;
        const ChunkedStatus status =
            unchunk_body(raw_request.substr(body_start), decoded_body);
        if (status != CHUNKED_COMPLETE)
            throw std::runtime_error("Invalid or incomplete chunked body");
        request.body.swap(decoded_body);
    }
    else
    {
        if (raw_request.size() - body_start < content_length)
            throw std::runtime_error("Incomplete HTTP body");
        request.body = raw_request.substr(body_start, content_length);
    }
    return request;
}