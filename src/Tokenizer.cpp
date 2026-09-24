#include "Tokenizer.hpp"


Tokenizer::Tokenizer()
{
    file_path = "";
}


Tokenizer::Tokenizer(std::string path)
{
    file_path = path;
}
Tokenizer::Tokenizer(const Tokenizer &ot)
{
    this->_tokens = ot._tokens;
    this->file_path = ot.file_path;
}
Tokenizer &Tokenizer::operator=(const Tokenizer &ot)
{
    if(this != &ot)
    {
        this->_tokens = ot._tokens;
        this->file_path = ot.file_path;
    }
    return (*this);
}

bool Tokenizer::open_file_empty_and_valid(std::string path)
{
    std::ifstream file(path.c_str());

    if(!file.is_open())
    {
        std::cerr << "Error: Could not open file " << path << std::endl;
        return false ;
    }
    if (file.peek() == std::ifstream::traits_type::eof())
    {
        std::cerr << "Error: File " << path << " is empty!" << std::endl;
        file.close();
        return false;
    }

    file.close();
    file_path = path;
    return true;

}

// Вспомогательный метод для перевода enum в строку (для красивого вывода)
std::string Tokenizer::token_type_to_string(TokenType type) const
{
    switch (type)
    {
        case TOKEN_SERVER:                 return "TOKEN_SERVER";
        case TOKEN_LISTEN:                 return "TOKEN_LISTEN";
        case TOKEN_LOCATION:               return "TOKEN_LOCATION";
        case TOKEN_ROOT:                   return "TOKEN_ROOT";
        case TOKEN_INDEX:                  return "TOKEN_INDEX";
        case TOKEN_ERROR_PAGE:             return "TOKEN_ERROR_PAGE";
        case TOKEN_CLIENT_MAX_BODY_SIZE:   return "TOKEN_CLIENT_MAX_BODY_SIZE";
        case TOKEN_ALLOWED_METHODS:        return "TOKEN_ALLOWED_METHODS";
        case TOKEN_AUTOINDEX:              return "TOKEN_AUTOINDEX";
        case TOKEN_RETURN:                 return "TOKEN_RETURN";
        case TOKEN_UPLOAD:                 return "TOKEN_UPLOAD";
        case TOKEN_UPLOAD_PATH:            return "TOKEN_UPLOAD_PATH";
        case TOKEN_CGI:                    return "TOKEN_CGI";
        case TOKEN_DIRECTIVE:              return "TOKEN_DIRECTIVE";
        case TOKEN_ARGUMENT:               return "TOKEN_ARGUMENT";
        case TOKEN_LBRACE:                 return "TOKEN_LBRACE";
        case TOKEN_RBRACE:                 return "TOKEN_RBRACE";
        case TOKEN_SEMICOLON:              return "TOKEN_SEMICOLON";
        default:                           return "TOKEN_UNKNOWN";
    }
}

void Tokenizer::print_tokens(void)
{
    for (size_t i = 0; i < this->_tokens.size(); ++i)
    {
        std::cout << "Index: " << i 
                  << " | Type: " << token_type_to_string(this->_tokens[i].type)
                  << " | Value: [ " << this->_tokens[i].value << " ]" 
                  << std::endl;
    }
}

void Tokenizer::add_token_word(std::string &word)
{
    Token t;
    t.value = word;
    if (word == "server") {
        t.type = TOKEN_SERVER;
    } 
    else if (word == "listen") {
        t.type = TOKEN_LISTEN;
    } 
    else if (word == "location") {
        t.type = TOKEN_LOCATION;
    } 
    else if (word == "root") {
        t.type = TOKEN_ROOT;
    } 
    else if (word == "index") {
        t.type = TOKEN_INDEX;
    } 
    else if (word == "error_page") {
        t.type = TOKEN_ERROR_PAGE;
    }
    else if (word == "client_max_body_size") {
        t.type = TOKEN_CLIENT_MAX_BODY_SIZE;
    }
    else if (word == "allowed_methods") {
        t.type = TOKEN_ALLOWED_METHODS;
    }
    else if (word == "autoindex") {
        t.type = TOKEN_AUTOINDEX;
    }
    else if (word == "return") {
        t.type = TOKEN_RETURN;
    }
    else if (word == "upload")                 
        t.type = TOKEN_UPLOAD;       // <-- ADDED
    else if (word == "upload_path")            
        t.type = TOKEN_UPLOAD_PATH;  // <-- ADDED
    else if (word == "cgi")                    
        t.type = TOKEN_CGI;
    else {
        t.type = TOKEN_ARGUMENT;
    }
    this->_tokens.push_back(t);
}


bool Tokenizer::word_to_token(std::string &line, size_t &i)
{
    std::string word = "";
    // std::cout << "\nstart Word\n";
    while (i < line.length() && 
           !std::isspace(static_cast<unsigned char>(line[i])) && 
           line[i] != '{' && line[i] != '}' && line[i] != ';' && line[i] != '#')
    {
        word += line[i];
        i++;
    }
    if (word.empty())
        return false;
    // std::cout << "Word Token: [" << word << "]" << std::endl;
    add_token_word(word);
    return true;
}

void Tokenizer::add_separator_token(std::string &line, size_t &i)
{
    Token t;
    t.value = std::string(1, line[i]);

    if(line[i] == '{')
        t.type = TOKEN_LBRACE;
    else if (line[i] == '}')
        t.type = TOKEN_RBRACE;
    else if (line[i] == ';')
        t.type = TOKEN_SEMICOLON;
    this->_tokens.push_back(t);
    i++;
}


std::vector<Token>  Tokenizer::get_token(void)
{
    return (this->_tokens);
}

void Tokenizer::str_to_token(std::string line)
{
    size_t i = 0;
    while(i < line.length())
    {
        if (std::isspace(static_cast<unsigned char>(line[i]))) 
        {
            i++;
            continue;
        }
        if (line[i] == '#') 
        {
            break;
        }
        if (line[i] == '{' || line[i] == '}' || line[i] == ';')
        {
            add_separator_token(line, i);
            continue;
        }
        word_to_token(line, i);
    }
}

void Tokenizer::create_tokens()
{
    std::string p = file_path;
    std::string line;
    std::ifstream file(p.c_str());

    while (std::getline(file, line)) {
        str_to_token(line);
    }
    print_tokens();
    file.close();


}

Tokenizer::~Tokenizer()
{
}