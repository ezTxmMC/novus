#pragma once
#include "../ast/ast.hpp"
#include "../lexer/token.hpp"
#include <map>
#include <vector>

using namespace std;

struct MethodSignature {
  string name;
  vector<ParameterNode> parameters;
  Type returnType;

  MethodSignature(const string &n, const vector<ParameterNode> &p,
                  const Type &rt)
      : name(n), parameters(p), returnType(rt) {}

  bool matches(const vector<Type> &paramTypes) const {
    if (parameters.size() != paramTypes.size())
      return false;

    for (size_t i = 0; i < parameters.size(); i++) {
      if (!(parameters[i].type == paramTypes[i])) {
        return false;
      }
    }
    return true;
  }
};

struct VarSymbol {
  Type type;
  string name;

  VarSymbol() : type("unknown"), name("") {}
  VarSymbol(const Type &t, const string &n) : type(t), name(n) {}
};

class Parser {
public:
  Parser(const vector<Token> &tokens);

  unique_ptr<Node> parse();

private:
  vector<Token> tokens;
  size_t pos = 0;
  map<string, VarSymbol> symbolTable;
  map<string, vector<MethodSignature>> methodRegistry;

  Token current();
  Token peek();
  void advance();

  // Top-level parsing
  unique_ptr<Node> parseTopLevel();
  unique_ptr<Node> parsePackage();

  // Method parsing
  unique_ptr<Node> parseMethod();
  vector<ParameterNode> parseParameters();
  Type parseType();

  // Statement parsing
  unique_ptr<Node> parseStatement();
  unique_ptr<Node> parseVariable();
  unique_ptr<Node> parseExpression();
  unique_ptr<Node> parseReturn();
  unique_ptr<Node> parsePrint();
  unique_ptr<Node> parseBinaryOp(unique_ptr<Node> left);
  unique_ptr<Node> parsePrimary();

  bool methodExists(const string &methodName, const vector<Type> &paramTypes);
  Type getVariableType(const string &varName);
  bool isBinaryOperator(TokenType type);
};
