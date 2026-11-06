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


void Parser::warn_duplicate_listen_without_server_name(const std::vector<ServerConfig> &servers) const
{
    for (std::size_t i = 0; i < servers.size(); ++i)
    {
        for (std::size_t j = i + 1; j < servers.size(); ++j)
        {
            bool same_listen = false;
            std::string shared_ip;
            int shared_port = 0;

            for (std::size_t a = 0; a < servers[i].listens.size() && !same_listen; ++a)
            {
                for (std::size_t b = 0; b < servers[j].listens.size(); ++b)
                {
                    if (servers[i].listens[a].ip == servers[j].listens[b].ip && servers[i].listens[a].port == servers[j].listens[b].port)
                    {
                        same_listen = true;
                        shared_ip = servers[i].listens[a].ip;
                        shared_port = servers[i].listens[a].port;
                        break;
                    }
                }
            }
            if (same_listen && servers[i].server_name.empty() && servers[j].server_name.empty())
            {
                std::cerr << "WARNING: server blocks "
                << i + 1 << " and " << j + 1 << " use the same listen address "
                << shared_ip << ":" << shared_port << " without server_name."
                << std::endl;
            }
            else if (same_listen && !servers[i].server_name.empty() && servers[i].server_name == servers[j].server_name)
            {
                std::cerr << "WARNING: duplicate server_name '"
                << servers[i].server_name << "' in server blocks "
                << i + 1 << " and " << j + 1 << " on "
                << shared_ip << ":" << shared_port << "."
                << std::endl;
            }
        }
    }
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

    ListenConfig listen;
    listen.ip = ip;
    listen.port = std::atoi(port.c_str());

    for (std::size_t i = 0; i < conf.listens.size(); ++i)
    {
        if (conf.listens[i].ip == listen.ip && conf.listens[i].port == listen.port)
            throw std::runtime_error("Duplicate listen address");
    }
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_SEMICOLON)
        throw std::runtime_error("Expected ';' after 'listen'");
    ++_index;
    listen_defined = true;
    conf.listens.push_back(listen);
    if (conf.listens.size() == 1)
    {
        conf.ip = listen.ip;
        conf.port = listen.port;
    }
    std::cout << "Listen IP: " << ip << std::endl;
    std::cout << "Listen port: " << port << std::endl;
}


void Parser::check_location(const ServerConfig &conf) const
{
    for(size_t i = 0; i < conf.locations.size(); ++i)
    {
        const LocationConfig &location = conf.locations[i];

        if (location.root.empty())
            throw std::runtime_error("Missing root for location: " + location.path);

        validate_directory(location.root, "location root", R_OK | X_OK);

        // Check upload directory
        if (location.upload_enabled)
            validate_directory(location.upload_path, "upload path",W_OK | X_OK);
        for (std::size_t j = 0; j < location.cgi.size(); ++j)
        {
            validate_executable(location.cgi[j].interpreter, "CGI interpreter");
        }
    }
}



void Parser::validate_directory(const std::string &path, const std::string &name, int permissions) const
{
    struct stat info;

    if (path.empty())
        throw std::runtime_error(name + " path is empty");
    if (stat(path.c_str(), &info) != 0)
        throw std::runtime_error(
            "Cannot access " + name + ": " + path);

    if (!S_ISDIR(info.st_mode))
        throw std::runtime_error(
            name + " is not a directory: " + path);

    if (access(path.c_str(), permissions) != 0)
        throw std::runtime_error(
            "Insufficient permissions for " + name + ": " + path);
}

void Parser::validate_executable(const std::string &path, const std::string &name) const
{
    struct stat info;

    if (path.empty())
        throw std::runtime_error(name + " path is empty");
    if (stat(path.c_str(), &info) != 0)
        throw std::runtime_error("Cannot access " + name + ": " + path);

    if (!S_ISREG(info.st_mode))
        throw std::runtime_error(name + " is not a regular file: " + path);

    if (access(path.c_str(), X_OK) != 0)
        throw std::runtime_error(name + " is not executable: " + path);
}

void Parser::validate_root(const ServerConfig &conf) const
{

    if (!conf.root.empty())
        validate_directory(conf.root, "server root", R_OK | X_OK);
    check_location(conf);
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


void Parser::parse_location_allowed_methods(LocationConfig &location)
{
    bool methood_found = false;
    std::string  method;
    while(_index < _tokens.size() && _tokens[_index].type != TOKEN_SEMICOLON)
    {
        if(_tokens[_index].type != TOKEN_ARGUMENT)
            throw std::runtime_error("Expected HTTP method");
        
        method = _tokens[_index].value;
        if(method != "GET" && method != "POST" && method != "DELETE")
            throw std::runtime_error("Invalid method HTTP");

        for(std::size_t i = 0; i < location.allowed_methods.size(); ++i)
        {
            if(location.allowed_methods[i] == method)
                throw std::runtime_error("Duplicate HTTP method" + method);
        }
        location.allowed_methods.push_back(method);
        methood_found = true;
        ++_index;
    }
    if(!methood_found)
        throw std::runtime_error("AT least one HTTP methood is required");
    if(_index >= _tokens.size()  || _tokens[_index].type != TOKEN_SEMICOLON)
        throw std::runtime_error("Expected ';' after allowed_methods");
    ++_index;
}

void Parser::parse_location_return(LocationConfig &location)
{
    if(_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected return status code ");

    const std::string code = _tokens[_index].value;
    
    if(code != "301" && code != "302" && code != "303" && code != "307" && code != "308")
        throw std::runtime_error("Only return 301,302 303, 307, 308 is supported -> " + code);
    
    location.return_code = std::atoi(code.c_str());
    ++_index;
    if(_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected return URL");
    
    location.return_url = _tokens[_index].value;
    ++_index;
    if(_index >= _tokens.size() || _tokens[_index].type != TOKEN_SEMICOLON)
        throw std::runtime_error("Expected ';'  after return");
    ++_index;

    std::cout << "return code -> " << location.return_code << "\n";
    std::cout << "return URL -> " << location.return_url  << "\n";
}


void Parser::parse_location_upload(LocationConfig &location)
{
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected 'on' or 'off' after upload");
    
    const std::string value = _tokens[_index].value;

    if (value == "on")
        location.upload_enabled = true;
    else if (value == "off")
        location.upload_enabled = false;
    else
        throw std::runtime_error("Invalid upload value: " + value);

    ++_index;

    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_SEMICOLON)
        throw std::runtime_error("Expected ';' after upload");

    ++_index;

}

void Parser::parse_location_upload_path(LocationConfig &location)
{
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected path after 'upload_path'");

    const std::string path = _tokens[_index].value;
    if (path.empty())
        throw std::runtime_error("Upload path cannot be empty");
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_SEMICOLON)
        throw std::runtime_error("Expected ';' after upload_path");
    ++_index;
    location.upload_path = path;
}


void Parser::parse_location_cgi(LocationConfig &location)
{

    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected CGI extension");
    const std::string ext = _tokens[_index].value;
    if (ext.size() < 2 || ext[0] != '.' || ext.find('/') != std::string::npos || ext.find('\\') != std::string::npos)
        throw std::runtime_error("Invalid CGI extension");
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected CGI interpreter");
    const std::string interpreter = _tokens[_index].value;
    if (interpreter.empty())
        throw std::runtime_error("CGI interpreter cannot be empty");
    for (std::size_t i = 0; i < location.cgi.size(); ++i)
    {
        if (location.cgi[i].extension == ext)
            throw std::runtime_error("Duplicate CGI extension: " + ext);
    }
    ++_index;

    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_SEMICOLON)
        throw std::runtime_error("Expected ';' after cgi");
    ++_index;
    CgiConfig config;
    config.extension = ext;
    config.interpreter = interpreter;

    location.cgi.push_back(config);
}

void Parser::parse_location_directives(LocationConfig &location)
{
    bool root_defined = false;
    bool index_defined = false;
    bool methods_defined = false;
    bool return_defined = false;
    bool autoindex_defined = false;
    bool upload_defined = false;
    bool upload_path_defined = false;
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
        else if (_tokens[_index].type == TOKEN_ALLOWED_METHODS)
        {
            if(methods_defined)
                throw std::runtime_error("Duplicate 'allowed_methods' ");
            ++_index;
            parse_location_allowed_methods(location);
            methods_defined = true;
        }
        else if(_tokens[_index].type == TOKEN_RETURN)
        {
            if(return_defined)
                throw std::runtime_error("Duplicate 'return' directives in location");
            ++_index;
            parse_location_return(location);
            return_defined = true;
        }
        else if (_tokens[_index].type == TOKEN_AUTOINDEX)
        {
            if (autoindex_defined)
                throw std::runtime_error("Duplicate 'autoindex' directive");
        
            ++_index;
            parse_location_autoindex(location);
            autoindex_defined = true;
        }
        else if (_tokens[_index].type == TOKEN_UPLOAD)
        {
            if (upload_defined)
                throw std::runtime_error("Duplicate 'upload' directive");
            ++_index;
            parse_location_upload(location);
            upload_defined = true;
        }
        else if(_tokens[_index].type == TOKEN_UPLOAD_PATH)
        {
            if (upload_path_defined)
                throw std::runtime_error("Duplicate 'upload_path' directive");
                
            ++_index;
            parse_location_upload_path(location);
            upload_path_defined = true;
        }
        else if (_tokens[_index].type == TOKEN_CGI)
        {
            ++_index;
            parse_location_cgi(location);
        }
        
        else
            throw std::runtime_error("Unknown directive inside location: '" + _tokens[_index].value + "'");
    }
    if (location.upload_enabled && location.upload_path.empty())
        throw std::runtime_error("'upload on' requires 'upload_path'");
}


void Parser::parse_location(ServerConfig &conf)
{
    LocationConfig location;
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected path after 'location'");
    location.path = _tokens[_index].value;
    if (location.path.empty() || location.path[0] != '/')
        throw std::runtime_error("Location path must start with '/'");
    for (std::size_t i = 0; i < conf.locations.size(); ++i)
    {
        if (conf.locations[i].path == location.path)
            throw std::runtime_error(
                "Duplicate location: " + location.path);
    }
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_LBRACE)
        throw std::runtime_error("Expected '{' after location path");
    ++_index;
    parse_location_directives(location);
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_RBRACE)
        throw std::runtime_error("Unexpected end of file: expected '}' after location");
    ++_index;
    conf.locations.push_back(location);
}

void Parser::parse_server_name(ServerConfig &conf, bool &server_name_defined)
{
    if(server_name_defined)
        throw std::runtime_error("Duplicate 'server_name' directive");
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected hostname after 'server_name'");
    conf.server_name = _tokens[_index].value;
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_SEMICOLON)
        throw std::runtime_error("Expected ';' after 'server_name'");
    ++_index;
    server_name_defined = true;

}

void Parser::parse_error_page(ServerConfig &conf)
{
    ++_index;

    if(_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected HTTP error code after 'error_page'");
    const std::string code = _tokens[_index].value;

    if(code.size() != 3 || code[0] < '0' || code[0] > '9' || code[1] > '9' || code[1] < '0' || code[2] < '0' || code[2] > '9')
        throw std::runtime_error("Invalid HTTP error code");

    int status_code = std::atoi(code.c_str());

    if(status_code < 400 || status_code > 599)
        throw std::runtime_error("HTTP error code must be between 400 and 599");
    if(conf.error_pages.find(status_code) != conf.error_pages.end())
        throw std::runtime_error("Duplicate error_page status code");
    ++_index;
    if(_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected HTTP error page URL");

    const std::string path = _tokens[_index].value;

    if(path.empty() || path[0] != '/')
        throw std::runtime_error("Error page URI must start with '/'");
    ++_index;

    if(_index >= _tokens.size() || _tokens[_index].type != TOKEN_SEMICOLON)
        throw std::runtime_error("Expected ';' after error_page");
    ++_index;
    conf.error_pages[status_code] = path;
}


void Parser::bit_size(const std::string &suffix, std::size_t & multiplier)
{ 
    if (suffix.empty())
    multiplier = 1;
    else if (suffix == "K" || suffix == "k")
        multiplier = 1024;
    else if (suffix == "M" || suffix == "m")
        multiplier = 1024 * 1024;
    else if (suffix == "G" || suffix == "g")
        multiplier = 1024 * 1024 * 1024;
    else
        throw std::runtime_error("Invalid size suffix");
}

void Parser::get_number_multiplayer( const std::string &number_part, std::size_t &number)
{
    const std::size_t max_size = std::numeric_limits<std::size_t>::max();

    for (std::string::size_type i = 0; i < number_part.size(); ++i)
    {
        int digit = number_part[i] - '0';

        if (number > (max_size - digit) / 10)
            throw std::runtime_error("client_max_body_size overflow");

        number = number * 10 + digit;
    }
}

void Parser::parse_client_max_body_size(ServerConfig &conf, bool &defined)
{
    if (defined)
        throw std::runtime_error("Duplicate 'client_max_body_size' directive");
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected size after 'client_max_body_size'");

    const std::string value = _tokens[_index].value;
    std::string::size_type pos = 0;

    while(pos < value.size() && value[pos] >= '0'&& value[pos] <= '9')
        ++pos;
    if(pos == 0)
        throw std::runtime_error("Invalid client_max_body_size value");
    std::string number_part = value.substr(0, pos);
    std::string suffix = value.substr(pos);
    std::size_t multiplier = 1;
    std::size_t number = 0;
    bit_size(suffix, multiplier);

    get_number_multiplayer(number_part, number);

    if (number > std::numeric_limits<std::size_t>::max() / multiplier)
        throw std::runtime_error("client_max_body_size overflow");

    std::size_t size = number * multiplier;
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_SEMICOLON)
        throw std::runtime_error("Expected ';' after client_max_body_size");
    ++_index;

    conf.client_max_body_size = size;
    defined = true;
}


void Parser::parse_location_autoindex(LocationConfig &location)
{
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_ARGUMENT)
        throw std::runtime_error("Expected 'on' or 'off' after autoindex");
    
    const std::string value = _tokens[_index].value;

    if(value == "on")
        location.autoindex = true;
    else if (value == "off")
        location.autoindex = false;
    else
        throw std::runtime_error("Invalid autoindex value: " + value);
    ++_index;
    if (_index >= _tokens.size() || _tokens[_index].type != TOKEN_SEMICOLON)
        throw std::runtime_error("Expected ';' after autoindex");
    ++_index;
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
    bool server_name_defined = false;
    bool root_defined = false;
    bool index_defined = false;
    bool client_max_body_size_defined = false;
    while (_index < _tokens.size() &&  _tokens[_index].type != TOKEN_RBRACE)
    {
        if (_tokens[_index].type == TOKEN_LISTEN)
            parse_listen(conf, listen_defined);
        else if (_tokens[_index].type == TOKEN_SERVER_NAME)
            parse_server_name(conf, server_name_defined);
        else if (_tokens[_index].type == TOKEN_ROOT)
            parse_root(conf, root_defined);
        else if (_tokens[_index].type == TOKEN_INDEX)
            parse_index(conf, index_defined);
        else if (_tokens[_index].type == TOKEN_LOCATION)
            parse_location(conf);
        else if(_tokens[_index].type == TOKEN_ERROR_PAGE)
            parse_error_page(conf);
        else if (_tokens[_index].type == TOKEN_CLIENT_MAX_BODY_SIZE)
            parse_client_max_body_size(conf, client_max_body_size_defined);
        else
            throw std::runtime_error("Unknown directive inside server");
    }
    if (_index >= _tokens.size())
        throw std::runtime_error("Unexpected end of file: expected '}'");

    ++_index;
    if (!listen_defined)
        throw std::runtime_error("Missing required directive: listen");
    for (std::size_t i = 0; i < conf.locations.size(); ++i)
    {
        if (conf.locations[i].root.empty())
            conf.locations[i].root = conf.root;
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
    warn_duplicate_listen_without_server_name(servers);
    return servers;
}

Parser::~Parser()
{

}