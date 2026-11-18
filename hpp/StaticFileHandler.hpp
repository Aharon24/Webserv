#ifndef STATIC_FILE_HANDLER_HPP
#define STATIC_FILE_HANDLER_HPP

#include <string>

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "Router.hpp"
#include "ServerConfig.hpp"

/*
** StaticFileHandler
**
** Answers GET requests from the filesystem: regular files, directory
** index files, autoindex listings and directory redirects. It also builds
** every error response of the server (configured error_page or built-in page).
*/
class StaticFileHandler
{
private:
    const ServerConfig &_conf;
    const Router &_router;

    StaticFileHandler &operator=(const StaticFileHandler &other);

    HttpResponse serve_file(const std::string &path) const;
    HttpResponse serve_directory(const HttpRequest &request, const RouteResult &route) const;
    HttpResponse build_autoindex(const std::string &dir_path, const std::string &uri) const;

    // Maps errno of a failed stat()/open() to 404 / 403 / 500.
    static int status_from_errno(int error_number);
    static bool read_file(const std::string &path, std::string &content);

public:
    StaticFileHandler(const ServerConfig &conf, const Router &router);
    StaticFileHandler(const StaticFileHandler &other);
    ~StaticFileHandler();

    // GET entry point. route must be ROUTE_OK.
    HttpResponse handle_get(const HttpRequest &request, const RouteResult &route) const;

    // Error response for any 4xx/5xx status, using conf.error_pages when possible.
    HttpResponse make_error(int status) const;
    HttpResponse make_redirect(int status, const std::string &location) const;
};

#endif
