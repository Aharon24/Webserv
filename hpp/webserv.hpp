#include <iostream>
#include <fstream>
#include <string>



class webserv
{
private:
    std::string _config_path;

public:
    webserv();
    webserv& operator=(const webserv &ot);
    webserv(const webserv &ot);
    webserv(const std::string &path);
    ~webserv();
};


