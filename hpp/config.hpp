#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>

class config
{
private:

public:
    config();
    config& operator=(const config &ot);
    config(const config &ot);
    config(const std::string &path);
    ~config();
};


#endif