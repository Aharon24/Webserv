#ifndef ROUTER_HPP
#define ROUTER_HPP

#include <string>

#include "HttpRequest.hpp"
#include "ServerConfig.hpp"

enum RouteStatus
{
    ROUTE_OK,                   // request may be handled by a method handler
    ROUTE_REDIRECT,             // location has "return": send redirect_code/redirect_url
    ROUTE_BAD_REQUEST,          // malformed percent-encoding or NUL byte in the URI
    ROUTE_FORBIDDEN,            // URI tries to climb above the root ("/../")
    ROUTE_METHOD_NOT_ALLOWED    // method is not in the location's allowed_methods
};

enum NormalizeStatus
{
    NORMALIZE_OK,
    NORMALIZE_BAD_REQUEST,
    NORMALIZE_FORBIDDEN
};

// Everything a method handler needs to know about where a request points.
struct RouteResult
{
    RouteStatus status;
    const LocationConfig *location;     // matched location, NULL if none matched
    std::string uri;                    // decoded + normalized path, always starts with '/'
    std::string root;                   // effective root directory
    std::string fs_path;                // filesystem path built from root + uri
    std::string allow;                  // "GET, POST" (filled for 405)
    int redirect_code;                  // filled for ROUTE_REDIRECT
    std::string redirect_url;

    RouteResult()
        : status(ROUTE_OK), location(NULL), redirect_code(0)
    {
    }
};

/*
** Router
**
** Decides which location block a request belongs to and where on the disk
** the requested resource lives. It never touches the disk itself.
*/
class Router
{
private:
    const ServerConfig &_conf;

    Router &operator=(const Router &other);

public:
    explicit Router(const ServerConfig &conf);
    Router(const Router &other);
    ~Router();

    // Full routing decision for one request.
    RouteResult route(const HttpRequest &request) const;

    // Longest-prefix match on whole path segments ("/img" never matches "/imgx").
    const LocationConfig *find_location(const std::string &uri) const;

    // Effective settings of a location, falling back to the server block.
    std::string get_root(const LocationConfig *location) const;
    std::string get_index(const LocationConfig *location) const;

    // root + (uri without the location prefix)
    std::string build_path(const std::string &uri, const LocationConfig *location) const;

    // Empty allowed_methods means "everything we implement" (GET, POST, DELETE).
    bool is_method_allowed(const LocationConfig *location, const std::string &method) const;
    std::string allowed_methods_header(const LocationConfig *location) const;

    // Percent-decode once, drop "." / empty segments, resolve "..", keep a trailing '/'.
    static NormalizeStatus normalize_uri(const std::string &raw, std::string &normalized);

    // Percent-encode everything except unreserved characters and '/'.
    static std::string encode_uri(const std::string &path);
};

#endif
