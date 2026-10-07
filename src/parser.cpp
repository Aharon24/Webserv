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


void Parser::start_parse()
{
    _index = 0;

    if (_tokens[_index].type != TOKEN_SERVER)
        throw std::runtime_error("Expected server");

    parse_server();
}

void Parser::parse_server(void)
{
    // server
    if (_tokens[_index].type != TOKEN_SERVER)
        throw std::runtime_error("Expected server");

    ++_index;

    // {
    if (_tokens[_index].type != TOKEN_LBRACE)
        throw std::runtime_error("Expected '{'");

    ++_index;

    // directives inside server
    while (_tokens[_index].type != TOKEN_RBRACE)
    {
        if (_tokens[_index].type == TOKEN_LISTEN)
            parse_listen();
        else
            throw std::runtime_error("Unknown directive inside server");
    }

    // }
    ++_index;
}

Parser::~Parser()
{

}