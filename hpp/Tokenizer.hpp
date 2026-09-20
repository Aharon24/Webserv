#ifndef TOKENIZER_HPP
#define TOKENIZER_HPP


#include <string>
#include <vector>
#include <fstream>
#include <iostream>

enum TokenType
{
    // 1. Բլոկների բանալի բառեր (Block Keywords)
    TOKEN_SERVER,       // "server"
    TOKEN_LOCATION,     // "location"

    // 2. Դիրեկտիվաներ (Directives)
    TOKEN_DIRECTIVE,    // "listen", "root", "index", "server_name", "error_page", "client_max_body_size" և այլն

    // 3. Արժեքներ (Values / Arguments)
    TOKEN_ARGUMENT,     // "8080", "localhost", "/var/www/html", "GET", "POST"

    // 4. Սիմվոլներ (Special Characters)
    TOKEN_LBRACE,       // '{'
    TOKEN_RBRACE,       // '}'
    TOKEN_SEMICOLON,    // ';'

    // 5. Ծառայողական
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
};





#endif