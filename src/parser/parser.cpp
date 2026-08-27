#include "parser.hpp"
#include "../logger/logger.hpp"

Parser::Parser(const vector<Token> &tokens) : tokens(tokens) {}

Token Parser::current() {
  if (pos >= tokens.size()) {
    return {TokenType::EndOfFile, ""};
  }

  return tokens[pos];
}

Token Parser::peek() {
  if (pos + 1 >= tokens.size()) {
    return {TokenType::EndOfFile, ""};
  }

  return tokens[pos + 1];
}

void Parser::advance() { pos++; }

unique_ptr<Node> Parser::parse() {
  auto program = make_unique<ProgramNode>();

  while (current().type != TokenType::EndOfFile) {
    auto node = parseTopLevel();
    if (node) {
      program->methods.push_back(std::move(node));
    }
  }

  return program;
}

unique_ptr<Node> Parser::parseTopLevel() {
  if (current().type == TokenType::Package) {
    return parsePackage();
  }

  if (current().type == TokenType::Method) {
    return parseMethod();
  }

  if (current().type == TokenType::Println) {
    return parsePrint();
  }

  if (current().type == TokenType::Var) {
    return parseVariable();
  }

  advance();
  return nullptr;
}

std::unique_ptr<Node> Parser::parsePackage() {
  advance();

  if (current().type == TokenType::Identifier) {
    advance();
  }

  return nullptr;
}

unique_ptr<Node> Parser::parseMethod() {
  advance();

  if (current().type != TokenType::Identifier) {
    logError("Expected method name");
    return nullptr;
  }

  string methodName = current().value;
  advance();

  // Parse Parameters: method name(type param, type param)
  vector<ParameterNode> parameters;
  if (current().type == TokenType::LParen) {
    parameters = parseParameters();
  }

  // Parse Return Type: method name(): Type
  Type returnType("void");
  if (current().type == TokenType::Colon) {
    advance();
    returnType = parseType();
  }

  MethodSignature sig(methodName, parameters, returnType);
  methodRegistry[methodName].push_back(sig);

  auto method = make_unique<MethodNode>(methodName, returnType);
  method->parameters = parameters;

  if (current().type != TokenType::LBrace) {
    logError("Expected '{' after method signature");
    return nullptr;
  }

  advance();

  // Parse method body
  while (current().type != TokenType::RBrace &&
         current().type != TokenType::EndOfFile) {
    auto statement = parseStatement();
    if (statement) {
      method->body.push_back(std::move(statement));
    }
  }

  if (current().type != TokenType::RBrace) {
    logError("Expected '}' at end of method");
    return nullptr;
  }

  advance();

  return method;
}

vector<ParameterNode> Parser::parseParameters() {
  vector<ParameterNode> params;

  if (current().type != TokenType::LParen) {
    return params;
  }

  advance();

  while (current().type != TokenType::RParen &&
         current().type != TokenType::EndOfFile) {
    // Parse type
    Type paramType = parseType();

    // Parse parameter name
    if (current().type != TokenType::Identifier) {
      logError("Expected parameter name");
      return params;
    }

    string paramName = current().value;
    advance();

    params.push_back(ParameterNode(paramType, paramName));

    if (current().type == TokenType::Comma) {
      advance();
    }
  }

  if (current().type != TokenType::RParen) {
    logError("Expected ')' after parameters");
    return params;
  }

  advance();

  return params;
}

Type Parser::parseType() {
  string typeName;

  if (current().type == TokenType::StringType) {
    typeName = "string";
    advance();
  } else if (current().type == TokenType::IntegerType) {
    typeName = "integer";
    advance();
  } else if (current().type == TokenType::FloatType) {
    typeName = "float";
    advance();
  } else if (current().type == TokenType::Void) {
    typeName = "void";
    advance();
  } else if (current().type == TokenType::Identifier) {
    typeName = current().value;
    advance();
  } else {
    logError("Expected type");
    return Type("unknown");
  }

  if (current().type == TokenType::LAngle) {
    advance();
    parseType();

    if (current().type == TokenType::RAngle) {
      advance();
    }
    return Type(typeName, true);
  }

  return Type(typeName, false);
}

unique_ptr<Node> Parser::parseStatement() {
  if (current().type == TokenType::Package) {
    advance();
    advance();
    return parseStatement();
  }

  if (current().type == TokenType::Return) {
    return parseReturn();
  }

  if (current().type == TokenType::Println) {
    return parsePrint();
  }

  if (current().type == TokenType::Var) {
    return parseVariable();
  }

  if (current().type == TokenType::Identifier) {
    return parseExpression();
  }

  advance();
  return nullptr;
}

unique_ptr<Node> Parser::parseVariable() {
  bool isPrivate = false;
  bool isFinal = false;

  while (current().type == TokenType::Private ||
         current().type == TokenType::Final) {
    if (current().type == TokenType::Private)
      isPrivate = true;
    if (current().type == TokenType::Final)
      isFinal = true;
    advance();
  }

  if (current().type != TokenType::Var) {
    logError("Expected 'var'");
    return nullptr;
  }

  advance();

  // Parse variable name
  if (current().type != TokenType::Identifier) {
    logError("Expected variable name");
    return nullptr;
  }

  string varName = current().value;
  advance();

  // Parse type
  Type varType = Type("inferred");
  if (current().type == TokenType::Colon) {
    advance();
    varType = parseType();
  }

  // Parse assignment
  unique_ptr<Node> value = nullptr;
  if (current().type == TokenType::Equal ||
      current().type == TokenType::Assign) {
    advance();
    value = parseExpression();
  }

  symbolTable[varName] = VarSymbol(varType, varName);

  return make_unique<VariableNode>(varType, varName, std::move(value));
}

unique_ptr<Node> Parser::parseExpression() {
  auto left = parsePrimary();
  if (!left) {
    return nullptr;
  }

  auto result = parseBinaryOp(std::move(left));

  if (isBinaryOperator(current().type)) {
    return parseBinaryOp(std::move(result));
  }

  return result;
}

unique_ptr<Node> Parser::parsePrint() {
  advance();

  if (current().type == TokenType::String) {
    string value = current().value;
    advance();
    return make_unique<PrintNode>(value);
  }

  if (current().type == TokenType::Identifier) {
    auto expr = parseExpression();
    return expr;
  }

  logError("Expected string or identifier after println");
  return nullptr;
}

bool Parser::methodExists(const string &methodName,
                          const vector<Type> &paramTypes) {
  auto it = methodRegistry.find(methodName);
  if (it == methodRegistry.end())
    return false;

  for (const auto &sig : it->second) {
    if (sig.matches(paramTypes))
      return true;
  }

  return false;
}

Type Parser::getVariableType(const string &varName) {
  auto it = symbolTable.find(varName);
  if (it != symbolTable.end()) {
    return it->second.type;
  }
  return Type("unknown");
}

unique_ptr<Node> Parser::parseReturn() {
  advance();

  auto value = parseExpression();
  return make_unique<ReturnNode>(std::move(value));
}

unique_ptr<Node> Parser::parsePrimary() {
  if (current().type == TokenType::String) {
    string value = current().value;
    advance();
    return make_unique<StringLiteralNode>(value);
  }

  if (current().type == TokenType::Integer) {
    int value = stoi(current().value);
    advance();
    return make_unique<IntegerLiteralNode>(value);
  }

  if (current().type == TokenType::Float) {
    double value = stod(current().value);
    advance();
    return make_unique<FloatLiteralNode>(value);
  }

  if (current().type == TokenType::LParen) {
    advance();
    auto expr = parseExpression();
    if (current().type == TokenType::RParen) {
      advance();
    }
    return expr;
  }

  if (current().type == TokenType::Identifier) {
    string name = current().value;
    advance();

    if (current().type == TokenType::LParen) {
      advance();
      auto methodCall = make_unique<MethodCallNode>(name, name);

      while (current().type != TokenType::RParen &&
             current().type != TokenType::EndOfFile) {
        auto arg = parseExpression();
        if (arg) {
          methodCall->arguments.push_back(std::move(arg));
        }

        if (current().type == TokenType::Comma) {
          advance();
        }
      }

      if (current().type != TokenType::RParen) {
        logError("Expected ')' after method call");
        return nullptr;
      }

      advance();
      return methodCall;
    }

    return make_unique<IdentifierNode>(name);
  }

  logError("Expected primary expression");
  return nullptr;
}

bool Parser::isBinaryOperator(TokenType type) {
  return type == TokenType::Plus || type == TokenType::Minus ||
         type == TokenType::Star || type == TokenType::Slash ||
         type == TokenType::Percent;
}

unique_ptr<Node> Parser::parseBinaryOp(unique_ptr<Node> left) {
  if (!isBinaryOperator(current().type)) {
    return left;
  }

  char op = current().value[0];
  advance();

  auto right = parsePrimary();
  if (!right) {
    return left;
  }

  auto result =
      make_unique<BinaryOpNode>(std::move(left), std::move(right), op);

  while (isBinaryOperator(current().type)) {
    op = current().value[0];
    advance();

    right = parsePrimary();
    if (!right) {
      return result;
    }

    result = make_unique<BinaryOpNode>(std::move(result), std::move(right), op);
  }

  return result;
}
