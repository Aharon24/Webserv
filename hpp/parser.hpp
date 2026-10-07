#ifndef PARSER_HPP
#define PARSER_HPP

#include <vector>
#include <stdexcept>
#include "Tokenizer.hpp"
#include "Config.hpp"


// struct ServerConfig
// {
//     std::vector<std::string> listen;

//     std::map<int, std::string> error_pages;

//     size_t client_max_body_size;

//     std::vector<LocationConfig> locations;
// };

// struct LocationConfig
// {
//     std::string path;

//     std::vector<std::string> allowed_methods;

//     int         return_code;
//     std::string return_path;

//     std::string root;

//     bool        autoindex;

//     std::string index;

//     bool        upload;
//     std::string upload_store;

//     std::string cgi_extension;
//     std::string cgi_path;
// };



class Parser
{
private:
    std::vector<Token> _tokens;
    size_t _index;
    // cong_file setings_cong;
public:
    Parser();
    Parser(const std::vector<Token>& _tokens);
    Parser(const Parser &ot);
    Parser& operator=(const Parser &ot);
    void print_tokens( void );
    void start_parse(void);
    ~Parser();

    void set_token(const std::vector<Token>& _tokens);
    void parse_server(void);
};

#endif