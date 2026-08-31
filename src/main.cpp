#include "../hpp/webserv.hpp"


void init_webserv(std::string &path)
{
    webserv web(path);

    
}

int main(int argc, char *argv[])
{
    std::string path;
    
    path = " ";
    if(argc == 2)
        path = argv[1];
    init_webserv(path);
}