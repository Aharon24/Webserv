
#ifndef WEBSERV_HPP
#define WEBSERV_HPP

#include <iostream>
#include <fstream>
#include <string>
#include "config.hpp"
#include <unistd.h>



class webserv
{
private:
    std::string _config_path;
    config _conf;
public:
    webserv();
    webserv& operator=(const webserv &ot);
    webserv(const webserv &ot);
    webserv(const std::string &path);
    ~webserv();

    void take_path();
    void check_path(std::string &path);


};

#endif

