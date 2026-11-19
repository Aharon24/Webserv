// Link with -Wl,--wrap=write,--wrap=close,--wrap=remove for fault injection.
// Usage: upload_io_test PRIVATE_TEST_DIRECTORY MODE (partial, error, zero, close, cleanup).
#include "Upload.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <unistd.h>

extern "C" ssize_t __real_write(int fd, const void *buffer, size_t count);
extern "C" int __real_close(int fd);
extern "C" int __real_remove(const char *path);

static std::string mode;
static int writes = 0;
static bool close_failed = false;

extern "C" ssize_t __wrap_write(int fd, const void *buffer, size_t count)
{
    ++writes;
    if (mode == "zero")
        return 0;
    if ((mode == "error" || mode == "cleanup") && writes > 1)
    {
        // The upload must not classify a write failure as a permission error.
        errno = EACCES;
        return -1;
    }
    if (mode != "close" && count > 2)
        count = 2;
    return __real_write(fd, buffer, count);
}

extern "C" int __wrap_close(int fd)
{
    const int result = __real_close(fd);
    if (mode == "close" && !close_failed)
    {
        close_failed = true;
        errno = EIO;
        return -1;
    }
    return result;
}

extern "C" int __wrap_remove(const char *path)
{
    if (mode == "cleanup")
    {
        errno = EACCES;
        return -1;
    }
    return __real_remove(path);
}

int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    mode = argv[2];
    const std::string data("a\0bcdef", 7);
    const bool saved = Upload::save_file(argv[1], "case.bin", data);
    const int error = errno;
    const std::string path = std::string(argv[1]) + "/case.bin";
    std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
    const bool exists = file.is_open();
    bool ok;
    if (mode == "partial")
    {
        std::ostringstream bytes;
        bytes << file.rdbuf();
        ok = saved && exists && bytes.str() == data && writes > 1;
    }
    else if (mode == "cleanup")
        ok = !saved && error == EIO && exists;
    else
        ok = !saved && error == EIO && !exists;
    std::cout << (ok ? "PASS " : "FAIL ") << mode << " saved=" << saved
              << " exists=" << exists << " writes=" << writes << std::endl;
    return ok ? 0 : 1;
}
