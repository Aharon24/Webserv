#include "webserv.hpp"

webserv::webserv()
{
    this->_config_path = " ";
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


webserv::~webserv()
{

}


