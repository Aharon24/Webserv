#include "Upload.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#define RESET "\033[0m"
#define GREEN "\033[32m"
#define RED "\033[31m"
#define CYAN "\033[36m"
#define YELLOW "\033[33m"
#define BOLD "\033[1m"

struct TestCase
{
    const char *name;
    bool (*run)(const std::string &);
};

static bool read_file(const std::string &path, std::string &content)
{
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    if (!input)
        return false;
    content.assign(std::istreambuf_iterator<char>(input),
                   std::istreambuf_iterator<char>());
    return !input.bad();
}

static bool test_text(const std::string &dir)
{
    const std::string expected = "Hello Webserv!";
    if (!Upload::save_file(dir, "hello.txt", expected))
        return false;
    std::string actual;
    bool ok = read_file(dir + "/hello.txt", actual) && actual == expected;
    unlink((dir + "/hello.txt").c_str());
    return ok;
}

static bool test_existing(const std::string &dir)
{
    const std::string path = dir + "/existing.txt";
    if (!Upload::save_file(dir, "existing.txt", "original"))
        return false;
    errno = 0;
    bool rejected = !Upload::save_file(dir, "existing.txt", "changed")
                    && errno == EEXIST;
    std::string content;
    bool ok = rejected && read_file(path, content) && content == "original";
    unlink(path.c_str());
    return ok;
}

static bool test_empty_name(const std::string &dir)
{
    errno = 0;
    return !Upload::save_file(dir, "", "data") && errno == EINVAL;
}

static bool test_traversal(const std::string &dir)
{
    errno = 0;
    return !Upload::save_file(dir, "../escape.txt", "data")
        && errno == EINVAL;
}

static bool test_binary(const std::string &dir)
{
    const std::string expected("ABC\0DEF", 7);
    const std::string path = dir + "/binary.bin";
    if (!Upload::save_file(dir, "binary.bin", expected))
        return false;
    std::string actual;
    bool ok = read_file(path, actual) && actual == expected;
    unlink(path.c_str());
    return ok;
}

static bool test_missing_directory(const std::string &dir)
{
    return !Upload::save_file(dir + "/missing_dir", "file.txt", "data");
}

static bool test_empty_content(const std::string &dir)
{
    const std::string path = dir + "/empty.bin";
    if (!Upload::save_file(dir, "empty.bin", ""))
        return false;
    struct stat info;
    bool ok = stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode)
              && info.st_size == 0;
    unlink(path.c_str());
    return ok;
}

static const TestCase TESTS[] = {
    {"Сохранение текстового файла", test_text},
    {"Запрет перезаписи (EEXIST)", test_existing},
    {"Пустое имя файла (EINVAL)", test_empty_name},
    {"Защита от ../ (EINVAL)", test_traversal},
    {"Бинарные данные с \\0", test_binary},
    {"Несуществующий каталог", test_missing_directory},
    {"Пустой файл (0 байт)", test_empty_content}
};

static const int TEST_COUNT = sizeof(TESTS) / sizeof(TESTS[0]);

static bool run_one(int index, const std::string &dir)
{
    bool ok = TESTS[index].run(dir);
    std::cout << (ok ? GREEN "  [PASS] " : RED "  [FAIL] ")
              << RESET << TESTS[index].name << std::endl;
    return ok;
}

static void run_all(const std::string &dir)
{
    int passed = 0;
    for (int i = 0; i < TEST_COUNT; ++i)
        if (run_one(i, dir))
            ++passed;
    std::cout << "\n" << BOLD << "Итог: " << passed << "/"
              << TEST_COUNT << " тестов" << RESET << std::endl;
}

int main()
{
    char temp[] = "/tmp/webserv-upload-XXXXXX";
    char *created = mkdtemp(temp);
    if (!created)
    {
        std::cerr << RED << "Не удалось создать временную папку: "
                  << std::strerror(errno) << RESET << std::endl;
        return 1;
    }
    const std::string directory(created);
    std::string command;
    while (true)
    {
        std::cout << "\n" << CYAN << BOLD
                  << "========== 42 WEBSERV | UPLOAD TESTS =========="
                  << RESET << "\n";
        std::cout << "Временная папка: " << directory << "\n\n";
        for (int i = 0; i < TEST_COUNT; ++i)
            std::cout << "  " << (i + 1) << ") " << TESTS[i].name << "\n";
        std::cout << YELLOW << "  8) Запустить все тесты\n"
                  << "  0) Выход\n" << RESET;
        std::cout << "\nВыбор: ";
        if (!std::getline(std::cin, command))
            break;
        if (command == "0")
            break;
        if (command == "8")
            run_all(directory);
        else if (command.size() == 1 && command[0] >= '1'
                 && command[0] <= '7')
            run_one(command[0] - '1', directory);
        else
            std::cout << RED << "Неизвестный пункт меню" << RESET << "\n";
    }
    if (rmdir(directory.c_str()) == -1)
        std::cerr << YELLOW << "Папка оставлена для проверки: "
                  << directory << RESET << std::endl;
    return 0;
}
