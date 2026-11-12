#include "Router.hpp"
#include "StaticFileHandler.hpp"

#include <cstdlib>

namespace
{
    // "/images/" -> 1 char shorter, "/" -> 0. Matching is done on this length.
    std::size_t prefix_length(const std::string &path)
    {
        std::size_t len = path.size();
        while (len > 0 && path[len - 1] == '/')
            --len;
        return len;
    }

    int hex_value(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }
}

Router::Router(const ServerConfig &conf) : _conf(conf)
{
}

// Longest-prefix match on a segment boundary: "/img" matches "/img" and
// "/img/a.png" but not "/imgs".
const LocationConfig *Router::find_location(const std::string &decoded_uri) const
{
    const LocationConfig *best = NULL;
    std::size_t best_len = 0;

    for (std::size_t i = 0; i < _conf.locations.size(); ++i)
    {
        const std::string &prefix = _conf.locations[i].path;
        const std::size_t len = prefix_length(prefix);

        if (decoded_uri.compare(0, len, prefix, 0, len) != 0)
            continue;
        if (decoded_uri.size() > len && decoded_uri[len] != '/')
            continue;
        if (best == NULL || len > best_len)
        {
            best = &_conf.locations[i];
            best_len = len;
        }
    }
    return best;
}

// Same mapping as Server::build_file_path (location prefix is replaced by root),
// but on the decoded URI. Returns "" when no root is configured.
std::string Router::build_path(const std::string &decoded_uri,
                               const LocationConfig *location) const
{
    std::string root = _conf.root;
    std::string relative = decoded_uri;

    if (location != NULL)
    {
        if (!location->root.empty())
            root = location->root;
        relative = decoded_uri.substr(prefix_length(location->path));
    }
    if (root.empty())
        return "";
    if (!relative.empty() && relative[0] == '/')
        relative.erase(0, 1);
    if (root[root.size() - 1] != '/')
        root += '/';
    return root + relative;
}

bool Router::url_decode(const std::string &in, std::string &out)
{
    out.clear();
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        if (in[i] != '%')
        {
            out += in[i];
            continue;
        }
        if (i + 2 >= in.size())
            return false;
        const int hi = hex_value(in[i + 1]);
        const int lo = hex_value(in[i + 2]);
        if (hi < 0 || lo < 0)
            return false;
        out += static_cast<char>(hi * 16 + lo);
        i += 2;
    }
    return true;
}

// Rejects NUL bytes and any ".." path segment (after decoding, so %2e%2e too).
bool Router::is_safe_path(const std::string &decoded_uri)
{
    if (decoded_uri.empty() || decoded_uri[0] != '/')
        return false;
    if (decoded_uri.find('\0') != std::string::npos)
        return false;

    std::string::size_type start = 0;
    while (start <= decoded_uri.size())
    {
        std::string::size_type end = decoded_uri.find('/', start);
        if (end == std::string::npos)
            end = decoded_uri.size();
        if (decoded_uri.compare(start, end - start, "..") == 0)
            return false;
        start = end + 1;
    }
    return true;
}

bool Router::prepare(const HttpRequest &request, HttpResponse &response) const
{
    std::string uri;

    if (!url_decode(request.path, uri))
    {
        response = error_response(400);
        return true;
    }
    if (!is_safe_path(uri))
    {
        response = error_response(403);
        return true;
    }

    const LocationConfig *location = find_location(uri);
    if (location == NULL)
        return false;

    if (location->return_code != 0)
    {
        response = redirect_response(location->return_code, location->return_url);
        return true;
    }
    if (!location->allowed_methods.empty())
    {
        bool allowed = false;
        std::string allow_header;

        for (std::size_t i = 0; i < location->allowed_methods.size(); ++i)
        {
            if (location->allowed_methods[i] == request.method)
                allowed = true;
            if (i > 0)
                allow_header += ", ";
            allow_header += location->allowed_methods[i];
        }
        if (!allowed)
        {
            response = error_response(405);
            response.set_header("Allow", allow_header);
            return true;
        }
    }
    return false;
}

HttpResponse Router::handle_get(const HttpRequest &request) const
{
    std::string uri;

    if (!url_decode(request.path, uri) || !is_safe_path(uri))
        return error_response(400);

    const LocationConfig *location = find_location(uri);
    const std::string path = build_path(uri, location);
    if (path.empty())
        return error_response(500);

    std::string index = _conf.index;
    bool autoindex = false;
    if (location != NULL)
    {
        if (!location->index.empty())
            index = location->index;
        autoindex = location->autoindex;
    }

    HttpResponse response;
    const int code = StaticFileHandler::serve(path, request.path,
                                              request.query_string,
                                              index, autoindex, response);
    if (code >= 400)
        return error_response(code);
    return response;
}

bool Router::load_error_page(const std::string &uri, std::string &body) const
{
    if (!is_safe_path(uri))
        return false;
    const std::string path = build_path(uri, find_location(uri));
    if (path.empty())
        return false;
    return StaticFileHandler::read_file(path, body);
}

HttpResponse Router::error_response(int code) const
{
    HttpResponse response(code);
    std::string body;

    std::map<int, std::string>::const_iterator it = _conf.error_pages.find(code);
    if (it != _conf.error_pages.end() && load_error_page(it->second, body))
        response.set_body(body, "text/html; charset=UTF-8");
    else
        response.set_body(HttpResponse::default_error_body(code),
                          "text/html; charset=UTF-8");
    return response;
}

HttpResponse Router::redirect_response(int code, const std::string &url) const
{
    HttpResponse response(code);

    response.set_header("Location", url);
    response.set_body("<html><body>" + HttpResponse::html_escape(
                          HttpResponse::reason_phrase(code))
                      + ": <a href=\"" + HttpResponse::html_escape(url) + "\">"
                      + HttpResponse::html_escape(url) + "</a></body></html>\n",
                      "text/html; charset=UTF-8");
    return response;
}

// Adapts handlers that still produce ("404 Not Found", body) pairs.
HttpResponse Router::from_status_line(const std::string &status,
                                      const std::string &body) const
{
    int code = std::atoi(status.c_str());

    if (code < 100 || code > 599)
        code = 500;
    if (code >= 400)
        return error_response(code);

    HttpResponse response(code);
    if (code != 204 && code != 304)
        response.set_body(body, "text/html; charset=UTF-8");
    return response;
}
