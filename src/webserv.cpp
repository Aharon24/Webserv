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
    if (access(path.c_str(), F_OK) == 0)
    {
        std::cout << "have file ok\n";
    }
    else
        std::cout << "Error don't have a file you have a creat and give .conf file\n";
}

void webserv::take_path()
{
    if(this->_config_path == "")
    {
        this->_config_path = "config/default.conf";
        this->_conf.set_path("config/default.conf");
    }
    else
        this->_conf.set_path(this->_config_path);
    this->check_path(this->_config_path);
}



webserv::~webserv()
{

}


