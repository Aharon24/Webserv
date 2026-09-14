#ifndef TOKENIZER_HPP
#define TOKENIZER_HPP


#include <string>
#include <vector>
#include <iostream>

enum TokenType
{
    TOKEN_WORD,
    TOKEN_LBRACE,
    TOKEN_RBRACE
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
public:
    Tokenizer();
    ~Tokenizer();

    void create_tokens(std::string path);
};





#endif