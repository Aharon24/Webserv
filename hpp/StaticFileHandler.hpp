#ifndef STATICFILEHANDLER_HPP
#define STATICFILEHANDLER_HPP

#include <string>
#include "HttpResponse.hpp"

/*
** StaticFileHandler - Programmer 3
** Serves a file, a directory index, an autoindex listing or a 301 redirect
** for directories requested without a trailing slash.
**
** serve() returns the HTTP status code:
**   200 / 301 -> `out` is fully filled
**   >= 400    -> `out` is untouched; the caller builds the error response
**                (so custom error_page files are applied in one place).
*/
class StaticFileHandler
{
public:
    static int serve(const std::string &fs_path,
                     const std::string &uri,          // raw (still encoded) request path
                     const std::string &query,
                     const std::string &index_name,   // may be empty
                     bool autoindex,
                     HttpResponse &out);

    static bool read_file(const std::string &path, std::string &content);

private:
    StaticFileHandler();

    static int serve_file(const std::string &path, HttpResponse &out);
    static int serve_directory(const std::string &dir_path,
                               const std::string &uri,
                               const std::string &query,
                               const std::string &index_name,
                               bool autoindex,
                               HttpResponse &out);
    static int build_autoindex(const std::string &dir_path,
                               const std::string &uri,
                               HttpResponse &out);
};

#endif
