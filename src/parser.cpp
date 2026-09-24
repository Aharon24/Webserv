#include "parser.hpp"

Parser::Parser()
{
}
Parser::Parser(const std::vector<Token>& _tokens)
{

    this->_tokens = _tokens;
}


void Parser::set_token(const std::vector<Token>& _tokens)
{
    this->_tokens = _tokens;
}
Parser::Parser(const Parser &ot)
{
    this->_index = ot._index;
    this->_tokens = ot._tokens;
}

Parser& Parser::operator=(const Parser &ot)
{
    if(this != &ot)
    {
        this->_index = ot._index;
        this->_tokens = ot._tokens;
    }
    return(*this);
}


void Parser::print_tokens(void)
{
    std::cout << "------ in parse -> \n \n" ;
    for (size_t i = 0; i < this->_tokens.size(); ++i)
    {
        std::cout << "Index: " << i 
                  << " | Type: " << this->_tokens[i].type
                  << " | Value: [ " << this->_tokens[i].value << " ]" 
                  << std::endl;
    }
}


 void Parser::start_parse(void)
 {
    
 }

Parser::~Parser()
{

}