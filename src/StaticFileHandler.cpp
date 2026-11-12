#include "StaticFileHandler.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{
    int errno_to_status(int err)
    {
        if (err == ENOENT || err == ENOTDIR || err == ENAMETOOLONG)
            return 404;
        if (err == EACCES || err == EPERM)
            return 403;
        return 500;
    }

    std::string url_encode(const std::string &text)
    {
        static const char *hex = "0123456789ABCDEF";
        std::string out;

        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                out += static_cast<char>(c);
            else
            {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 0x0F];
            }
        }
        return out;
    }

    struct DirEntry
    {
        std::string name;
        bool is_dir;
        bool known;
        off_t size;
        time_t mtime;
    };

    bool entry_less(const DirEntry &a, const DirEntry &b)
    {
        if (a.is_dir != b.is_dir)
            return a.is_dir;
        return a.name < b.name;
    }
}

bool StaticFileHandler::read_file(const std::string &path, std::string &content)
{
    std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
    if (!file)
        return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();
    return true;
}

int StaticFileHandler::serve(const std::string &fs_path, const std::string &uri,
                             const std::string &query, const std::string &index_name,
                             bool autoindex, HttpResponse &out)
{
    struct stat info;

    if (stat(fs_path.c_str(), &info) != 0)
        return errno_to_status(errno);
    if (S_ISDIR(info.st_mode))
        return serve_directory(fs_path, uri, query, index_name, autoindex, out);
    if (!S_ISREG(info.st_mode))
        return 403;
    return serve_file(fs_path, out);
}

int StaticFileHandler::serve_file(const std::string &path, HttpResponse &out)
{
    std::string content;

    if (access(path.c_str(), R_OK) != 0)
        return 403;
    if (!read_file(path, content))
        return 500;
    out.set_status(200);
    out.set_body(content, HttpResponse::mime_type(path));
    return 200;
}

int StaticFileHandler::serve_directory(const std::string &dir_path, const std::string &uri,
                                       const std::string &query, const std::string &index_name,
                                       bool autoindex, HttpResponse &out)
{
    // "/images" -> 301 -> "/images/" (relative links inside the page need it).
    if (uri.empty() || uri[uri.size() - 1] != '/')
    {
        std::string target = uri + "/";
        if (!query.empty())
            target += "?" + query;
        out.set_status(301);
        out.set_header("Location", target);
        out.set_body("<html><body>301 Moved Permanently: <a href=\""
                     + HttpResponse::html_escape(target) + "\">"
                     + HttpResponse::html_escape(target) + "</a></body></html>\n",
                     "text/html; charset=UTF-8");
        return 301;
    }

    std::string dir = dir_path;
    if (dir.empty() || dir[dir.size() - 1] != '/')
        dir += '/';

    if (!index_name.empty())
    {
        const std::string candidate = dir + index_name;
        struct stat info;
        if (stat(candidate.c_str(), &info) == 0 && S_ISREG(info.st_mode))
            return serve_file(candidate, out);
    }
    if (autoindex)
        return build_autoindex(dir, uri, out);
    return 403;
}

int StaticFileHandler::build_autoindex(const std::string &dir_path, const std::string &uri,
                                       HttpResponse &out)
{
    DIR *dir = opendir(dir_path.c_str());
    if (dir == NULL)
        return errno_to_status(errno);

    std::vector<DirEntry> entries;
    struct dirent *item;
    while ((item = readdir(dir)) != NULL)
    {
        const std::string name = item->d_name;
        if (name == "." || name == "..")
            continue;

        DirEntry entry;
        struct stat info;
        entry.name = name;
        entry.is_dir = false;
        entry.known = false;
        entry.size = 0;
        entry.mtime = 0;
        if (stat((dir_path + name).c_str(), &info) == 0)
        {
            entry.known = true;
            entry.is_dir = S_ISDIR(info.st_mode);
            entry.size = info.st_size;
            entry.mtime = info.st_mtime;
        }
        entries.push_back(entry);
    }
    closedir(dir);
    std::sort(entries.begin(), entries.end(), entry_less);

    const std::string title = "Index of " + HttpResponse::html_escape(uri);
    std::ostringstream html;
    html << "<!DOCTYPE html>\n<html><head><meta charset=\"UTF-8\"><title>" << title
         << "</title></head>\n<body><h1>" << title << "</h1><hr>\n<table>\n";
    if (uri != "/")
        html << "<tr><td><a href=\"../\">../</a></td><td></td><td></td></tr>\n";

    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const DirEntry &entry = entries[i];
        char date[32];
        date[0] = '\0';
        if (entry.known)
        {
            std::tm *tm_info = std::localtime(&entry.mtime);
            if (tm_info != NULL)
                std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M", tm_info);
        }
        const std::string suffix = entry.is_dir ? "/" : "";
        html << "<tr><td><a href=\"" << url_encode(entry.name) << suffix << "\">"
             << HttpResponse::html_escape(entry.name) << suffix << "</a></td><td>"
             << date << "</td><td>";
        if (entry.known && !entry.is_dir)
            html << entry.size;
        else
            html << "-";
        html << "</td></tr>\n";
    }
    html << "</table><hr></body></html>\n";

    out.set_status(200);
    out.set_body(html.str(), "text/html; charset=UTF-8");
    return 200;
}
