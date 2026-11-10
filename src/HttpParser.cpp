#include "../hpp/HttpParser.hpp"
#include <sstream>
#include <stdexcept>
#include <cctype>

void HttpParser::parse_method(const std::string &method)
{
    if (method != "GET" && method != "POST" && method != "DELETE")
        throw std::runtime_error("Unsupported HTTP method");
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static bool is_token_character(unsigned char c)
{
    if (std::isalnum(c))
        return true;
    return c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
        c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' ||
        c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
}

static bool is_valid_chunk_extensions(const std::string &extensions)
{
    std::string::size_type pos = 0;

    while (pos < extensions.size())
    {
        if (extensions[pos++] != ';')
            return false;

        std::string::size_type name_start = pos;
        while (pos < extensions.size() &&
               is_token_character(static_cast<unsigned char>(extensions[pos])))
            ++pos;
        if (pos == name_start)
            return false;

        if (pos < extensions.size() && extensions[pos] == '=')
        {
            ++pos;
            if (pos == extensions.size())
                return false;
            if (extensions[pos] == '"')
            {
                ++pos;
                bool closed = false;
                while (pos < extensions.size())
                {
                    unsigned char c = static_cast<unsigned char>(extensions[pos++]);
                    if (c == '"')
                    {
                        closed = true;
                        break;
                    }
                    if (c == '\\')
                    {
                        if (pos == extensions.size())
                            return false;
                        c = static_cast<unsigned char>(extensions[pos++]);
                    }
                    if (c < 32 || c == 127)
                        return false;
                }
                if (!closed)
                    return false;
            }
            else
            {
                std::string::size_type value_start = pos;
                while (pos < extensions.size() &&
                       is_token_character(static_cast<unsigned char>(extensions[pos])))
                    ++pos;
                if (pos == value_start)
                    return false;
            }
        }
        if (pos < extensions.size() && extensions[pos] != ';')
            return false;
    }
    return true;
}

std::string HttpParser::decode_path(const std::string &input) const
{
    std::string decoded;

    for (std::string::size_type i = 0; i < input.size(); ++i)
    {
        if (input[i] == '%')
        {
            if (i + 2 >= input.size())
                throw std::runtime_error("Invalid request path");
            int hi = hex_value(input[i + 1]);
            int lo = hex_value(input[i + 2]);
            if (hi < 0 || lo < 0)
                throw std::runtime_error("Invalid request path");
            char decoded_char = static_cast<char>((hi << 4) | lo);
            if (decoded_char == '\0')
                throw std::runtime_error("Invalid request path");
            decoded += decoded_char;
            i += 2;
        }
        else
            decoded += input[i];
    }
    return decoded;
}

void HttpParser::parse_url(const std::string &raw_url, HttpRequest &request)
{
    if (raw_url.empty() || raw_url[0] != '/')
        throw std::runtime_error("Invalid request path");

    for (std::string::size_type i = 0; i < raw_url.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(raw_url[i]);
        if (c <= 31 || c == 127 || c == ' ' || c == '#')
            throw std::runtime_error("Invalid request path");
    }

    std::string::size_type query_pos = raw_url.find('?');
    std::string raw_path;

    if (query_pos == std::string::npos)
    {
        raw_path = raw_url;
        request.query.clear();
    }
    else
    {
        raw_path = raw_url.substr(0, query_pos);
        request.query = raw_url.substr(query_pos + 1);
    }

    request.path = decode_path(raw_path);
    if (request.path.empty() || request.path[0] != '/')
        throw std::runtime_error("Invalid request path");
    if (request.path.find("..") != std::string::npos)
        throw std::runtime_error("Invalid request path");
}

void HttpParser::parse_request_line(const std::string &line, HttpRequest &request)
{
    std::string::size_type first_space = line.find(' ');
    std::string::size_type second_space =
        first_space == std::string::npos ? std::string::npos : line.find(' ', first_space + 1);
    if (first_space == std::string::npos || first_space == 0 ||
        second_space == std::string::npos || second_space == first_space + 1 ||
        second_space + 1 == line.size() ||
        line.find(' ', second_space + 1) != std::string::npos)
        throw std::runtime_error("Invalid HTTP request line");

    request.method = line.substr(0, first_space);
    std::string raw_url = line.substr(first_space + 1, second_space - first_space - 1);
    request.version = line.substr(second_space + 1);

    parse_method(request.method);
    parse_url(raw_url, request);
    if (request.version != "HTTP/1.0" && request.version != "HTTP/1.1")
        throw std::runtime_error("Unsupported HTTP version");
}

bool HttpParser::is_valid_header_name(const std::string &name) const
{
    if (name.empty())
        return false;
    for (std::string::size_type i = 0; i < name.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (std::isalnum(c))
            continue;
        if (c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
            c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' ||
            c == '^' || c == '_' || c == '`' || c == '|' || c == '~')
            continue;
        return false;
    }
    return true;
}

void HttpParser::parse_header(const std::string &line, HttpRequest &request, bool &host_found)
{
    std::string::size_type colon = line.find(':');
    if (colon == std::string::npos || colon == 0)
        throw std::runtime_error("Invalid HTTP header");

    std::string name = line.substr(0, colon);
    std::string value = line.substr(colon + 1);

    if (!is_valid_header_name(name))
        throw std::runtime_error("Invalid HTTP header");

    for (std::size_t i = 0; i < name.size(); ++i)
        name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));

    while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
        value.erase(0, 1);
    while (!value.empty() && (value[value.size() - 1] == ' ' || value[value.size() - 1] == '\t'))
        value.erase(value.size() - 1);

    for (std::string::size_type i = 0; i < value.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if ((c < 32 && c != '\t') || c == 127)
            throw std::runtime_error("Invalid HTTP header value");
    }

    if (name == "host")
    {
        if (host_found)
            throw std::runtime_error("Duplicate Host header");
        if (value.empty())
            throw std::runtime_error("Empty Host header");
        request.host = value;
        host_found = true;
    }
    if (name == "content-length" && request.headers.find(name) != request.headers.end())
        throw std::runtime_error("Duplicate Content-Length header");

    if (request.headers.find(name) != request.headers.end())
        request.headers[name] += ", " + value;
    else
        request.headers[name] = value;
}

void HttpParser::parse_content_length(HttpRequest &request)
{
    std::map<std::string, std::string>::const_iterator it = request.headers.find("content-length");

    if (it == request.headers.end())
    {
        request.has_content_length = false;
        request.content_length = 0;
        return;
    }

    const std::string &value = it->second;
    if (value.empty())
        throw std::runtime_error("Invalid Content-Length");

    std::size_t length = 0;
    for (std::string::size_type i = 0; i < value.size(); ++i)
    {
        if (value[i] < '0' || value[i] > '9')
            throw std::runtime_error("Invalid Content-Length");
        std::size_t digit = static_cast<std::size_t>(value[i] - '0');
        if (length > (static_cast<std::size_t>(-1) / 10)
            || (length == (static_cast<std::size_t>(-1) / 10)
                && digit > (static_cast<std::size_t>(-1) % 10)))
            throw std::runtime_error("Invalid Content-Length");
        length = length * 10 + digit;
    }
    request.has_content_length = true;
    request.content_length = length;
}

void HttpParser::parse_body(const std::string &raw_body, HttpRequest &request)
{
    if (!request.has_content_length)
    {
        request.body.clear();
        return;
    }
    if (raw_body.size() < request.content_length)
        throw std::runtime_error("Incomplete request body");
    request.body = raw_body.substr(0, request.content_length);
}

bool HttpParser::parse_chunked_body(const std::string &raw_body, HttpRequest &request) const
{
    std::string::size_type pos = 0;
    request.body.clear();
    request.content_length = 0;

    while (true)
    {
        std::string::size_type line_end = raw_body.find("\r\n", pos);
        if (line_end == std::string::npos)
            return false;

        std::string size_line = raw_body.substr(pos, line_end - pos);
        std::string::size_type extension_start = size_line.find(';');
        std::string size_text = size_line.substr(0, extension_start);
        if (size_text.empty())
            throw std::runtime_error("Invalid chunk size");
        if (extension_start != std::string::npos &&
            !is_valid_chunk_extensions(size_line.substr(extension_start)))
            throw std::runtime_error("Invalid chunk extension");

        std::size_t chunk_size = 0;
        for (std::string::size_type i = 0; i < size_text.size(); ++i)
        {
            int digit = hex_value(size_text[i]);
            if (digit < 0 ||
                chunk_size > (static_cast<std::size_t>(-1) -
                              static_cast<std::size_t>(digit)) / 16)
                throw std::runtime_error("Invalid chunk size");
            chunk_size = chunk_size * 16 + static_cast<std::size_t>(digit);
        }

        pos = line_end + 2;
        if (chunk_size == 0)
        {
            while (true)
            {
                std::string::size_type trailer_end = raw_body.find("\r\n", pos);
                if (trailer_end == std::string::npos)
                    return false;
                if (trailer_end == pos)
                    return true;

                std::string trailer = raw_body.substr(pos, trailer_end - pos);
                std::string::size_type colon = trailer.find(':');
                if (colon == std::string::npos || colon == 0 ||
                    trailer[0] == ' ' || trailer[0] == '\t' ||
                    !is_valid_header_name(trailer.substr(0, colon)))
                    throw std::runtime_error("Invalid chunk trailer");

                std::string name = trailer.substr(0, colon);
                for (std::string::size_type i = 0; i < name.size(); ++i)
                    name[i] = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(name[i])));
                if (name == "content-length" || name == "transfer-encoding" ||
                    name == "host" || name == "trailer")
                    throw std::runtime_error("Invalid chunk trailer");

                for (std::string::size_type i = colon + 1; i < trailer.size(); ++i)
                {
                    unsigned char c = static_cast<unsigned char>(trailer[i]);
                    if ((c < 32 && c != '\t') || c == 127)
                        throw std::runtime_error("Invalid chunk trailer");
                }
                pos = trailer_end + 2;
            }
        }

        if (chunk_size > static_cast<std::size_t>(-1) - request.content_length)
            throw std::runtime_error("Invalid chunk size");
        request.content_length += chunk_size;

        std::string::size_type remaining = raw_body.size() - pos;
        if (chunk_size > remaining)
            return false;
        if (remaining - chunk_size < 2)
            return false;
        std::string::size_type chunk_end = pos + chunk_size;
        if (raw_body[chunk_end] != '\r' || raw_body[chunk_end + 1] != '\n')
            throw std::runtime_error("Invalid chunk terminator");

        request.body.append(raw_body, pos, chunk_size);
        pos = chunk_end + 2;
    }
}

HttpParser::ParseResult HttpParser::parse(const std::string &raw_request, HttpRequest &request)
{
    request = HttpRequest();
    std::string::size_type header_end = raw_request.find("\r\n\r\n");
    if (header_end == std::string::npos)
        return PARSE_INCOMPLETE;

    std::istringstream stream(raw_request.substr(0, header_end));
    std::string line;
    bool host_found = false;

    if (!std::getline(stream, line))
        throw std::runtime_error("Missing request line");
    if (!line.empty() && line[line.size() - 1] == '\r')
        line.erase(line.size() - 1);
    parse_request_line(line, request);
    while (std::getline(stream, line))
    {
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        if (line.empty())
            break;
        if (line[0] == ' ' || line[0] == '\t')
            throw std::runtime_error("Invalid HTTP header");
        parse_header(line, request, host_found);
    }
    if (request.version == "HTTP/1.1" && !host_found)
        throw std::runtime_error("Missing Host header");
    std::map<std::string, std::string>::const_iterator transfer_encoding =
        request.headers.find("transfer-encoding");
    bool chunked = transfer_encoding != request.headers.end();
    if (chunked)
    {
        std::string value = transfer_encoding->second;
        for (std::string::size_type i = 0; i < value.size(); ++i)
            value[i] = static_cast<char>(
                std::tolower(static_cast<unsigned char>(value[i])));
        if (value != "chunked")
            throw std::runtime_error("Unsupported Transfer-Encoding");
        if (request.version != "HTTP/1.1")
            throw std::runtime_error("Transfer-Encoding requires HTTP/1.1");
        if (request.headers.find("content-length") != request.headers.end())
            throw std::runtime_error("Content-Length with Transfer-Encoding");
    }
    parse_content_length(request);

    std::string::size_type body_start = header_end + 4;
    if (chunked)
        return parse_chunked_body(raw_request.substr(body_start), request)
            ? PARSE_COMPLETE : PARSE_INCOMPLETE;
    if (request.content_length > raw_request.size() - body_start)
        return PARSE_INCOMPLETE;
    parse_body(raw_request.substr(body_start), request);
    return PARSE_COMPLETE;
}
