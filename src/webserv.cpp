#include "webserv.hpp"

webserv::webserv()
{
    this->_config_path = "";
}

webserv::webserv(const std::string &path)
{
    this->_config_path = path;
}

webserv::webserv(const webserv &ot)
{
    *this = ot;
}

webserv& webserv::operator=(const webserv &ot)
{
    if(this != &ot)
        this->_config_path = ot._config_path;
    return(*this);
}

void webserv::check_path(std::string &path)
{
    struct stat info;

    if (stat(path.c_str(), &info) != 0)
        throw std::runtime_error(
            "Cannot access configuration file: " + path);

    if (!S_ISREG(info.st_mode))
        throw std::runtime_error(
            "Configuration path is not a regular file: " + path);

    if (access(path.c_str(), R_OK) != 0)
        throw std::runtime_error(
            "Configuration file is not readable: " + path);
}


void webserv::take_path()
{
    if (this->_config_path == "")
        this->_config_path = "config/default.conf";

    this->check_path(this->_config_path);
}

//old 

// void webserv::take_path()
// {
//     if(this->_config_path == "")
//     {
//         this->_config_path = "config/default.conf";
//         this->_conf.set_path("config/default.conf");
//     }
//     else
//         this->_conf.set_path(this->_config_path);
//     this->check_path(this->_config_path);
// }

std::string webserv::get_path()
{
    return(this->_config_path);
}


webserv::~webserv()
{

}


