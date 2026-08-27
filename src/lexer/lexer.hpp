#pragma
#include "token.hpp"
#include <vector>

class Lexer {
public:
  Lexer(const string &input);
  vector<Token> tokenize();

private:
  string src;
  size_t pos = 0;

  char current();
  char peek();
  void advance();

  void skipWhitespace();

  Token readIdentifier();
  Token readNumber();
  Token readString();
};
