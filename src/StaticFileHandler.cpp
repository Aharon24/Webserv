#include "StaticFileHandler.hpp"

#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{
    struct DirEntry
    {
        std::string name;
        bool is_dir;
        off_t size;
        time_t mtime;
    };

    // Directories first, then case-sensitive alphabetical order.
    bool entry_less(const DirEntry &a, const DirEntry &b)
    {
        if (a.is_dir != b.is_dir)
            return a.is_dir;
        return a.name < b.name;
    }

    std::string size_to_string(off_t size)
    {
        std::ostringstream ss;

        ss << static_cast<unsigned long long>(size);
        return ss.str();
    }
}

StaticFileHandler::StaticFileHandler(const ServerConfig &conf, const Router &router)
    : _conf(conf), _router(router)
{
}

StaticFileHandler::StaticFileHandler(const StaticFileHandler &other)
    : _conf(other._conf), _router(other._router)
{
}

StaticFileHandler::~StaticFileHandler()
{
}

int StaticFileHandler::status_from_errno(int error_number)
{
    switch (error_number)
    {
        case ENOENT:
        case ENOTDIR:
        case ENAMETOOLONG:
            return 404;
        case EACCES:
        case EPERM:
            return 403;
        default:
            return 500;
    }
}

bool StaticFileHandler::read_file(const std::string &path, std::string &content)
{
    std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);

    if (!file)
        return false;

    std::ostringstream buffer;

    buffer << file.rdbuf();
    if (file.bad())
        return false;
    content = buffer.str();
    return true;
}

HttpResponse StaticFileHandler::make_redirect(int status, const std::string &location) const
{
    return HttpResponse::redirect(status, location);
}

/*
** The configured error_page is a URI ("/errors/404.html"). It is routed like
** a normal request (location + root) and read from disk. If anything goes
** wrong - not configured, file missing, unreadable - the built-in page is used,
** so building an error can never fail or loop.
*/
HttpResponse StaticFileHandler::make_error(int status) const
{
    std::map<int, std::string>::const_iterator it = _conf.error_pages.find(status);

    if (it != _conf.error_pages.end())
    {
        std::string uri;

        if (Router::normalize_uri(it->second, uri) == NORMALIZE_OK)
        {
            const LocationConfig *location = _router.find_location(uri);
            const std::string path = _router.build_path(uri, location);
            struct stat info;
            std::string content;

            if (stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode)
                && read_file(path, content))
            {
                HttpResponse response(status);

                response.set_body(content, HttpResponse::mime_type(path));
                return response;
            }
        }
    }
    return HttpResponse::default_error(status);
}

HttpResponse StaticFileHandler::serve_file(const std::string &path) const
{
    struct stat info;

    if (stat(path.c_str(), &info) != 0)
        return make_error(status_from_errno(errno));
    if (!S_ISREG(info.st_mode))
        return make_error(403);
    if (access(path.c_str(), R_OK) != 0)
        return make_error(403);

    std::string content;

    if (!read_file(path, content))
        return make_error(500);

    HttpResponse response(200);

    response.set_body(content, HttpResponse::mime_type(path));
    response.set_header("Last-Modified", HttpResponse::http_date(info.st_mtime));
    return response;
}

HttpResponse StaticFileHandler::build_autoindex(const std::string &dir_path, const std::string &uri) const
{
    DIR *dir = opendir(dir_path.c_str());

    if (dir == NULL)
        return make_error(status_from_errno(errno));

    std::vector<DirEntry> entries;
    std::string base = dir_path;

    if (base.empty() || base[base.size() - 1] != '/')
        base += '/';

    struct dirent *item;
    while ((item = readdir(dir)) != NULL)
    {
        const std::string name = item->d_name;
        struct stat info;
        DirEntry entry;

        if (name == "." || name == "..")
            continue;
        if (stat((base + name).c_str(), &info) != 0)
            continue;               // broken symlink or file removed meanwhile
        entry.name = name;
        entry.is_dir = S_ISDIR(info.st_mode);
        entry.size = info.st_size;
        entry.mtime = info.st_mtime;
        entries.push_back(entry);
    }
    closedir(dir);
    std::sort(entries.begin(), entries.end(), entry_less);

    std::ostringstream html;
    const std::string title = "Index of " + HttpResponse::html_escape(uri);

    html << "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n<title>"
         << title << "</title>\n</head>\n<body>\n<h1>" << title << "</h1>\n<hr>\n<pre>\n";
    if (uri != "/")
        html << "<a href=\"../\">../</a>\n";
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const std::string shown = entries[i].name + (entries[i].is_dir ? "/" : "");
        std::string line = "<a href=\"" + Router::encode_uri(shown) + "\">"
            + HttpResponse::html_escape(shown) + "</a>";
        std::string date = HttpResponse::http_date(entries[i].mtime);

        // Simple column alignment: pad the visible name to 40 characters.
        if (shown.size() < 40)
            line += std::string(40 - shown.size(), ' ');
        else
            line += ' ';
        html << line << date << "  "
             << (entries[i].is_dir ? std::string("-") : size_to_string(entries[i].size))
             << "\n";
    }
    html << "</pre>\n<hr>\n</body>\n</html>\n";

    HttpResponse response(200);

    response.set_body(html.str(), "text/html; charset=UTF-8");
    return response;
}

/*
** Directory handling order:
**   1. no trailing slash        -> 301 to the same URI with a slash
**   2. index file exists        -> serve it
**   3. autoindex on             -> generated listing
**   4. otherwise                -> 403
*/
HttpResponse StaticFileHandler::serve_directory(const HttpRequest &request, const RouteResult &route) const
{
    if (route.uri[route.uri.size() - 1] != '/')
    {
        std::string location = Router::encode_uri(route.uri) + "/";

        if (!request.query_string.empty())
            location += "?" + request.query_string;
        return make_redirect(301, location);
    }

    const std::string index = _router.get_index(route.location);

    if (!index.empty())
    {
        std::string index_path = route.fs_path;
        struct stat info;

        if (index_path[index_path.size() - 1] != '/')
            index_path += '/';
        index_path += index;
        const int result = stat(index_path.c_str(), &info);
        const int error_number = errno;

        if (result == 0 && S_ISREG(info.st_mode))
            return serve_file(index_path);
        if (result != 0 && error_number == EACCES)
            return make_error(403);
    }

    if (route.location != NULL && route.location->autoindex)
        return build_autoindex(route.fs_path, route.uri);
    return make_error(404);
}

HttpResponse StaticFileHandler::handle_get(const HttpRequest &request, const RouteResult &route) const
{
    struct stat info;

    if (stat(route.fs_path.c_str(), &info) != 0)
        return make_error(status_from_errno(errno));
    if (S_ISDIR(info.st_mode))
        return serve_directory(request, route);
    return serve_file(route.fs_path);
}
