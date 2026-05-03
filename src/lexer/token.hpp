#pragma once
#include <string>

using namespace std;

enum class TokenType
{
    Package,

    Identifier,
    Integer,
    Float,
    String,

    Private,
    Final,

    // Type Keywords
    StringType,
    IntegerType,
    FloatType,
    ArrayType,

    // Operators & Delimiters
    Colon,
    Comma,
    Dot,
    LAngle,
    RAngle,
    Assign,

    // Control Flow
    If,
    For,
    While,
    Return,

    // Modifiers
    Void,

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
    Star,
    Slash,
    Percent,

    EndOfFile
};

struct Token
{
    TokenType type;
    string value;
};
