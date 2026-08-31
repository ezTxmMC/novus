#include "lexer.hpp"
#include "token.hpp"
#include <cctype>

Lexer::Lexer(const string &input) : src(input) {}

char Lexer::current() {
  if (pos >= src.size()) {
    return '\0';
  }
  return src[pos];
}

char Lexer::peek() {
  if (pos + 1 >= src.size()) {
    return '\0';
  }
  return src[pos + 1];
}

void Lexer::advance() {
  if (current() == '\n') {
    line++;
    column = 1;
  } else {
    column++;
  }
  pos++;
}

void Lexer::skipWhitespace() {
  while (isspace((unsigned char)current())) {
    advance();
  }
}

Token Lexer::make(TokenType type, const string &value) {
  return {type, value, startLine, startColumn};
}

Token Lexer::readIdentifier() {
  string value;

  while (isalnum((unsigned char)current()) || current() == '_') {
    value += current();
    advance();
  }

  if (value == "package") {
    return make(TokenType::Package, value);
  }

  if (value == "method") {
    return make(TokenType::Method, value);
  }

  if (value == "var") {
    return make(TokenType::Var, value);
  }

  if (value == "println") {
    return make(TokenType::Println, value);
  }

  if (value == "print") {
    return make(TokenType::Print, value);
  }

  if (value == "private") {
    return make(TokenType::Private, value);
  }

  if (value == "final") {
    return make(TokenType::Final, value);
  }

  if (value == "string") {
    return make(TokenType::StringType, value);
  }

  if (value == "str") {
    return make(TokenType::StringType, value);
  }

  if (value == "integer") {
    return make(TokenType::IntegerType, value);
  }

  if (value == "int") {
    return make(TokenType::IntegerType, value);
  }

  if (value == "boolean") {
    return make(TokenType::BooleanType, value);
  }

  if (value == "bool") {
    return make(TokenType::BooleanType, value);
  }

  if (value == "float") {
    return make(TokenType::FloatType, value);
  }

  if (value == "void") {
    return make(TokenType::Void, value);
  }

  if (value == "return") {
    return make(TokenType::Return, value);
  }

  if (value == "if") {
    return make(TokenType::If, value);
  }

  if (value == "else") {
    return make(TokenType::Else, value);
  }

  if (value == "while") {
    return make(TokenType::While, value);
  }

  if (value == "true") {
    return make(TokenType::True, value);
  }

  if (value == "false") {
    return make(TokenType::False, value);
  }

  if (value == "for") {
    return make(TokenType::For, value);
  }

  if (value == "in") {
    return make(TokenType::In, value);
  }

  if (value == "import") {
    return make(TokenType::Import, value);
  }

  if (value == "break") {
    return make(TokenType::Break, value);
  }

  if (value == "continue") {
    return make(TokenType::Continue, value);
  }

  return make(TokenType::Identifier, value);
}

Token Lexer::readNumber() {
  string value;

  while (isdigit((unsigned char)current())) {
    value += current();
    advance();
  }

  if (current() == '.' && isdigit((unsigned char)peek())) {
    value += current();
    advance();

    while (isdigit((unsigned char)current())) {
      value += current();
      advance();
    }

    return make(TokenType::Float, value);
  }

  return make(TokenType::Integer, value);
}

Token Lexer::readString() {
  advance(); // opening quote

  string value;

  while (current() != '"' && current() != '\0') {
    if (current() == '\\') {
      advance();
      switch (current()) {
      case 'n':
        value += '\n';
        break;
      case 't':
        value += '\t';
        break;
      case 'r':
        value += '\r';
        break;
      case '"':
        value += '"';
        break;
      case '\\':
        value += '\\';
        break;
      default:
        value += current();
        break;
      }
      advance();
    } else {
      value += current();
      advance();
    }
  }

  advance(); // closing quote

  return make(TokenType::String, value);
}

vector<Token> Lexer::tokenize() {
  vector<Token> tokens;

  while (current() != '\0') {
    skipWhitespace();

    if (current() == '\0') {
      break;
    }

    startLine = line;
    startColumn = column;

    // Line comment: // ... end of line
    if (current() == '/' && peek() == '/') {
      while (current() != '\n' && current() != '\0') {
        advance();
      }
      continue;
    }

    // Block comment: /* ... */
    if (current() == '/' && peek() == '*') {
      advance();
      advance();
      while (current() != '\0' && !(current() == '*' && peek() == '/')) {
        advance();
      }
      if (current() != '\0') {
        advance();
        advance();
      }
      continue;
    }

    if (isalpha((unsigned char)current())) {
      tokens.push_back(readIdentifier());
      continue;
    }

    if (isdigit(current())) {
      tokens.push_back(readNumber());
      continue;
    }

    if (current() == '"') {
      tokens.push_back(readString());
      continue;
    }

    if (current() == '=' && peek() == '=') {
      tokens.push_back(make(TokenType::EqualEqual, "=="));
      advance();
      advance();
      continue;
    }

    if (current() == '!' && peek() == '=') {
      tokens.push_back(make(TokenType::NotEqual, "!="));
      advance();
      advance();
      continue;
    }

    if (current() == '<' && peek() == '=') {
      tokens.push_back(make(TokenType::LessEqual, "<="));
      advance();
      advance();
      continue;
    }

    if (current() == '=' && peek() == '>') {
      tokens.push_back(make(TokenType::EqualLess, ">="));
      advance();
      advance();
      continue;
    }

    if (current() == '>' && peek() == '=') {
      tokens.push_back(make(TokenType::GreaterEqual, ">="));
      advance();
      advance();
      continue;
    }

    if (current() == '=' && peek() == '<') {
      tokens.push_back(make(TokenType::EqualGreater, "<="));
      advance();
      advance();
      continue;
    }

    if (current() == '&' && peek() == '&') {
      tokens.push_back(make(TokenType::AndAnd, "&&"));
      advance();
      advance();
      continue;
    }

    if (current() == '|' && peek() == '|') {
      tokens.push_back(make(TokenType::OrOr, "||"));
      advance();
      advance();
      continue;
    }

    switch (current()) {
    case '{':
      tokens.push_back(make(TokenType::LBrace, "{"));
      break;
    case '}':
      tokens.push_back(make(TokenType::RBrace, "}"));
      break;

    case '(':
      tokens.push_back(make(TokenType::LParen, "("));
      break;
    case ')':
      tokens.push_back(make(TokenType::RParen, ")"));
      break;

    case '[':
      tokens.push_back(make(TokenType::LBracket, "["));
      break;
    case ']':
      tokens.push_back(make(TokenType::RBracket, "]"));
      break;

    case '=':
      tokens.push_back(make(TokenType::Equal, "="));
      break;
    case '+':
      tokens.push_back(make(TokenType::Plus, "+"));
      break;
    case '-':
      tokens.push_back(make(TokenType::Minus, "-"));
      break;

    case '*':
      tokens.push_back(make(TokenType::Star, "*"));
      break;

    case '/':
      tokens.push_back(make(TokenType::Slash, "/"));
      break;

    case '%':
      tokens.push_back(make(TokenType::Percent, "%"));
      break;

    case ':':
      tokens.push_back(make(TokenType::Colon, ":"));
      break;

    case ';':
      tokens.push_back(make(TokenType::Semicolon, ";"));
      break;

    case ',':
      tokens.push_back(make(TokenType::Comma, ","));
      break;

    case '.':
      tokens.push_back(make(TokenType::Dot, "."));
      break;

    case '<':
      tokens.push_back(make(TokenType::LAngle, "<"));
      break;

    case '>':
      tokens.push_back(make(TokenType::RAngle, ">"));
      break;

    case '!':
      tokens.push_back(make(TokenType::Not, "!"));
      break;

    case '@':
      tokens.push_back(make(TokenType::At, "@"));
      break;

    default:
      advance();
      continue;
    }

    advance();
  }

  tokens.push_back(make(TokenType::EndOfFile, ""));
  return tokens;
}
