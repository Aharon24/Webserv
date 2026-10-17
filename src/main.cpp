#include "../hpp/webserv.hpp"
#include "../hpp/Server.hpp"




void init_webserv(std::string &path)
{
    webserv web(path);
    Tokenizer Tok;
    Parser pars;
    ServerConfig conf;


    // pars.print_tokens();
    
    try
    {
        web.take_path();
        Tok.open_file_empty_and_valid(web.get_path());
        Tok.create_tokens();
        pars.set_token(Tok.get_token());
        conf =  pars.start_parse();
        pars.validate_root();
        Server serv(conf);
        serv.create_socket();

        std::cout << "Socket created successfully" << std::endl;
        serv.bind_socket();
        serv.listen_socket();
        int client_fd = serv.accept_connection();

        std::cout << "Client connected! FD: "
                  << client_fd << std::endl;
        serv.read_request(client_fd);
        close(client_fd);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Config error: " << e.what() << std::endl;
        return ;
    }
    std::cout << "IP: " << conf.ip << std::endl;
    std::cout << "Port: " << conf.port << std::endl;
    std::cout << "Root: " << conf.root << std::endl;
    std::cout << "Index: " << conf.index << std::endl;

}

int main(int argc, char *argv[])
{
    std::string path;

    path = "";
    if(argc == 2)
        path = argv[1];
    init_webserv(path);
}

