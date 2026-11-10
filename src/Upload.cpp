
/*
** ============================================================
** UPLOAD.CPP - TODO
** 42 Webserv
** ============================================================
**
** PURPOSE:
** Handle uploaded file data and save it to the configured
** upload directory.
**
** This module must not handle client sockets or HTTP responses.
** It receives already collected request body data.
**
** TODO 1: Validate the upload directory
** [x] Check that the directory path is not empty.
** [x] Check that the destination directory exists.
** [x] Handle inaccessible directories.
**
** TODO 2: Validate the filename
** [x] Reject empty filenames.
** [x] Reject "." and "..".
** [x] Reject path separators ('/' and '\\').
** [x] Prevent path traversal.
**
** TODO 3: Build the destination path
** [x] Combine upload_path and filename.
** [x] Handle a trailing '/' correctly.
**
** TODO 4: Create the destination file
** [x] Use open() with appropriate flags.
** [x] Avoid overwriting existing files.
** [x] Handle open() errors.
**
** TODO 5: Write file content
** [x] Write binary data without modifying it.
** [x] Handle partial write() operations.
** [x] Handle EINTR correctly.
** [x] Detect write() failures.
**
** TODO 6: Close and clean up
** [x] Close the file descriptor.
** [x] Remove incomplete files after a failed write.
** [x] Return the operation result.
**
** TODO 7: Parse multipart/form-data
** [x] Extract the boundary from Content-Type.
** [x] Identify individual multipart sections.
** [x] Extract the uploaded filename.
** [x] Extract file content without multipart headers.
** [x] Preserve binary data.
** [x] Reject malformed multipart data.
**
** TODO 8: Integration (LATER)
** [ ] Receive body and Content-Type from POST handling.
** [ ] Use LocationConfig::upload_path.
** [ ] Check upload_enabled before allowing uploads.
** [ ] Convert upload results to HTTP status codes.
**
** TODO 9: Tests
** [ ] Upload a normal text file.
** [ ] Upload a binary file containing '\0'.
** [ ] Test an empty file.
** [ ] Test an unsafe filename.
** [ ] Test an existing filename.
** [ ] Test an invalid upload directory.
** [ ] Test malformed multipart data.
** [ ] Verify that failed uploads leave no partial files.
** ============================================================
*/

#include "Upload.hpp"

#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <map>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
    // Removes optional spaces and tabs around a header component.
    std::string trim_header_space(const std::string &value)
    {
        const std::string::size_type first = value.find_first_not_of(" \t");
        if (first == std::string::npos)
            return "";
        return value.substr(first, value.find_last_not_of(" \t") - first + 1);
    }

    // Header names and media types use case-insensitive ASCII comparisons.
    std::string lower_header_text(std::string value)
    {
        for (std::string::size_type i = 0; i < value.size(); ++i)
            if (value[i] >= 'A' && value[i] <= 'Z')
                value[i] = static_cast<char>(value[i] + ('a' - 'A'));
        return value;
    }

    // Accepts an HTTP token character for an unquoted parameter or its name.
    bool header_token_char(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9')
            || std::string("!#$%&'*+-.^_`|~").find(c) != std::string::npos;
    }

    // Reads one token or quoted value, including quoted backslash escapes.
    bool read_parameter_value(const std::string &text,
                              std::string::size_type &pos,
                              std::string &value)
    {
        value.clear();
        if (pos == text.size())
            return false;
        if (text[pos] != '"')
        {
            while (pos < text.size() && header_token_char(text[pos]))
                value += text[pos++];
            return !value.empty();
        }
        ++pos;
        while (pos < text.size())
        {
            char c = text[pos++];
            if (c == '"')
                return true;
            if (c == '\\')
            {
                if (pos == text.size())
                    return false;
                c = text[pos++];
            }
            value += c;
        }
        return false;
    }

    // MIME boundaries contain 1-70 allowed ASCII characters, no trailing space.
    bool valid_boundary(const std::string &boundary)
    {
        if (boundary.empty() || boundary.size() > 70
            || boundary[boundary.size() - 1] == ' ')
            return false;
        for (std::string::size_type i = 0; i < boundary.size(); ++i)
        {
            const char c = boundary[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9')
                || std::string("'()+_,-./:=? ").find(c) != std::string::npos))
                return false;
        }
        return true;
    }

    // Extracts a unique boundary; failure leaves boundary unchanged and sets error.
    // Inline allows standalone compilation before parse_multipart uses this helper.
    inline bool extract_boundary(const std::string &content_type,
                                 std::string &boundary,
                                 std::string &error)
    {
        error = "Invalid multipart Content-Type";
        // Reject controls before parsing, including embedded NUL and header injection.
        for (std::string::size_type i = 0; i < content_type.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(content_type[i]);
            if ((c < 32 && c != '\t') || c == 127)
                return false;
        }
        std::string::size_type pos = content_type.find(';');
        if (lower_header_text(trim_header_space(content_type.substr(0, pos)))
            != "multipart/form-data")
            return false;
        bool found = false;
        std::string result;
        while (pos != std::string::npos && pos < content_type.size())
        {
            ++pos;
            pos = content_type.find_first_not_of(" \t", pos);
            if (pos == std::string::npos)
                return false;
            const std::string::size_type start = pos;
            while (pos < content_type.size() && header_token_char(content_type[pos]))
                ++pos;
            const std::string name = lower_header_text(content_type.substr(start, pos - start));
            pos = content_type.find_first_not_of(" \t", pos);
            if (name.empty() || pos == std::string::npos || content_type[pos] != '=')
                return false;
            pos = content_type.find_first_not_of(" \t", pos + 1);
            if (pos == std::string::npos)
                return false;
            std::string value;
            if (!read_parameter_value(content_type, pos, value))
                return false;
            if (name == "boundary")
            {
                if (found || !valid_boundary(value))
                {
                    error = "Invalid or duplicate multipart boundary";
                    return false;
                }
                found = true;
                result = value;
            }
            pos = content_type.find_first_not_of(" \t", pos);
            if (pos != std::string::npos && content_type[pos] != ';')
                return false;
        }
        if (!found)
        {
            error = "Missing multipart boundary";
            return false;
        }
        boundary = result;
        error.clear();
        return true;
    }

    // Checks a complete delimiter line; next points past its CRLF or final EOF.
    bool read_boundary_line(const std::string &body,
                            const std::string &marker,
                            std::string::size_type pos,
                            std::string::size_type &next,
                            bool &closing)
    {
        if (body.compare(pos, marker.size(), marker) != 0)
            return false;
        pos += marker.size();
        closing = body.compare(pos, 2, "--") == 0;
        if (closing)
            pos += 2;
        // MIME delimiter lines may have optional trailing spaces or tabs.
        while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t'))
            ++pos;
        if (closing && pos == body.size())
        {
            next = pos;
            return true;
        }
        if (body.compare(pos, 2, "\r\n") != 0)
            return false;
        next = pos + 2;
        return true;
    }

    // Finds a full delimiter at a line start, skipping boundary-like file bytes.
    bool find_boundary_line(const std::string &body,
                            const std::string &marker,
                            std::string::size_type start,
                            std::string::size_type &pos,
                            std::string::size_type &next,
                            bool &closing)
    {
        pos = body.find(marker, start);
        while (pos != std::string::npos)
        {
            if ((pos == 0 || (pos >= 2 && body.compare(pos - 2, 2, "\r\n") == 0))
                && read_boundary_line(body, marker, pos, next, closing))
                return true;
            pos = body.find(marker, pos + 1);
        }
        return false;
    }

    // Splits raw parts, preserving headers and binary bytes; no files are written.
    // Preamble and epilogue are ignored. At least one part and a closing line are required.
    // On failure, parts stays unchanged. Inline permits use before public integration.
    inline bool split_multipart_parts(const std::string &body,
                                      const std::string &boundary,
                                      std::vector<std::string> &parts,
                                      std::string &error)
    {
        if (!valid_boundary(boundary))
        {
            error = "Invalid multipart boundary";
            return false;
        }
        const std::string marker = "--" + boundary;
        std::string::size_type pos;
        std::string::size_type next;
        bool closing;
        if (!find_boundary_line(body, marker, 0, pos, next, closing) || closing)
        {
            error = "Missing multipart opening boundary";
            return false;
        }
        std::vector<std::string> result;
        while (!closing)
        {
            const std::string::size_type part_start = next;
            if (!find_boundary_line(body, marker, part_start, pos, next, closing))
            {
                error = "Missing multipart closing boundary";
                return false;
            }
            // Only the CRLF immediately before a delimiter belongs to that delimiter.
            if (pos < 2 || pos - 2 < part_start)
            {
                error = "Missing CRLF before multipart boundary";
                return false;
            }
            result.push_back(body.substr(part_start, pos - 2 - part_start));
        }
        parts.swap(result);
        error.clear();
        return true;
    }

	// Rejects empty names, "." and "..", and any path separators or NUL bytes.
    bool valid_filename(const std::string &filename)
    {
        return !filename.empty()
            && filename != "." && filename != ".."
            && filename.find('/') == std::string::npos
            && filename.find('\\') == std::string::npos
            && filename.find('\0') == std::string::npos;
    }

    // Validates one unfolded part header; control bytes and invalid names are rejected.
    bool parse_part_header(const std::string &line,
                           std::string &name, std::string &value)
    {
        const std::string::size_type colon = line.find(':');
        if (colon == std::string::npos || colon == 0)
            return false;
        for (std::string::size_type i = 0; i < colon; ++i)
            if (!header_token_char(line[i]))
                return false;
        for (std::string::size_type i = colon + 1; i < line.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(line[i]);
            if ((c < 32 && c != '\t') || c == 127)
                return false;
        }
        name = lower_header_text(line.substr(0, colon));
        value = trim_header_space(line.substr(colon + 1));
        return true;
    }

    // Reads unique form-data parameters, reusing the quoted-value parser.
    bool parse_disposition(const std::string &text,
                           std::string &filename, bool &is_file)
    {
        std::string::size_type pos = text.find(';');
        if (lower_header_text(trim_header_space(text.substr(0, pos))) != "form-data")
            return false;
        std::map<std::string, std::string> parameters;
        while (pos != std::string::npos && pos < text.size())
        {
            pos = text.find_first_not_of(" \t", pos + 1);
            if (pos == std::string::npos)
                return false;
            const std::string::size_type start = pos;
            while (pos < text.size() && header_token_char(text[pos]))
                ++pos;
            const std::string name = lower_header_text(text.substr(start, pos - start));
            pos = text.find_first_not_of(" \t", pos);
            if (name.empty() || pos == std::string::npos || text[pos] != '=')
                return false;
            pos = text.find_first_not_of(" \t", pos + 1);
            if (pos == std::string::npos)
                return false;
            std::string value;
            if (!read_parameter_value(text, pos, value)
                || parameters.find(name) != parameters.end())
                return false;
            parameters[name] = value;
            pos = text.find_first_not_of(" \t", pos);
            if (pos != std::string::npos && text[pos] != ';')
                return false;
        }
        // Extended filename encoding is unsupported; do not silently misname a file.
        if (parameters.find("name") == parameters.end() || parameters["name"].empty()
            || parameters.find("filename*") != parameters.end())
            return false;
        is_file = parameters.find("filename") != parameters.end();
        if (is_file)
        {
            filename = parameters["filename"];
            return valid_filename(filename);
        }
        return true;
    }

    // Requires one Content-Disposition; duplicate headers and transfer encodings fail.
    bool read_part_headers(const std::string &headers,
                           std::string &filename, bool &is_file)
    {
        std::map<std::string, std::string> values;
        std::string::size_type pos = 0;
        while (pos < headers.size())
        {
            const std::string::size_type end = headers.find("\r\n", pos);
            const std::string line = headers.substr(pos,
                end == std::string::npos ? std::string::npos : end - pos);
            std::string name;
            std::string value;
            if (!parse_part_header(line, name, value)
                || values.find(name) != values.end())
                return false;
            values[name] = value;
            if (end == std::string::npos)
                break;
            pos = end + 2;
        }
        // Only raw part bytes are supported; no base64 or nested multipart decoding.
        if (values.find("content-disposition") == values.end()
            || values.find("content-transfer-encoding") != values.end())
            return false;
        if (values.find("content-type") != values.end()
            && lower_header_text(values["content-type"]).compare(0, 10, "multipart/") == 0)
            return false;
        return parse_disposition(values["content-disposition"], filename, is_file);
    }

    // Separates headers from bytes; normal form fields are validated but not returned.
    bool parse_file_part(const std::string &part, Upload::UploadedFile &file,
                         bool &is_file, std::string &error)
    {
        const std::string::size_type header_end = part.find("\r\n\r\n");
        if (header_end == std::string::npos)
        {
            error = "Missing multipart header separator";
            return false;
        }
        if (!read_part_headers(part.substr(0, header_end), file.filename, is_file))
        {
            error = "Invalid multipart headers or filename";
            return false;
        }
        // substr uses a byte count, so NUL bytes and trailing CRLF remain intact.
        if (is_file)
            file.content = part.substr(header_end + 4);
        return true;
    }

	// Checks that the directory exists, is a directory, and is writable and searchable.
    bool valid_directory(const std::string &directory)
    {
        if (directory.empty() || directory.find('\0') != std::string::npos)
        {
            errno = EINVAL;
            return false;
        }
        struct stat info;
        if (stat(directory.c_str(), &info) == -1)
            return false;
        if (!S_ISDIR(info.st_mode))
        {
            errno = ENOTDIR;
            return false;
        }
        return access(directory.c_str(), W_OK | X_OK) == 0;
    }

	// Writes the entire content to the file descriptor, handling partial writes and EINTR.
    bool write_content(int fd, const std::string &content)
    {
        std::string::size_type offset = 0;
        const std::size_t max_write = static_cast<std::size_t>(
            std::numeric_limits<ssize_t>::max());

        while (offset < content.size())
        {
            std::size_t count = content.size() - offset;
            if (count > max_write)
                count = max_write;
            ssize_t written = write(fd, content.c_str() + offset, count);
            if (written < 0)
            {
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (written == 0)
            {
                errno = EIO;
                return false;
            }
            offset += static_cast<std::string::size_type>(written);
        }
        return true;
    }
}

// Saves a file to the specified directory, rejecting unsafe filenames and existing files.
// On failure, returns false and sets errno. Existing files are preserved.
bool Upload::save_file(const std::string &directory,
                       const std::string &filename,
                       const std::string &content)
{
    if (!valid_filename(filename))
    {
        errno = EINVAL;
        return false;
    }
    if (!valid_directory(directory))
        return false;

    std::string path = directory;
    if (path[path.size() - 1] != '/')
        path += '/';
    path += filename;

    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd == -1)
        return false;
    if (!write_content(fd, content))
    {
        const int error = errno;
        close(fd);
        unlink(path.c_str());
        errno = error;
        return false;
    }
    if (close(fd) == -1)
    {
        const int error = errno;
        unlink(path.c_str());
        errno = error;
        return false;
    }
    return true;
}


// Parses the complete collected body; it performs no filesystem or socket operations.
// Results replace files only after every part passes validation, including form fields.
bool Upload::parse_multipart(const std::string &content_type,
                             const std::string &body,
                             std::vector<UploadedFile> &files,
                             std::string &error)
{
    std::string boundary;
    std::vector<std::string> parts;
    if (!extract_boundary(content_type, boundary, error)
        || !split_multipart_parts(body, boundary, parts, error))
        return false;
    std::vector<UploadedFile> result;
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        UploadedFile file;
        bool is_file = false;
        if (!parse_file_part(parts[i], file, is_file, error))
            return false;
        if (is_file)
            result.push_back(file);
    }
    files.swap(result);
    error.clear();
    return true;
}
