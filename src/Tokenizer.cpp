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

void Tokenizer::create_tokens()
{
    std::string p = file_path;

}

Tokenizer::~Tokenizer()
{
}