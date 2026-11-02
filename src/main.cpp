#include "../hpp/webserv.hpp"
#include "../hpp/Server.hpp"
#include "../hpp/HttpRequest.hpp"




void init_webserv(std::string &path)
{
    webserv web(path);
    Tokenizer Tok;
    Parser pars;
    HttpParser Http_parser;
    std::vector<ServerConfig> configs;


    // pars.print_tokens();
    
    try
    {
        web.take_path();
        if (!Tok.open_file_empty_and_valid(web.get_path()))
            throw std::runtime_error("Invalid configuration file");

        Tok.create_tokens();
        pars.set_token(Tok.get_token());

        configs =  pars.start_parse();
        for (std::size_t i = 0; i < configs.size(); ++i)
        {
            std::cout << "\n===== SERVER " << i + 1 << " =====\n";
        
            for (std::map<int, std::string>::const_iterator it =
                    configs[i].error_pages.begin();
                 it != configs[i].error_pages.end(); ++it)
            {
                std::cout << "Error code: " << it->first
                          << " | Page: " << it->second << std::endl;
            }
        }
        for (std::size_t i = 0; i < configs.size(); ++i)
        {
            std::cout << "\n===== SERVER " << i + 1 << " =====" << std::endl;
        
            for (std::size_t j = 0; j < configs[i].locations.size(); ++j)
            {
                const LocationConfig &loc = configs[i].locations[j];
            
                std::cout << "Location: " << loc.path << std::endl;
                std::cout << "Return code: " << loc.return_code << std::endl;
                std::cout << "Return URL: " << loc.return_url << std::endl;
                std::cout << "Root: " << loc.root << std::endl;
                std::cout << "Index: " << loc.index << std::endl;
                std::cout << "autoindex: " << loc.autoindex << std::endl;
                std::cout << "up pload: " << loc.upload_enabled << std::endl;
                std::cout << "Max body size: " << configs[i].client_max_body_size << " bytes" << std::endl;
            }
        }
        for (std::size_t i = 0; i < configs.size(); ++i)
            pars.validate_root(configs[i]);

        for (std::size_t i = 0; i < configs.size(); ++i)
        {
            std::cout << "\nServer " << i + 1 << std::endl;
            std::cout << "IP: " << configs[i].ip << std::endl;
            std::cout << "Port: " << configs[i].port << std::endl;
            std::cout << "Root: " << configs[i].root << std::endl;
            std::cout << "Index: " << configs[i].index << std::endl;
        }
        Server serv(configs);
        serv.create_socket();
        std::cout << "Socket created successfully" << std::endl;

        serv.bind_socket();
        serv.listen_socket();
        serv.run(Http_parser);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Config error: " << e.what() << std::endl;
        return ;
    }

}

int main(int argc, char *argv[])
{
    std::string path;

    path = "";
    if(argc == 2)
        path = argv[1];
    init_webserv(path);
}

