#include <cctype>
#include "lexer.hpp"

Lexer::Lexer(const string &input) : src(input) {}

char Lexer::current()
{
    if (pos >= src.size())
    {
        return '\0';
    }
    return src[pos];
}

char Lexer::peek()
{
    if (pos + 1 >= src.size())
    {
        return '\0';
    }
    return src[pos + 1];
}

void Lexer::advance()
{
    pos++;
}

void Lexer::skipWhitespace()
{
    while (isspace((unsigned char)current()))
    {
        advance();
    }
}

Token Lexer::readIdentifier()
{
    string value;

    while (isalnum((unsigned char)current()) || current() == '_')
    {
        value += current();
        advance();
    }

    if (value == "package")
    {
        return {TokenType::Package, value};
    }

    if (value == "method")
    {
        return {TokenType::Method, value};
    }

    if (value == "var")
    {
        return {TokenType::Var, value};
    }

    if (value == "println")
    {
        return {TokenType::Println, value};
    }

    return {TokenType::Identifier, value};
}

Token Lexer::readNumber()
{
    string value;

    while (isdigit((unsigned char)current()))
    {
        value += current();
        advance();
    }

    return {TokenType::Number, value};
}

Token Lexer::readString()
{
    advance();

    string value;

    while (current() != '"' && current() != '\0')
    {
        value += current();
        advance();
    }

    advance();

    return {TokenType::String, value};
}

vector<Token> Lexer::tokenize()
{
    vector<Token> tokens;

    while (current() != '\0')
    {
        skipWhitespace();

        if (current() == '\0')
        {
            break;
        }

        if (isalpha((unsigned char)current()))
        {
            tokens.push_back(readIdentifier());
            continue;
        }

        if (isdigit(current()))
        {
            tokens.push_back(readNumber());
            continue;
        }

        if (current() == '"')
        {
            tokens.push_back(readString());
            continue;
        }

        switch (current())
        {
        case '{':
            tokens.push_back({TokenType::LBrace, "{"});
            break;
        case '}':
            tokens.push_back({TokenType::RBrace, "}"});
            break;

        case '(':
            tokens.push_back({TokenType::LParen, "("});
            break;
        case ')':
            tokens.push_back({TokenType::RParen, ")"});
            break;

        case '=':
            tokens.push_back({TokenType::Equal, "="});
            break;
        case '+':
            tokens.push_back({TokenType::Plus, "+"});
            break;
        case '-':
            tokens.push_back({TokenType::Minus, "-"});
            break;

        default:
            advance();
            continue;
        }

        advance();
    }

    tokens.push_back({TokenType::EndOfFile, ""});
    return tokens;
}
