#include <iostream>
#include "parser.hpp"
#include "../logger/logger.hpp"

Parser::Parser(const vector<Token> &tokens) : tokens(tokens) {}

Token Parser::current()
{
    if (pos >= tokens.size())
    {
        return {TokenType::EndOfFile, ""};
    }

    return Parser::peek();
}

Token Parser::peek()
{
    if (pos + 1 >= tokens.size())
    {
        return {TokenType::EndOfFile, ""};
    }

    return tokens[pos + 1];
}

void Parser::advance()
{
    pos++;
}

unique_ptr<Node> Parser::parse()
{
    auto program = make_unique<ProgramNode>();

    while (current().type != TokenType::EndOfFile)
    {
        if (current().type == TokenType::Method)
        {
            program->methods.push_back(parseMethod());
            continue;
        }

        if (current().type == TokenType::Package)
        {
            advance();
            advance();
            continue;
        }

        advance();
    }

    return program;
}

unique_ptr<Node> Parser::parseTopLevel()
{
    if (current().type == TokenType::Package)
    {
        return parsePackage();
    }

    if (current().type == TokenType::Method)
    {
        return parseMethod();
    }

    if (current().type == TokenType::Println)
    {
        return parsePrint();
    }

    advance();
    return nullptr;
}

std::unique_ptr<Node> Parser::parsePackage()
{
    advance();

    if (current().type == TokenType::Identifier)
    {
        advance();
    }

    return nullptr;
}

unique_ptr<Node> Parser::parseMethod()
{
    advance();

    if (current().type != TokenType::Identifier)
    {
        return nullptr;
    }

    string name = current().value;
    advance();

    if (current().type != TokenType::LBrace)
    {
        return nullptr;
    }

    advance();

    auto method = make_unique<MethodNode>(name);

    while (current().type != TokenType::RBrace &&
           current().type != TokenType::EndOfFile)
    {
        auto statement = parseStatement();
        if (statement)
        {
            method->body.push_back(move(statement));
        }
    }

    advance();

    return method;
}

unique_ptr<Node> Parser::parseStatement()
{
    if (current().type == TokenType::Package)
    {
        advance();
        advance();

        return parseStatement();
    }

    if (current().type == TokenType::Println)
    {
        return parsePrint();
    }

    logError("Unknown statement.");
    return nullptr;
}

unique_ptr<Node> Parser::parsePrint()
{
    advance();

    if (current().type != TokenType::String)
    {
        logError("Expected string after println");
        return nullptr;
    }

    string value = current().value;

    advance();

    return make_unique<PrintNode>(value);
}
