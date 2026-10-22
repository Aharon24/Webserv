#ifndef PARSER_HPP
#define PARSER_HPP

#include <vector>
#include <stdexcept>
#include "Tokenizer.hpp"
#include "config.hpp"
#include "ServerConfig.hpp"
#include <cstdlib>

#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>


class Parser
{
private:
    std::vector<Token> _tokens;
    size_t _index;
    bool _listen_defined;
    bool _root_defined;
    ServerConfig conf;

public:
    Parser();
    Parser(const std::vector<Token>& _tokens);
    Parser(const Parser &ot);
    Parser& operator=(const Parser &ot);
    void print_tokens( void );
    ServerConfig start_parse(void);
    ~Parser();

    void set_token(const std::vector<Token>& _tokens);

    void parse_server(void);
    void parse_listen(void);
    void parse_root(void);
    void parse_index(void);
    bool is_valid_ipv4(const std::string& ip);
    bool is_valid_port(const std::string& port);
    void validate_root(void);
    void parse_location(void);
    void parse_location_root(LocationConfig &location);
    void parse_location_index(LocationConfig &location);
};

#endif