#ifndef PARSER_HPP
#define PARSER_HPP

#include <vector>
#include <stdexcept>
#include "Tokenizer.hpp"
#include "Config.hpp"



class Parser
{
private:
    const std::vector<Token>& _tokens;
    size_t _index;
public:
    Parser();
    Parser(const std::vector<Token>& _tokens);
    Parser(const Parser &ot);
    Parser& operator=(const Parser &ot);
    void print_tokens( void );
    ~Parser();



    void set_token(const std::vector<Token>& _tokens);
};




#endif