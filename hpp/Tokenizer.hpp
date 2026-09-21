#ifndef TOKENIZER_HPP
#define TOKENIZER_HPP


#include <string>
#include <vector>
#include <fstream>
#include <iostream>

enum TokenType
{
    TOKEN_SERVER,       // "server"
    TOKEN_LOCATION,     // "location"
    TOKEN_LISTEN,
    TOKEN_ROOT,
    TOKEN_DIRECTIVE,    // "listen", "root", "index", "server_name", "error_page", "client_max_body_size" և այլն
    TOKEN_INDEX,
    TOKEN_ARGUMENT,     // "8080", "localhost", "/var/www/html", "GET", "POST"
    TOKEN_ERROR_PAGE,

    TOKEN_CLIENT_MAX_BODY_SIZE, 
    TOKEN_ALLOWED_METHODS,      
    TOKEN_AUTOINDEX,            
    TOKEN_UPLOAD,               
    TOKEN_UPLOAD_PATH,          
    TOKEN_RETURN,               
    TOKEN_CGI,

    TOKEN_LBRACE,       // '{'
    TOKEN_RBRACE,       // '}'
    TOKEN_SEMICOLON,    // ';'

    TOKEN_EOF,          // Ֆայլի ավարտ
    TOKEN_UNKNOWN       // Անհայտ սիմվոլ (սխալի համար)
};

struct Token
{
    TokenType type;
    std::string value;
};

class Tokenizer
{
private:
    std::vector<Token> _tokens;
    std::string file_path;
public:
    Tokenizer();
    Tokenizer(std::string path);
    Tokenizer(const Tokenizer &ot);
    Tokenizer &operator=(const Tokenizer &ot);
    ~Tokenizer();

    bool open_file_empty_and_valid(std::string path);
    void create_tokens();
    void str_to_token(std::string line);
    bool word_to_token(std::string &line, size_t &i);
    void add_token_word(std::string &word);
    void print_tokens(void);
    std::string token_type_to_string(TokenType type) const;
    void add_separator_token(std::string &line, size_t &i);

};





#endif