#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>

class config
{
private:
    std::string _path;

public:
    config();
    config& operator=(const config &ot);
    config(const config &ot);
    config(const std::string &path);
    ~config();

    void set_path(const std::string &path);
};


#endif