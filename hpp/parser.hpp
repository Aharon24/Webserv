#ifndef PARSER_HPP
#define PARSER_HPP

#include <vector>
#include <stdexcept>
#include "Tokenizer.hpp"
#include "Config.hpp"


struct cong_file
{
    int port;
    std::string ip;
    std::string TOKEN_SERVER;      
    std::string TOKEN_LISTEN;  
    std::string TOKEN_LOCATION;              
    std::string TOKEN_ROOT;                
    std::string TOKEN_INDEX;                  
    std::string TOKEN_ERROR_PAGE;            
    std::string TOKEN_CLIENT_MAX_BODY_SIZE;   
    std::string TOKEN_ALLOWED_METHODS;   
    std::string TOKEN_AUTOINDEX;        
    std::string TOKEN_RETURN;              
    std::string TOKEN_UPLOAD;                 
    std::string TOKEN_UPLOAD_PATH;          
    std::string TOKEN_CGI;            
    std::string TOKEN_DIRECTIVE;              
    std::string TOKEN_ARGUMENT;              
    std::string TOKEN_LBRACE;               
    std::string TOKEN_RBRACE;                 
    std::string TOKEN_SEMICOLON;              
};



class Parser
{
private:
    std::vector<Token> _tokens;
    size_t _index;
    cong_file setings_cong;
public:
    Parser();
    Parser(const std::vector<Token>& _tokens);
    Parser(const Parser &ot);
    Parser& operator=(const Parser &ot);
    void print_tokens( void );
    void start_parse(void);
    ~Parser();

    void set_token(const std::vector<Token>& _tokens);
};

#endif