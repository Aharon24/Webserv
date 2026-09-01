
#ifndef WEBSERV_HPP
#define WEBSERV_HPP

#include <iostream>
#include <fstream>
#include <string>
#include "config.hpp"



class webserv
{
private:
    std::string _config_path;
    config conf;
public:
    webserv();
    webserv& operator=(const webserv &ot);
    webserv(const webserv &ot);
    webserv(const std::string &path);
    ~webserv();

};

#endif

