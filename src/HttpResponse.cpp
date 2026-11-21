#include "HttpResponse.hpp"

#include <cstdlib>
#include <sstream>

namespace
{
    const char *SERVER_NAME = "webserv/1.0";

    std::string to_lower(const std::string &text)
    {
        std::string result = text;

        for (std::size_t i = 0; i < result.size(); ++i)
        {
            if (result[i] >= 'A' && result[i] <= 'Z')
                result[i] = static_cast<char>(result[i] - 'A' + 'a');
        }
        return result;
    }

    std::string number_to_string(std::size_t value)
    {
        std::ostringstream ss;

        ss << value;
        return ss.str();
    }

    std::string two_digits(int value)
    {
        std::string result;

        result += static_cast<char>('0' + (value / 10) % 10);
        result += static_cast<char>('0' + value % 10);
        return result;
    }

    struct MimeEntry
    {
        const char *extension;
        const char *type;
    };

    const MimeEntry MIME_TABLE[] =
    {
        {"html", "text/html; charset=UTF-8"},
        {"htm", "text/html; charset=UTF-8"},
        {"css", "text/css; charset=UTF-8"},
        {"js", "application/javascript; charset=UTF-8"},
        {"mjs", "application/javascript; charset=UTF-8"},
        {"json", "application/json; charset=UTF-8"},
        {"xml", "application/xml; charset=UTF-8"},
        {"txt", "text/plain; charset=UTF-8"},
        {"md", "text/plain; charset=UTF-8"},
        {"csv", "text/csv; charset=UTF-8"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"svg", "image/svg+xml"},
        {"ico", "image/x-icon"},
        {"webp", "image/webp"},
        {"bmp", "image/bmp"},
        {"pdf", "application/pdf"},
        {"zip", "application/zip"},
        {"gz", "application/gzip"},
        {"tar", "application/x-tar"},
        {"mp3", "audio/mpeg"},
        {"wav", "audio/wav"},
        {"ogg", "audio/ogg"},
        {"mp4", "video/mp4"},
        {"webm", "video/webm"},
        {"woff", "font/woff"},
        {"woff2", "font/woff2"},
        {"ttf", "font/ttf"},
        {"otf", "font/otf"},
        {NULL, NULL}
    };
}

HttpResponse::HttpResponse() : _status(200), _reason("OK")
{
}

HttpResponse::HttpResponse(int status) : _status(status), _reason(reason_phrase(status))
{
}

void HttpResponse::set_status(int status)
{
    _status = status;
    _reason = reason_phrase(status);
}

void HttpResponse::set_status(int status, const std::string &reason)
{
    _status = status;
    _reason = reason;
}

int HttpResponse::get_status() const
{
    return _status;
}

const std::string &HttpResponse::get_reason() const
{
    return _reason;
}

// Header names are stored lowercase so set/get are case-insensitive.
void HttpResponse::set_header(const std::string &name, const std::string &value)
{
    _headers[to_lower(name)] = value;
}

bool HttpResponse::has_header(const std::string &name) const
{
    return _headers.find(to_lower(name)) != _headers.end();
}

std::string HttpResponse::get_header(const std::string &name) const
{
    std::map<std::string, std::string>::const_iterator it = _headers.find(to_lower(name));

    if (it == _headers.end())
        return "";
    return it->second;
}

void HttpResponse::set_body(const std::string &body, const std::string &content_type)
{
    _body = body;
    if (!content_type.empty())
        set_header("Content-Type", content_type);
}

const std::string &HttpResponse::get_body() const
{
    return _body;
}

bool HttpResponse::status_has_no_body(int status)
{
    return (status >= 100 && status < 200) || status == 204 || status == 304;
}

// Canonical spelling for the headers we emit (the map stores lowercase names).
static std::string canonical_header_name(const std::string &lower)
{
    std::string result = lower;
    bool upper_next = true;

    for (std::size_t i = 0; i < result.size(); ++i)
    {
        if (upper_next && result[i] >= 'a' && result[i] <= 'z')
            result[i] = static_cast<char>(result[i] - 'a' + 'A');
        upper_next = (result[i] == '-');
    }
    return result;
}

std::string HttpResponse::build_head(const std::string &version) const
{
    return build_head_internal(version, _body.size(), true);
}

std::string HttpResponse::build_head(const std::string &version,std::size_t body_size) const
{
    return build_head_internal(version, body_size, true);
}

std::string HttpResponse::build_head_streaming(const std::string &version) const
{
    return build_head_internal(version, 0, false);
}

std::string HttpResponse::build_head_internal(const std::string &version,std::size_t body_size,bool include_content_length) const
{
    std::ostringstream head;

    const std::string http_version =
        version.empty() ? "HTTP/1.1" : version;

    head << http_version << " " << _status << " "
         << _reason << "\r\n";

    head << "Server: " << SERVER_NAME << "\r\n";
    head << "Date: " << http_date(time(NULL)) << "\r\n";

    for (std::map<std::string, std::string>::const_iterator it =
             _headers.begin();
         it != _headers.end(); ++it)
    {
        if (it->first == "content-length"
            || it->first == "connection"
            || it->first == "date"
            || it->first == "server")
            continue;

        head << canonical_header_name(it->first)
             << ": " << it->second << "\r\n";
    }

    if (include_content_length &&
        !status_has_no_body(_status))
    {
        head << "Content-Length: "
             << number_to_string(body_size) << "\r\n";
    }

    head << "Connection: close\r\n";
    head << "\r\n";

    return head.str();
}


std::string HttpResponse::build(const std::string &version) const
{
    std::string result = build_head(version);

    if (status_has_no_body(_status))
        return result;

    result.append(_body);

    return result;
}

std::string HttpResponse::reason_phrase(int status)
{
    switch (status)
    {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default: break;
    }
    if (status >= 100 && status < 200)
        return "Informational";
    if (status >= 200 && status < 300)
        return "Success";
    if (status >= 300 && status < 400)
        return "Redirection";
    if (status >= 400 && status < 500)
        return "Client Error";
    return "Server Error";
}

// Looks at the text after the last '.' of the last path component.
std::string HttpResponse::mime_type(const std::string &path)
{
    std::string::size_type slash = path.find_last_of('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    std::string::size_type dot = name.find_last_of('.');

    if (dot == std::string::npos || dot + 1 >= name.size())
        return "application/octet-stream";

    const std::string extension = to_lower(name.substr(dot + 1));

    for (std::size_t i = 0; MIME_TABLE[i].extension != NULL; ++i)
    {
        if (extension == MIME_TABLE[i].extension)
            return MIME_TABLE[i].type;
    }
    return "application/octet-stream";
}

// RFC 7231 IMF-fixdate, always in English regardless of the locale.
std::string HttpResponse::http_date(time_t when)
{
    static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    struct tm parts;

    if (gmtime_r(&when, &parts) == NULL)
        return "Thu, 01 Jan 1970 00:00:00 GMT";

    std::ostringstream date;
    date << days[parts.tm_wday] << ", " << two_digits(parts.tm_mday) << " "
         << months[parts.tm_mon] << " " << (parts.tm_year + 1900) << " "
         << two_digits(parts.tm_hour) << ":" << two_digits(parts.tm_min) << ":"
         << two_digits(parts.tm_sec) << " GMT";
    return date.str();
}

std::string HttpResponse::html_escape(const std::string &text)
{
    std::string result;

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        switch (text[i])
        {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default: result += text[i]; break;
        }
    }
    return result;
}

HttpResponse HttpResponse::redirect(int status, const std::string &location)
{
    HttpResponse response(status);
    std::ostringstream body;

    body << "<!DOCTYPE html>\n<html>\n<head><title>" << status << " "
         << reason_phrase(status) << "</title></head>\n<body>\n<h1>"
         << status << " " << reason_phrase(status) << "</h1>\n<p>Moved to <a href=\""
         << html_escape(location) << "\">" << html_escape(location)
         << "</a></p>\n</body>\n</html>\n";
    response.set_header("Location", location);
    response.set_body(body.str(), "text/html; charset=UTF-8");
    return response;
}

// Built-in page used when no configured error_page exists (or it cannot be read).
HttpResponse HttpResponse::default_error(int status)
{
    HttpResponse response(status);
    std::ostringstream body;

    body << "<!DOCTYPE html>\n<html>\n<head><title>" << status << " "
         << reason_phrase(status) << "</title></head>\n<body>\n<h1>"
         << status << " " << reason_phrase(status) << "</h1>\n<hr>\n<p>"
         << SERVER_NAME << "</p>\n</body>\n</html>\n";
    response.set_body(body.str(), "text/html; charset=UTF-8");
    return response;
}

// "404 Not Found" + body  ->  HttpResponse.
// Content-Type is guessed: markup stays HTML, anything else is plain text.
HttpResponse HttpResponse::from_status_line(const std::string &status_line, const std::string &body)
{
    int code = std::atoi(status_line.c_str());

    if (code < 100 || code > 599)
        code = 500;

    HttpResponse response(code);
    std::string::size_type space = status_line.find(' ');

    if (space != std::string::npos && space + 1 < status_line.size())
        response.set_status(code, status_line.substr(space + 1));

    if (!body.empty())
    {
        if (body[0] == '<')
            response.set_body(body, "text/html; charset=UTF-8");
        else
            response.set_body(body, "text/plain; charset=UTF-8");
    }
    return response;
}
