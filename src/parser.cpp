#include "parser.hpp"

Parser::Parser() : _index(0)
{
}
Parser::Parser(const std::vector<Token> &tokens) : _tokens(tokens), _index(0)
{
}

void Parser::set_token(const std::vector<Token> &tokens)
{
    _tokens = tokens;
    _index = 0;
}

Parser::Parser(const Parser &other) : _tokens(other._tokens), _index(other._index)
{
}

Parser &Parser::operator=(const Parser &other)
{
    if (this != &other)
    {
        _tokens = other._tokens;
        _index = other._index;
    }
    return *this;
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


std::vector<ServerConfig> Parser::start_parse()
{
    _index = 0;
    std::vector<ServerConfig> servers;
    if (_tokens.empty())
        throw std::runtime_error("Config file is empty");


    while (_index < _tokens.size())
    {
        if (_tokens[_index].type != TOKEN_SERVER)
            throw std::runtime_error(
                "Expected 'server' at the beginning of server block"
            );

        ServerConfig conf;
        parse_server(conf);

        servers.push_back(conf);
    }
    return servers;
}


void Parser::parse_root(ServerConfig &conf, bool &root_defined)
{
    if (root_defined)
        throw std::runtime_error("Duplicate 'root' directive");
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected path after 'root'");

    std::string path = _tokens[_index].value;
    if (path.empty())
        throw std::runtime_error("Root path cannot be empty");
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_SEMICOLON)
        throw std::runtime_error("Expected ';' after 'root'");
    conf.root = path;
    ++_index;
    root_defined = true;
}
void Parser::parse_index( ServerConfig &conf, bool &index_defined)
{
    if (index_defined)
        throw std::runtime_error("Duplicate 'index' directive");
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected filename after 'index'");
    conf.index = _tokens[_index].value;
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_SEMICOLON)
        throw std::runtime_error("Expected ';' after 'index'");
    ++_index;
    index_defined = true;
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

void Parser::parse_listen(ServerConfig &conf, bool &listen_defined)
{
    if (listen_defined)
        throw std::runtime_error("Duplicate 'listen' directive");
    ++_index;

    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected address after 'listen'");

    std::string address = _tokens[_index].value;
    std::string::size_type colon = address.find(':');
    if (colon == std::string::npos || address.find(':', colon + 1) != std::string::npos)
        throw std::runtime_error("Invalid listen address: expected IPv4:port");
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
    listen_defined = true;
}

void Parser::validate_root(const ServerConfig &conf) const
{
    if (conf.root.empty())
        throw std::runtime_error("Missing required directive: root");

    struct stat info;

    if (stat(conf.root.c_str(), &info) != 0)
        throw std::runtime_error("Cannot access root path");

    if (!S_ISDIR(info.st_mode))
        throw std::runtime_error("Root path is not a directory");

    if (access(conf.root.c_str(), R_OK | X_OK) != 0)
        throw std::runtime_error("Root directory is not accessible");
}


void Parser::parse_location_root(LocationConfig &location)
{
    if (_index >= _tokens.size() ||
        _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected path after 'root' in location");
    location.root = _tokens[_index].value;
    ++_index;
    if (_index >= _tokens.size() ||
        _tokens[_index].type != TOKEN_SEMICOLON)
        throw std::runtime_error("Expected ';' after location root");
    ++_index;
}
   
void Parser::parse_location_index(LocationConfig &location)
{
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected filename after 'index' in location");
    location.index = _tokens[_index].value;
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_SEMICOLON)
        throw std::runtime_error("Expected ';' after location index");
    ++_index;
}



void Parser::parse_location(ServerConfig &conf)
{
    LocationConfig location;
    bool root_defined = false;
    bool index_defined = false;
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected path after 'location'");
    location.path = _tokens[_index].value;
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_LBRACE)
        throw std::runtime_error("Expected '{' after location path");
    ++_index;
    while (_index < _tokens.size() && _tokens[_index].type != TOKEN_RBRACE)
    {
        if (_tokens[_index].type == TOKEN_ROOT)
        {
            if (root_defined)
                throw std::runtime_error("Duplicate 'root' directive in location");
            ++_index;
            parse_location_root(location);
            root_defined = true;
        }
        else if (_tokens[_index].type == TOKEN_INDEX)
        {
            if (index_defined)
                throw std::runtime_error("Duplicate 'index' directive in location");
            ++_index;
            parse_location_index(location);
            index_defined = true; 
        }
        else
            throw std::runtime_error("Unknown directive inside location: '" + _tokens[_index].value + "'");
    }
    if (_index >= _tokens.size())
        throw std::runtime_error("Unexpected end of file: expected '}' after location");
    ++_index;
    conf.locations.push_back(location);
}


void Parser::parse_server(ServerConfig &conf)
{
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_SERVER)
        throw std::runtime_error("Expected 'server'");
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_LBRACE)
        throw std::runtime_error("Expected '{' after 'server'");
    ++_index;
    bool listen_defined = false;
    bool root_defined = false;
    bool index_defined = false;
    while (_index < _tokens.size() &&  _tokens[_index].type != TOKEN_RBRACE)
    {
        if (_tokens[_index].type == TOKEN_LISTEN)
            parse_listen(conf, listen_defined);
        else if (_tokens[_index].type == TOKEN_ROOT)
            parse_root(conf, root_defined);
        else if (_tokens[_index].type == TOKEN_INDEX)
            parse_index(conf, index_defined);
        else if (_tokens[_index].type == TOKEN_LOCATION)
            parse_location(conf);
        else
            throw std::runtime_error("Unknown directive inside server");
    }
    if (_index >= _tokens.size())
        throw std::runtime_error("Unexpected end of file: expected '}'");

    ++_index;
    if (!listen_defined)
        throw std::runtime_error("Missing required directive: listen");
}


Parser::~Parser()
{

}