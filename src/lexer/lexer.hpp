#pragma once
#include "token.hpp"
#include <vector>

class Lexer {
public:
  Lexer(const string &input);
  vector<Token> tokenize();

private:
  string src;
  size_t pos = 0;
  int line = 1;
  int column = 1;
  int startLine = 1;
  int startColumn = 1;

  char current();
  char peek();
  void advance();

  void skipWhitespace();

  Token make(TokenType type, const string &value);
  Token readIdentifier();
  Token readNumber();
  Token readString();
};
