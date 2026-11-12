#ifndef ROUTER_HPP
#define ROUTER_HPP

#include <string>
#include "ServerConfig.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"

/*
** Router - Programmer 3
** One Router is created per request: Router router(conf);
**
**   prepare()      validates the URI, applies `return` redirects and
**                  allowed_methods. Returns true if `response` is final.
**   handle_get()   GET -> StaticFileHandler.
**   error_response() builds an error response, using conf.error_pages
**                  when the page exists, a default HTML page otherwise.
**   from_status_line() adapts the old "404 Not Found" + body handlers
**                  (POST / DELETE of Programmer 4) to HttpResponse.
*/
class Router
{
public:
    explicit Router(const ServerConfig &conf);

    const LocationConfig *find_location(const std::string &decoded_uri) const;
    std::string build_path(const std::string &decoded_uri,
                           const LocationConfig *location) const;

    bool prepare(const HttpRequest &request, HttpResponse &response) const;
    HttpResponse handle_get(const HttpRequest &request) const;

    HttpResponse error_response(int code) const;
    HttpResponse redirect_response(int code, const std::string &url) const;
    HttpResponse from_status_line(const std::string &status, const std::string &body) const;

    static bool url_decode(const std::string &in, std::string &out);
    static bool is_safe_path(const std::string &decoded_uri);

private:
    const ServerConfig &_conf;

    bool load_error_page(const std::string &uri, std::string &body) const;
};

#endif
