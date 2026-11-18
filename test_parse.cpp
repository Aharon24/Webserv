#include "hpp/HttpRequest.hpp"
#include <iostream>
#include <string>
#include <stdexcept>

void test_valid_request()
{
    std::cout << "\n===== TEST 1: Valid request =====" << std::endl;

    HttpParser parser;

    std::string raw_request =
        "POST /upload HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";

    try
    {
        HttpRequest request = parser.parse(raw_request);

        if (request.method == "POST" &&
            request.host == "localhost" &&
            request.body == "hello")
            std::cout << "PASS" << std::endl;
        else
            std::cout << "FAIL: Wrong parsed request" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cout << "FAIL: Unexpected error: "
                  << e.what() << std::endl;
    }
}

void test_duplicate_content_length()
{
    std::cout << "\n===== TEST 2: Duplicate Content-Length =====" << std::endl;

    HttpParser parser;

    std::string raw_request =
        "POST /upload HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 5\r\n"
        "Content-Length: 10\r\n"
        "\r\n"
        "hello";

    try
    {
        HttpRequest request = parser.parse(raw_request);
        (void)request;

        std::cout << "FAIL: Duplicate header accepted" << std::endl;
    }
    catch (const std::exception &e)
    {
        if (std::string(e.what()) ==
            "Duplicate HTTP header: content-length")
            std::cout << "PASS" << std::endl;
        else
            std::cout << "FAIL: Unexpected error: "
                      << e.what() << std::endl;
    }
}

void test_transfer_encoding_and_content_length()
{
    std::cout << "\n===== TEST 3: TE + Content-Length =====" << std::endl;

    HttpParser parser;

    std::string raw_request =
        "POST /upload HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Content-Length: 9\r\n"
        "\r\n"
        "4\r\n"
        "Wiki\r\n"
        "0\r\n"
        "\r\n";

    try
    {
        HttpRequest request = parser.parse(raw_request);
        (void)request;

        std::cout << "FAIL: Request was accepted" << std::endl;
    }
    catch (const std::exception &e)
    {
        if (std::string(e.what()) ==
            "Transfer-Encoding and Content-Length cannot be used together")
            std::cout << "PASS" << std::endl;
        else
            std::cout << "FAIL: Unexpected error: "
                      << e.what() << std::endl;
    }
}

int main()
{
    test_valid_request();
    test_duplicate_content_length();
    test_transfer_encoding_and_content_length();

    return 0;
}