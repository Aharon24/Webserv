#ifndef HTTP_RESPONSE_HPP
#define HTTP_RESPONSE_HPP

#include <ctime>
#include <map>
#include <string>

/*
** HttpResponse
**
** Owns everything about the bytes that go back to the client:
** status line, headers (Content-Type, Content-Length, Date, ...) and body.
** Other modules (Router, StaticFileHandler, Server, future CGI/Upload code)
** fill an HttpResponse and call build() / build_head() to serialize it.
*/
class HttpResponse
{
private:
    int _status;
    std::string _reason;
    std::map<std::string, std::string> _headers;
    std::string _body;

    std::string build_head_internal(const std::string &version,std::size_t body_size,bool include_content_length) const;

public:
    HttpResponse();
    explicit HttpResponse(int status);

    std::string build_head_streaming(const std::string &version) const;

    // Status
    void set_status(int status);
    void set_status(int status, const std::string &reason);
    int get_status() const;
    const std::string &get_reason() const;

    // Headers / body
    void set_header(const std::string &name, const std::string &value);
    bool has_header(const std::string &name) const;
    std::string get_header(const std::string &name) const;
    void set_body(const std::string &body, const std::string &content_type);
    const std::string &get_body() const;

    // Serialization.
    // build_head(): status line + headers + empty line (no body).
    // build():      build_head() + body (body is omitted for 1xx/204/304).

    std::string build_head(const std::string &version) const;
    std::string build_head(const std::string &version,std::size_t body_size) const;
    std::string build(const std::string &version) const;

    // Static helpers
    static std::string reason_phrase(int status);
    static std::string mime_type(const std::string &path);
    static std::string http_date(time_t when);
    static std::string html_escape(const std::string &text);
    static bool status_has_no_body(int status);

    // Ready-made responses
    static HttpResponse redirect(int status, const std::string &location);
    static HttpResponse default_error(int status);

    // Adapter for handlers that still return a "404 Not Found" style
    // status string and a raw body (POST / DELETE code of other modules).
    static HttpResponse from_status_line(const std::string &status_line, const std::string &body);
};

#endif
