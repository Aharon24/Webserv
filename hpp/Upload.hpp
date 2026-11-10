#ifndef UPLOAD_HPP
#define UPLOAD_HPP

#include <string>
#include <vector>

namespace Upload
{
    // On failure, returns false and sets errno. Existing files are preserved.
    bool save_file(const std::string &directory,
                   const std::string &filename,
                   const std::string &content);

    // Stores an uploaded filename and its binary content.
    struct UploadedFile
    {
        std::string filename;
        std::string content;
    };

    // Parses file parts without writing to disk.
    // On failure, sets error and leaves files unchanged.
    bool parse_multipart(const std::string &content_type,
                         const std::string &body,
                         std::vector<UploadedFile> &files,
                         std::string &error);
}

#endif