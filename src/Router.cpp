#include "Router.hpp"

#include <vector>

namespace
{
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

    bool is_unreserved(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '-' || c == '.'
            || c == '_' || c == '~' || c == '/';
    }

    // Decodes %XX sequences exactly once. Fails on bad hex or an encoded NUL.
    bool percent_decode(const std::string &raw, std::string &decoded)
    {
        decoded.clear();
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            if (raw[i] != '%')
            {
                if (raw[i] == '\0')
                    return false;
                decoded += raw[i];
                continue;
            }
            if (i + 2 >= raw.size())
                return false;
            int high = hex_value(raw[i + 1]);
            int low = hex_value(raw[i + 2]);
            if (high < 0 || low < 0)
                return false;
            char value = static_cast<char>(high * 16 + low);
            if (value == '\0')
                return false;
            decoded += value;
            i += 2;
        }
        return true;
    }

    // Joins root and relative part with exactly one '/' between them.
    std::string join_path(const std::string &root, const std::string &relative)
    {
        std::string result = root;
        std::size_t start = 0;

        while (start < relative.size() && relative[start] == '/')
            ++start;
        while (result.size() > 1 && result[result.size() - 1] == '/')
            result.erase(result.size() - 1);
        if (result.empty() || result[result.size() - 1] != '/')
            result += '/';
        result += relative.substr(start);
        return result;
    }
}

Router::Router(const ServerConfig &conf) : _conf(conf)
{
}

Router::Router(const Router &other) : _conf(other._conf)
{
}

Router::~Router()
{
}

NormalizeStatus Router::normalize_uri(const std::string &raw, std::string &normalized)
{
    std::string decoded;

    normalized = "/";
    if (raw.empty() || raw[0] != '/')
        return NORMALIZE_BAD_REQUEST;
    if (!percent_decode(raw, decoded))
        return NORMALIZE_BAD_REQUEST;

    std::vector<std::string> segments;
    std::size_t start = 0;

    while (start <= decoded.size())
    {
        std::size_t end = decoded.find('/', start);
        if (end == std::string::npos)
            end = decoded.size();

        const std::string segment = decoded.substr(start, end - start);

        if (segment == "..")
        {
            if (segments.empty())
                return NORMALIZE_FORBIDDEN;
            segments.pop_back();
        }
        else if (!segment.empty() && segment != ".")
            segments.push_back(segment);
        start = end + 1;
    }

    normalized.clear();
    for (std::size_t i = 0; i < segments.size(); ++i)
    {
        normalized += '/';
        normalized += segments[i];
    }
    if (normalized.empty())
        normalized = "/";

    // A trailing slash ("/dir/", "/dir/.", "/dir/..") still means "directory".
    const std::string::size_type last_slash = decoded.find_last_of('/');
    const std::string last = decoded.substr(last_slash + 1);
    if (normalized != "/" && (last.empty() || last == "." || last == ".."))
        normalized += '/';
    return NORMALIZE_OK;
}

std::string Router::encode_uri(const std::string &path)
{
    static const char *hex = "0123456789ABCDEF";
    std::string result;

    for (std::size_t i = 0; i < path.size(); ++i)
    {
        if (is_unreserved(path[i]))
            result += path[i];
        else
        {
            unsigned char byte = static_cast<unsigned char>(path[i]);
            result += '%';
            result += hex[byte >> 4];
            result += hex[byte & 0x0F];
        }
    }
    return result;
}

const LocationConfig *Router::find_location(const std::string &uri) const
{
    const LocationConfig *best = NULL;

    for (std::size_t i = 0; i < _conf.locations.size(); ++i)
    {
        const std::string &prefix = _conf.locations[i].path;
        bool matches = false;

        if (prefix.empty())
            continue;

        if (prefix == "/")
            matches = true;
        else if (prefix[prefix.size() - 1] == '/')
        {
            // "/images/" matches "/images/x" and also "/images" itself.
            matches = uri.compare(0, prefix.size(), prefix) == 0
                || uri == prefix.substr(0, prefix.size() - 1);
        }
        else if (uri.size() >= prefix.size() && uri.compare(0, prefix.size(), prefix) == 0)
            matches = (uri.size() == prefix.size() || uri[prefix.size()] == '/');

        if (matches && (best == NULL || prefix.size() > best->path.size()))
            best = &_conf.locations[i];
    }
    return best;
}

std::string Router::get_root(const LocationConfig *location) const
{
    if (location != NULL && !location->root.empty())
        return location->root;
    return _conf.root;
}

std::string Router::get_index(const LocationConfig *location) const
{
    if (location != NULL && !location->index.empty())
        return location->index;
    return _conf.index;
}

// The part of the URI that matched the location is replaced by the root, so
// "location /kapouet { root /tmp/www; }" maps /kapouet/a/b to /tmp/www/a/b.
std::string Router::build_path(const std::string &uri, const LocationConfig *location) const
{
    std::string relative = uri;

    if (location != NULL && location->path != "/")
    {
        std::string prefix = location->path;

        if (prefix[prefix.size() - 1] == '/')
            prefix.erase(prefix.size() - 1);
        relative = uri.substr(prefix.size());
    }
    return join_path(get_root(location), relative);
}

bool Router::is_method_allowed(const LocationConfig *location, const std::string &method) const
{
    if (method != "GET" && method != "POST" && method != "DELETE")
        return false;
    if (location == NULL || location->allowed_methods.empty())
        return true;
    for (std::size_t i = 0; i < location->allowed_methods.size(); ++i)
    {
        if (location->allowed_methods[i] == method)
            return true;
    }
    return false;
}

std::string Router::allowed_methods_header(const LocationConfig *location) const
{
    static const char *all[] = {"GET", "POST", "DELETE"};
    std::string result;

    for (std::size_t i = 0; i < 3; ++i)
    {
        if (!is_method_allowed(location, all[i]))
            continue;
        if (!result.empty())
            result += ", ";
        result += all[i];
    }
    return result;
}

RouteResult Router::route(const HttpRequest &request) const
{
    RouteResult result;

    switch (normalize_uri(request.path, result.uri))
    {
        case NORMALIZE_BAD_REQUEST:
            result.status = ROUTE_BAD_REQUEST;
            return result;
        case NORMALIZE_FORBIDDEN:
            result.status = ROUTE_FORBIDDEN;
            return result;
        case NORMALIZE_OK:
            break;
    }

    result.location = find_location(result.uri);
    result.root = get_root(result.location);
    result.fs_path = build_path(result.uri, result.location);

    // "return" wins over everything else, exactly like nginx.
    if (result.location != NULL && result.location->return_code != 0)
    {
        result.status = ROUTE_REDIRECT;
        result.redirect_code = result.location->return_code;
        result.redirect_url = result.location->return_url;
        return result;
    }

    if (!is_method_allowed(result.location, request.method))
    {
        result.status = ROUTE_METHOD_NOT_ALLOWED;
        result.allow = allowed_methods_header(result.location);
        return result;
    }
    return result;
}
