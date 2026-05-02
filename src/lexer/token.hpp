#pragma once
#include <string>

using namespace std;

enum class TokenType
{
    Package,

    Identifier,
    Number,
    String,

    Method,
    Var,
    Println,

    LBrace,
    RBrace,
    LParen,
    RParen,

    Equal,
    Plus,
    Minus,

    EndOfFile
};

struct Token
{
    TokenType type;
    string value;
};
