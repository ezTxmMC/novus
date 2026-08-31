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
  void setBaseDir(const string &dir);

private:
  vector<Token> tokens;
  size_t pos = 0;
  string baseDir;
  map<string, VarSymbol> symbolTable;
  map<string, vector<MethodSignature>> methodRegistry;

  Token current();
  Token peek();
  void advance();

  // Top-level parsing
  unique_ptr<Node> parseTopLevel();
  unique_ptr<Node> parsePackage();
  unique_ptr<Node> parseImport();
  unique_ptr<Node> parseDefine();
  unique_ptr<Node> parseClass();
  unique_ptr<Node> parseEnum();
  unique_ptr<Node> parseInterface();
  void parseMembers(vector<FieldNode> &fields,
                    unique_ptr<MethodNode> &constructor,
                    vector<unique_ptr<MethodNode>> &methods);
  void skipAnnotation();
  AnnotationUse parseAnnotationUse();
  void skipBracedBlock();

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
  unique_ptr<Node> parsePrimary();
  unique_ptr<Node> parsePostfix(unique_ptr<Node> expr);
  void parseArguments(vector<unique_ptr<Node>> &out);
  unique_ptr<Node> buildStringLiteral(const string &raw);
  unique_ptr<Node> parseIf();
  unique_ptr<Node> parseWhile();
  unique_ptr<Node> parseFor();
  unique_ptr<Node> parseAssignment();

  unique_ptr<Node> parseOr();
  unique_ptr<Node> parseAnd();
  unique_ptr<Node> parseEquality();
  unique_ptr<Node> parseComparison();
  unique_ptr<Node> parseAdditive();
  unique_ptr<Node> parseMultiplicative();
  unique_ptr<Node> parseUnary();

  void parseBlock(vector<unique_ptr<Node>> &out);
  void error(const string &message);

  bool methodExists(const string &methodName, const vector<Type> &paramTypes);
  Type getVariableType(const string &varName);
};
