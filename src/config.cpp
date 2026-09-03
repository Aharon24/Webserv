#include "config.hpp"


config::config()
{

}
config::config(const config &ot)
{
    this->_path = ot._path;
}

config::config(const std::string &path)
{
    _path = path;
}

config& config::operator=(const config &ot)
{
    if(this != &ot)
        this->_path = ot._path;
    return (*this);
}


void config::set_path(const std::string &path)
{
    this->_path = path;
}

config::~config()
{

}