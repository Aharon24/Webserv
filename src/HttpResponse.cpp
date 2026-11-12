#include "HttpResponse.hpp"

#include <cctype>
#include <cstddef>
#include <sstream>

namespace
{
    struct MimeEntry
    {
        const char *ext;
        const char *type;
    };

    const MimeEntry g_mime_types[] = {
        {"html", "text/html; charset=UTF-8"},
        {"htm", "text/html; charset=UTF-8"},
        {"css", "text/css; charset=UTF-8"},
        {"js", "application/javascript; charset=UTF-8"},
        {"json", "application/json; charset=UTF-8"},
        {"xml", "application/xml; charset=UTF-8"},
        {"txt", "text/plain; charset=UTF-8"},
        {"csv", "text/csv; charset=UTF-8"},
        {"md", "text/plain; charset=UTF-8"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"svg", "image/svg+xml"},
        {"webp", "image/webp"},
        {"ico", "image/x-icon"},
        {"bmp", "image/bmp"},
        {"pdf", "application/pdf"},
        {"zip", "application/zip"},
        {"mp3", "audio/mpeg"},
        {"wav", "audio/wav"},
        {"mp4", "video/mp4"},
        {"webm", "video/webm"},
        {"woff", "font/woff"},
        {"woff2", "font/woff2"},
        {"ttf", "font/ttf"},
        {NULL, NULL}
    };
}

HttpResponse::HttpResponse() : _status(200)
{
}

HttpResponse::HttpResponse(int status_code) : _status(status_code)
{
}

void HttpResponse::set_status(int code)
{
    _status = code;
}

int HttpResponse::get_status() const
{
    return _status;
}

void HttpResponse::set_header(const std::string &name, const std::string &value)
{
    _headers[name] = value;
}

void HttpResponse::set_body(const std::string &body, const std::string &content_type)
{
    _body = body;
    _headers["Content-Type"] = content_type;
}

const std::string &HttpResponse::get_body() const
{
    return _body;
}

std::string HttpResponse::to_string(const std::string &version) const
{
    const bool no_body = (_status == 204 || _status == 304
                          || (_status >= 100 && _status < 200));
    std::ostringstream head;

    head << (version.empty() ? std::string("HTTP/1.1") : version) << " "
         << _status << " " << reason_phrase(_status) << "\r\n";
    head << "Server: webserv\r\n";
    for (std::map<std::string, std::string>::const_iterator it = _headers.begin();
         it != _headers.end(); ++it)
    {
        if (it->first == "Content-Length" || it->first == "Connection")
            continue;
        head << it->first << ": " << it->second << "\r\n";
    }
    if (!no_body)
        head << "Content-Length: " << _body.size() << "\r\n";
    head << "Connection: close\r\n\r\n";
    if (no_body)
        return head.str();
    return head.str() + _body;
}

std::string HttpResponse::reason_phrase(int code)
{
    switch (code)
    {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default:  return "Unknown";
    }
}

std::string HttpResponse::mime_type(const std::string &path)
{
    const std::string::size_type slash = path.find_last_of('/');
    const std::string::size_type dot = path.find_last_of('.');

    if (dot == std::string::npos
        || (slash != std::string::npos && dot < slash))
        return "application/octet-stream";

    std::string ext = path.substr(dot + 1);
    for (std::size_t i = 0; i < ext.size(); ++i)
        ext[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i])));
    for (std::size_t i = 0; g_mime_types[i].ext != NULL; ++i)
    {
        if (ext == g_mime_types[i].ext)
            return g_mime_types[i].type;
    }
    return "application/octet-stream";
}

std::string HttpResponse::default_error_body(int code)
{
    std::ostringstream title;
    title << code << " " << reason_phrase(code);

    std::ostringstream page;
    page << "<!DOCTYPE html>\n<html><head><meta charset=\"UTF-8\"><title>"
         << title.str() << "</title></head>\n<body><h1>" << title.str()
         << "</h1><hr><p>webserv</p></body></html>\n";
    return page.str();
}

std::string HttpResponse::html_escape(const std::string &text)
{
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        switch (text[i])
        {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default:   out += text[i];
        }
    }
    return out;
}
