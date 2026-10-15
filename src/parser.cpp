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


ServerConfig Parser::start_parse()
{
    _index = 0;

    if (_tokens.empty())
        throw std::runtime_error("Config file is empty");

    if (_tokens[_index].type != TOKEN_SERVER)
        throw std::runtime_error("Expected server");

    if (_tokens[_index].type != TOKEN_SERVER)
        throw std::runtime_error(
            "Expected 'server' at the beginning of config"
        );

    parse_server();
    if (_index < _tokens.size())
        throw std::runtime_error(
            "Unexpected token after server block: '" +
            _tokens[_index].value + "'"
        );
    return conf;
}


void Parser::parse_root(void)
{

}
void Parser::parse_index(void)
{

}


bool Parser::is_valid_ipv4(const std::string& ip)
{
    int parts = 0;
    std::string::size_type start = 0;

    while(true)
    {
        std::string::size_type dot = ip.find('.', start);
        std::string part;
        int number = 0;

        if(dot == std::string::npos)
            part = ip.substr(start);
        else
            part = ip.substr(start, dot - start);

        if (part.empty() || part.size() > 3)
            return false;
        for (std::string::size_type i = 0; i < part.size(); ++i)
        {
            if (part[i] < '0' || part[i] > '9')
                return false;
            number = number * 10 + (part[i] - '0');
        }
        if (number > 255)
            return false;
        ++parts;
        if (dot == std::string::npos)
            break;
        start = dot + 1;
    }
    return parts == 4;
}


bool Parser::is_valid_port(const std::string& port)
{
    int number = 0;

    if (port.empty() || port.size() > 5)
        return false;
    for (std::string::size_type i = 0; i < port.size(); ++i)
    {
        if (port[i] < '0' || port[i] > '9')
            return false;

        number = number * 10 + (port[i] - '0');
    }
    if (number < 1 || number > 65535)
        return false;
    return true;
}

void Parser::parse_listen(void)
{
    ++_index;

    if (_index >= _tokens.size() ||
        _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected address after 'listen'");

    std::string address = _tokens[_index].value;
    std::string::size_type colon = address.find(':');
    if (colon == std::string::npos ||
        address.find(':', colon + 1) != std::string::npos)
        throw std::runtime_error(
            "Invalid listen address: expected IPv4:port"
        );
    std::string ip = address.substr(0, colon);
    std::string port = address.substr(colon + 1);

    if (!is_valid_ipv4(ip))
        throw std::runtime_error("Invalid IPv4 address");

    if (!is_valid_port(port))
        throw std::runtime_error("Invalid port");

    conf.ip = ip;
    conf.port = std::atoi(port.c_str());

    std::cout << "Listen IP: " << ip << std::endl;
    std::cout << "Listen port: " << port << std::endl;
    ++_index;
    if (_index >= _tokens.size() ||
        _tokens[_index].type != TOKEN_SEMICOLON)
        throw std::runtime_error("Expected ';' after 'listen'");
    ++_index;
}

void Parser::parse_root(void)
{
    
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
    while (_index < _tokens.size() && _tokens[_index].type != TOKEN_RBRACE)
    {
        if (_index >= _tokens.size())
            throw std::runtime_error(
                "Unexpected end of file: expected '}' to close server"
            );
        if (_tokens[_index].type == TOKEN_LISTEN)
            parse_listen();
        else if (_tokens[_index].type == TOKEN_ROOT)
            parse_root();
        else
            throw std::runtime_error("Unknown directive inside server");
    }

    // }
    ++_index;
}

Parser::~Parser()
{

}