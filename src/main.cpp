#include "../hpp/webserv.hpp"



void init_webserv(std::string &path)
{
    webserv web(path);
    Tokenizer Tok;
    Parser pars;

    web.take_path();
    Tok.open_file_empty_and_valid(web.get_path());
    Tok.create_tokens();
    pars.set_token(Tok.get_token());
    pars.print_tokens();
    pars.start_parse();

}

int main(int argc, char *argv[])
{
    std::string path;

    path = "";
    if(argc == 2)
        path = argv[1];
    init_webserv(path);
}

