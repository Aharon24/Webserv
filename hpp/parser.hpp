#ifndef PARSER_HPP
#define PARSER_HPP

#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <stdexcept>
#include <sys/stat.h>

#include "ServerConfig.hpp"
#include "Tokenizer.hpp"


class Parser
{
private:
    std::vector<Token> _tokens;
    size_t _index;

public:
    Parser();
    Parser(const std::vector<Token>& _tokens);
    Parser(const Parser &ot);
    Parser& operator=(const Parser &ot);

    ~Parser();
    
    void print_tokens( void );
    void set_token(const std::vector<Token>& _tokens);

    std::vector<ServerConfig> start_parse(void);
    
    void parse_server(ServerConfig &conf);
    void parse_listen(ServerConfig &conf, bool &listen_defined);
    void parse_root(ServerConfig &conf, bool &root_defined);
    void parse_index(ServerConfig &conf, bool &index_defined);

    bool is_valid_ipv4(const std::string &ip);
    bool is_valid_port(const std::string &port);

    void validate_root(const ServerConfig &conf) const;

    void parse_location(ServerConfig &conf);
    void parse_location_root(LocationConfig &location);
    void parse_location_index(LocationConfig &location);        
};

#endif