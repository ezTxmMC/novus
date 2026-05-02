#pragma once
#include <vector>
#include "../lexer/token.hpp"
#include "../ast/ast.hpp"

class Parser
{
public:
    Parser(const vector<Token> &tokens);

    unique_ptr<Node> parse();

private:
    vector<Token> tokens;
    size_t pos = 0;

    Token current();
    Token peek();
    void advance();

    unique_ptr<Node> parseTopLevel();
    unique_ptr<Node> parsePackage();
    unique_ptr<Node> parseMethod();
    unique_ptr<Node> parseStatement();
    unique_ptr<Node> parsePrint();
};
