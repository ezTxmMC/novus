#include "parser.hpp"
#include "../lexer/lexer.hpp"
#include "../logger/logger.hpp"
#include "../util/file.hpp"
#include <filesystem>
#include <set>

// Files already imported in this process (cycle/duplicate guard)
static set<string> importedFiles;
#include <string>

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
      // An import returns a whole ProgramNode - flatten it in.
      if (auto sub = dynamic_cast<ProgramNode *>(node.get())) {
        for (auto &method : sub->methods) {
          program->methods.push_back(std::move(method));
        }
      } else {
        program->methods.push_back(std::move(node));
      }
    }
  }

  return program;
}

void Parser::setBaseDir(const string &dir) { baseDir = dir; }

unique_ptr<Node> Parser::parseImport() {
  advance(); // 'import'

  // Future module form: import http - accepted and ignored for now
  if (current().type == TokenType::Identifier) {
    advance();
    return nullptr;
  }

  if (current().type != TokenType::String) {
    error("Expected file path string after 'import'");
    return nullptr;
  }

  string rawPath = current().value;
  advance();

  filesystem::path resolved =
      filesystem::path(baseDir.empty() ? "." : baseDir) / rawPath;
  string canonical;
  try {
    canonical = filesystem::weakly_canonical(resolved).string();
  } catch (...) {
    canonical = resolved.string();
  }

  if (importedFiles.count(canonical)) {
    return nullptr; // already imported
  }
  importedFiles.insert(canonical);

  if (!filesystem::exists(canonical)) {
    error("Cannot import '" + rawPath + "' (resolved to " + canonical + ")");
    return nullptr;
  }

  Lexer importLexer(readFile(canonical));
  auto importTokens = importLexer.tokenize();

  Parser importParser(importTokens);
  importParser.setBaseDir(filesystem::path(canonical).parent_path().string());
  return importParser.parse();
}

unique_ptr<Node> Parser::parseTopLevel() {
  if (current().type == TokenType::Package) {
    return parsePackage();
  }

  if (current().type == TokenType::At) {
    vector<AnnotationUse> uses;
    while (current().type == TokenType::At) {
      uses.push_back(parseAnnotationUse());
    }
    auto node = parseTopLevel();
    if (node) {
      if (auto m = dynamic_cast<MethodNode *>(node.get())) {
        m->annotations = std::move(uses);
      }
    }
    return node;
  }

  if (current().type == TokenType::Import) {
    return parseImport();
  }

  if (current().type == TokenType::Identifier && current().value == "define") {
    return parseDefine();
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

  // Top-level constant: [private] [final] NAME = expr
  if (current().type == TokenType::Private ||
      current().type == TokenType::Final) {
    while (current().type == TokenType::Private ||
           current().type == TokenType::Final) {
      advance();
    }
    if (current().type == TokenType::Var) {
      return parseVariable();
    }
    if (current().type == TokenType::Identifier &&
        peek().type == TokenType::Equal) {
      string name = current().value;
      advance();
      advance();
      return make_unique<VariableNode>(Type("inferred"), name,
                                       parseExpression());
    }
    return nullptr;
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

void Parser::skipBracedBlock() {
  if (current().type != TokenType::LBrace) {
    return;
  }
  int depth = 0;
  while (current().type != TokenType::EndOfFile) {
    if (current().type == TokenType::LBrace) {
      depth++;
    }
    if (current().type == TokenType::RBrace) {
      depth--;
      if (depth == 0) {
        advance();
        return;
      }
    }
    advance();
  }
}

AnnotationUse Parser::parseAnnotationUse() {
  advance(); // '@'
  AnnotationUse use;

  if (current().type == TokenType::Identifier) {
    use.name = current().value;
    advance();
  }

  if (current().type == TokenType::LBrace) {
    advance();
    while (current().type != TokenType::RBrace &&
           current().type != TokenType::EndOfFile) {
      if (current().type == TokenType::Identifier &&
          peek().type == TokenType::Equal) {
        string key = current().value;
        advance();
        advance();
        if (current().type == TokenType::String) {
          use.args.emplace_back(key, current().value);
          advance();
        } else {
          parseExpression(); // parsed, value ignored (non-string)
        }
      } else {
        advance();
      }
      if (current().type == TokenType::Comma) {
        advance();
      }
    }
    if (current().type == TokenType::RBrace) {
      advance();
    }
  }

  return use;
}

void Parser::skipAnnotation() {
  advance(); // '@'
  if (current().type == TokenType::Identifier) {
    advance();
  }
  skipBracedBlock(); // optional @Name{...} arguments
}

unique_ptr<Node> Parser::parseDefine() {
  advance(); // 'define'

  string kind = current().value;
  advance();

  if (kind == "class") {
    return parseClass();
  }

  if (kind == "enum") {
    return parseEnum();
  }

  if (kind == "abstract") {
    auto cls = parseClass();
    if (cls) {
      static_cast<ClassNode *>(cls.get())->isAbstract = true;
    }
    return cls;
  }

  if (kind == "interface") {
    return parseInterface();
  }

  if (kind == "annotation") {
    auto def = make_unique<AnnotationDefNode>();
    if (current().type == TokenType::Identifier) {
      def->name = current().value;
      advance();
    }
    while (current().type != TokenType::LBrace &&
           current().type != TokenType::EndOfFile) {
      advance();
    }
    skipBracedBlock();
    return def;
  }

  // enum / interface / abstract / annotation: parsed & skipped for now
  while (current().type != TokenType::LBrace &&
         current().type != TokenType::EndOfFile) {
    advance();
  }
  skipBracedBlock();
  return nullptr;
}

unique_ptr<Node> Parser::parseClass() {
  if (current().type != TokenType::Identifier) {
    error("Expected class name");
    return nullptr;
  }

  auto cls = make_unique<ClassNode>();
  cls->name = current().value;
  advance();

  // primary-constructor style params: parsed, ignored in v1
  if (current().type == TokenType::LParen) {
    parseParameters();
  }

  if (current().type == TokenType::Identifier && current().value == "based") {
    advance();
    if (current().type == TokenType::Identifier) {
      cls->baseName = current().value;
      advance();
    }
  }

  if (current().type != TokenType::LBrace) {
    error("Expected '{' after class header");
    return nullptr;
  }
  advance();

  parseMembers(cls->fields, cls->constructor, cls->methods);

  if (current().type != TokenType::RBrace) {
    error("Expected '}' at end of class");
    return nullptr;
  }
  advance();

  return cls;
}

void Parser::parseMembers(vector<FieldNode> &fields,
                          unique_ptr<MethodNode> &constructor,
                          vector<unique_ptr<MethodNode>> &methods) {
  vector<AnnotationUse> pendingAnnotations;

  while (current().type != TokenType::RBrace &&
         current().type != TokenType::EndOfFile) {
    if (current().type == TokenType::At) {
      pendingAnnotations.push_back(parseAnnotationUse());
      continue;
    }

    if (current().type == TokenType::Semicolon) {
      advance();
      continue;
    }

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

    if (current().type == TokenType::Identifier &&
        current().value == "abstract" && peek().type == TokenType::Method) {
      advance(); // 'abstract'
      advance(); // 'method'
      if (current().type != TokenType::Identifier) {
        error("Expected method name after 'abstract method'");
        continue;
      }
      auto m = make_unique<MethodNode>(current().value);
      advance();
      if (current().type == TokenType::LParen) {
        m->parameters = parseParameters();
      }
      if (current().type == TokenType::Colon) {
        advance();
        m->returnType = parseType();
      }
      m->isAbstract = true;
      m->annotations = std::move(pendingAnnotations);
      pendingAnnotations.clear();
      methods.push_back(std::move(m));
      continue;
    }

    if (current().type == TokenType::Method) {
      auto m = parseMethod();
      if (m) {
        methods.emplace_back(static_cast<MethodNode *>(m.release()));
        methods.back()->annotations = std::move(pendingAnnotations);
      }
      pendingAnnotations.clear();
      continue;
    }

    if (current().type == TokenType::Identifier &&
        current().value == "construct") {
      advance();
      auto ctor = make_unique<MethodNode>("construct");
      ctor->parameters = parseParameters();
      parseBlock(ctor->body);
      constructor = std::move(ctor);
      pendingAnnotations.clear();
      continue;
    }

    // Methodless member: name(params)[: type] [{ body }]  (interface style)
    if (current().type == TokenType::Identifier &&
        peek().type == TokenType::LParen) {
      auto m = make_unique<MethodNode>(current().value);
      advance();
      m->parameters = parseParameters();
      if (current().type == TokenType::Colon) {
        advance();
        m->returnType = parseType();
      }
      if (current().type == TokenType::LBrace) {
        parseBlock(m->body);
      } else {
        m->isAbstract = true;
      }
      m->annotations = std::move(pendingAnnotations);
      pendingAnnotations.clear();
      methods.push_back(std::move(m));
      continue;
    }

    // field: Type name [: get[, set]]
    Type fieldType = parseType();
    if (current().type != TokenType::Identifier) {
      error("Expected field name");
      advance();
      continue;
    }

    FieldNode field;
    field.type = fieldType;
    field.name = current().value;
    field.isPrivate = isPrivate;
    field.isFinal = isFinal;
    advance();

    if (current().type == TokenType::Colon) {
      advance();
      while (current().type == TokenType::Identifier &&
             (current().value == "get" || current().value == "set")) {
        if (current().value == "get")
          field.hasGet = true;
        else
          field.hasSet = true;
        advance();
        if (current().type == TokenType::Comma) {
          advance();
        } else {
          break;
        }
      }
    }

    fields.push_back(field);
    pendingAnnotations.clear();
  }
}

unique_ptr<Node> Parser::parseInterface() {
  if (current().type != TokenType::Identifier) {
    error("Expected interface name");
    return nullptr;
  }

  auto iface = make_unique<InterfaceNode>();
  iface->name = current().value;
  advance();

  if (current().type != TokenType::LBrace) {
    error("Expected '{' after interface name");
    return nullptr;
  }
  advance();

  while (current().type != TokenType::RBrace &&
         current().type != TokenType::EndOfFile) {
    if (current().type == TokenType::At) {
      skipAnnotation();
      continue;
    }
    if (current().type == TokenType::Method) {
      advance();
      continue;
    }
    if (current().type == TokenType::Identifier &&
        peek().type == TokenType::LParen) {
      iface->methodNames.push_back(current().value);
      advance();
      parseParameters();
      if (current().type == TokenType::Colon) {
        advance();
        parseType();
      }
      skipBracedBlock(); // tolerate optional default body
      continue;
    }
    advance();
  }

  if (current().type != TokenType::RBrace) {
    error("Expected '}' at end of interface");
    return nullptr;
  }
  advance();

  return iface;
}

unique_ptr<Node> Parser::parseEnum() {
  if (current().type != TokenType::Identifier) {
    error("Expected enum name");
    return nullptr;
  }

  auto en = make_unique<EnumNode>();
  en->name = current().value;
  advance();

  if (current().type != TokenType::LBrace) {
    error("Expected '{' after enum name");
    return nullptr;
  }
  advance();

  // Constants: NAME[(args)], ... terminated by ';' (or directly '}')
  while (current().type == TokenType::Identifier &&
         (peek().type == TokenType::Comma || peek().type == TokenType::LParen ||
          peek().type == TokenType::Semicolon ||
          peek().type == TokenType::RBrace)) {
    EnumConstant constant;
    constant.name = current().value;
    advance();

    if (current().type == TokenType::LParen) {
      advance();
      parseArguments(constant.args);
    }

    en->constants.push_back(std::move(constant));

    if (current().type == TokenType::Comma) {
      advance();
    }
    if (current().type == TokenType::Semicolon) {
      advance();
      break;
    }
  }

  parseMembers(en->fields, en->constructor, en->methods);

  if (current().type != TokenType::RBrace) {
    error("Expected '}' at end of enum");
    return nullptr;
  }
  advance();

  return en;
}

unique_ptr<Node> Parser::parseMethod() {
  advance();

  if (current().type != TokenType::Identifier) {
    error("Expected method name");
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

  parseBlock(method->body);

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
      error("Expected parameter name");
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
    error("Expected ')' after parameters");
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
  } else if (current().type == TokenType::BooleanType) {
    typeName = "bool";
    advance();
  } else if (current().type == TokenType::Void) {
    typeName = "void";
    advance();
  } else if (current().type == TokenType::Identifier) {
    typeName = current().value;
    advance();
  } else {
    error("Expected type");
    return Type("unknown");
  }

  if (current().type == TokenType::LAngle) {
    advance();
    parseType();

    while (current().type == TokenType::Comma) {
      advance();
      parseType();
    }

    if (current().type == TokenType::RAngle) {
      advance();
    }
    return Type(typeName, typeName == "array");
  }

  return Type(typeName, false);
}

unique_ptr<Node> Parser::parseStatement() {
  if (current().type == TokenType::Semicolon) {
    advance();
    return nullptr;
  }

  if (current().type == TokenType::Package) {
    advance();
    advance();
    return parseStatement();
  }

  if (current().type == TokenType::Return) {
    return parseReturn();
  }

  if (current().type == TokenType::If) {
    return parseIf();
  }

  if (current().type == TokenType::While) {
    return parseWhile();
  }

  if (current().type == TokenType::For) {
    return parseFor();
  }

  if (current().type == TokenType::Break) {
    advance();
    return make_unique<BreakNode>();
  }

  if (current().type == TokenType::Continue) {
    advance();
    return make_unique<ContinueNode>();
  }

  if (current().type == TokenType::Println) {
    return parsePrint();
  }

  if (current().type == TokenType::Var) {
    return parseVariable();
  }

  if (current().type == TokenType::Identifier &&
      peek().type == TokenType::Equal) {
    return parseAssignment();
  }

  if (current().type == TokenType::Identifier) {
    auto expr = parseExpression();

    // Property assignment: obj.field = value / this.field = value
    if (current().type == TokenType::Equal) {
      if (auto memberAccess = dynamic_cast<MemberAccessNode *>(expr.get())) {
        auto id = dynamic_cast<IdentifierNode *>(memberAccess->target.get());
        if (!id) {
          error("Unsupported assignment target");
          return nullptr;
        }
        advance();
        auto assignment = make_unique<PropertyAssignmentNode>();
        assignment->objectName = id->name;
        assignment->propertyName = memberAccess->propertyName;
        assignment->value = parseExpression();
        return assignment;
      }
    }

    // Index assignment: name[index] = value
    if (current().type == TokenType::Equal) {
      auto indexAccess = dynamic_cast<IndexAccessNode *>(expr.get());
      if (!indexAccess) {
        error("Invalid assignment target");
        return nullptr;
      }

      auto identifier =
          dynamic_cast<IdentifierNode *>(indexAccess->target.get());
      if (!identifier) {
        error("Only variables support index assignment");
        return nullptr;
      }

      advance(); // '='

      auto assignment = make_unique<IndexAssignmentNode>();
      assignment->name = identifier->name;
      assignment->index = std::move(indexAccess->index);
      assignment->value = parseExpression();
      return assignment;
    }

    return expr;
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
    error("Expected 'var'");
    return nullptr;
  }

  advance();

  // Parse variable name
  if (current().type != TokenType::Identifier) {
    error("Expected variable name");
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

unique_ptr<Node> Parser::parsePrint() {
  advance();
  return parseExpression();
}

unique_ptr<Node> Parser::parseExpression() { return parseOr(); }

unique_ptr<Node> Parser::parseOr() {
  auto left = parseAnd();
  while (current().type == TokenType::OrOr) {
    advance();
    auto right = parseAnd();
    left = make_unique<BinaryOpNode>(std::move(left), std::move(right), "||");
  }
  return left;
}

unique_ptr<Node> Parser::parseAnd() {
  auto left = parseEquality();
  while (current().type == TokenType::AndAnd) {
    advance();
    auto right = parseEquality();
    left = make_unique<BinaryOpNode>(std::move(left), std::move(right), "&&");
  }
  return left;
}

unique_ptr<Node> Parser::parseEquality() {
  auto left = parseComparison();
  while (current().type == TokenType::EqualEqual ||
         current().type == TokenType::NotEqual) {
    string op = current().value;
    advance();
    auto right = parseComparison();
    left = make_unique<BinaryOpNode>(std::move(left), std::move(right), op);
  }
  return left;
}

unique_ptr<Node> Parser::parseComparison() {
  auto left = parseAdditive();

  while (true) {
    string op;
    switch (current().type) {
    case TokenType::LAngle:
      op = "<";
      break;
    case TokenType::RAngle:
      op = ">";
      break;
    case TokenType::LessEqual:
    case TokenType::EqualGreater: // '=<'
      op = "<=";
      break;
    case TokenType::GreaterEqual:
    case TokenType::EqualLess: // '=>'
      op = ">=";
      break;
    default:
      return left;
    }

    advance();
    auto right = parseAdditive();
    left = make_unique<BinaryOpNode>(std::move(left), std::move(right), op);
  }
}

unique_ptr<Node> Parser::parseAdditive() {
  auto left = parseMultiplicative();
  while (current().type == TokenType::Plus ||
         current().type == TokenType::Minus) {
    string op = current().value;
    advance();
    auto right = parseMultiplicative();
    left = make_unique<BinaryOpNode>(std::move(left), std::move(right), op);
  }
  return left;
}

unique_ptr<Node> Parser::parseMultiplicative() {
  auto left = parseUnary();
  while (current().type == TokenType::Star ||
         current().type == TokenType::Slash ||
         current().type == TokenType::Percent) {
    string op = current().value;
    advance();
    auto right = parseUnary();
    left = make_unique<BinaryOpNode>(std::move(left), std::move(right), op);
  }
  return left;
}

unique_ptr<Node> Parser::parseUnary() {
  if (current().type == TokenType::Not) {
    advance();
    return make_unique<UnaryOpNode>("!", parseUnary());
  }

  return parsePrimary();
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
    return parsePostfix(buildStringLiteral(value));
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

  if (current().type == TokenType::True) {
    advance();
    return make_unique<BoolLiteralNode>(true);
  }

  if (current().type == TokenType::False) {
    advance();
    return make_unique<BoolLiteralNode>(false);
  }

  if (current().type == TokenType::LBrace) {
    advance();
    auto mapLiteral = make_unique<MapLiteralNode>();

    while (current().type != TokenType::RBrace &&
           current().type != TokenType::EndOfFile) {
      auto key = parseExpression();

      if (current().type != TokenType::Colon) {
        error("Expected ':' after map key");
        return nullptr;
      }
      advance();

      auto value = parseExpression();
      if (!key || !value) {
        break; // parse error - avoid infinite loop
      }
      mapLiteral->entries.emplace_back(std::move(key), std::move(value));

      if (current().type == TokenType::Comma) {
        advance();
      }
    }

    if (current().type != TokenType::RBrace) {
      error("Expected '}' after map literal");
      return nullptr;
    }
    advance();

    return parsePostfix(std::move(mapLiteral));
  }

  if (current().type == TokenType::LBracket) {
    advance();
    auto arrayLiteral = make_unique<ArrayLiteralNode>();

    while (current().type != TokenType::RBracket &&
           current().type != TokenType::EndOfFile) {
      auto element = parseExpression();
      if (!element) {
        break; // parse error - avoid infinite loop
      }
      arrayLiteral->elements.push_back(std::move(element));

      if (current().type == TokenType::Comma) {
        advance();
      }
    }

    if (current().type != TokenType::RBracket) {
      error("Expected ']' after array literal");
      return nullptr;
    }
    advance();

    return parsePostfix(std::move(arrayLiteral));
  }

  if (current().type == TokenType::LParen) {
    advance();
    auto expr = parseExpression();
    if (current().type == TokenType::RParen) {
      advance();
    }
    return parsePostfix(std::move(expr));
  }

  if (current().type == TokenType::Identifier) {
    string name = current().value;
    advance();

    // Object literal: ClassName{field=value, ...}
    if (current().type == TokenType::LBrace) {
      advance();
      auto obj = make_unique<ObjectLiteralNode>();
      obj->className = name;

      while (current().type != TokenType::RBrace &&
             current().type != TokenType::EndOfFile) {
        if (current().type != TokenType::Identifier) {
          error("Expected field name in object literal");
          break;
        }
        string fieldName = current().value;
        advance();

        if (current().type != TokenType::Equal) {
          error("Expected '=' after field name in object literal");
          break;
        }
        advance();

        auto value = parseExpression();
        if (!value) {
          break;
        }
        obj->fields.emplace_back(fieldName, std::move(value));

        if (current().type == TokenType::Comma) {
          advance();
        }
      }

      if (current().type != TokenType::RBrace) {
        error("Expected '}' after object literal");
        return nullptr;
      }
      advance();

      return parsePostfix(std::move(obj));
    }

    // Free method call: name(args)
    if (current().type == TokenType::LParen) {
      advance();
      auto methodCall = make_unique<MethodCallNode>(name, name);
      parseArguments(methodCall->arguments);
      return parsePostfix(std::move(methodCall));
    }

    return parsePostfix(make_unique<IdentifierNode>(name));
  }

  error("Expected primary expression");
  return nullptr;
}

void Parser::parseBlock(vector<unique_ptr<Node>> &out) {
  if (current().type != TokenType::LBrace) {
    error("Expected '{'");
    return;
  }
  advance();

  while (current().type != TokenType::RBrace &&
         current().type != TokenType::EndOfFile) {
    auto statement = parseStatement();
    if (statement) {
      out.push_back(std::move(statement));
    }
  }

  if (current().type != TokenType::RBrace) {
    error("Expected '}'");
    return;
  }
  advance();
}

unique_ptr<Node> Parser::parseIf() {
  advance(); // 'if'

  if (current().type != TokenType::LParen) {
    error("Expected '(' after 'if'");
    return nullptr;
  }
  advance();

  auto node = make_unique<IfNode>(parseExpression());

  if (current().type != TokenType::RParen) {
    error("Expected ')' after condition");
    return nullptr;
  }
  advance();

  parseBlock(node->thenBranch);

  if (current().type == TokenType::Else) {
    advance();
    if (current().type == TokenType::If) {
      // 'else if' => single If node as the else branch
      node->elseBranch.push_back(parseIf());
    } else {
      parseBlock(node->elseBranch);
    }
  }

  return node;
}

unique_ptr<Node> Parser::parseWhile() {
  advance(); // 'while'

  if (current().type != TokenType::LParen) {
    error("Expected '(' after 'while'");
    return nullptr;
  }
  advance();

  auto node = make_unique<WhileNode>(parseExpression());

  if (current().type != TokenType::RParen) {
    error("Expected ')' after condition");
    return nullptr;
  }
  advance();

  parseBlock(node->body);
  return node;
}

unique_ptr<Node> Parser::parseAssignment() {
  string name = current().value;
  advance(); // name
  advance(); // '='
  return make_unique<AssignmentNode>(name, parseExpression());
}

void Parser::error(const string &message) {
  logError(message + " (line " + to_string(current().line) + ", column " +
           to_string(current().column) + ", got '" + current().value + "')");
}

void Parser::parseArguments(vector<unique_ptr<Node>> &out) {
  while (current().type != TokenType::RParen &&
         current().type != TokenType::EndOfFile) {
    auto arg = parseExpression();
    if (!arg) {
      break; // parse error - no token consumed, avoid infinite loop
    }
    out.push_back(std::move(arg));

    if (current().type == TokenType::Comma) {
      advance();
    }
  }

  if (current().type != TokenType::RParen) {
    error("Expected ')' after arguments");
    return;
  }
  advance();
}

unique_ptr<Node> Parser::parsePostfix(unique_ptr<Node> expr) {
  while (true) {
    if (current().type == TokenType::LBracket) {
      advance();
      auto index = parseExpression();

      if (current().type != TokenType::RBracket) {
        error("Expected ']' after index");
        return expr;
      }
      advance();

      auto access = make_unique<IndexAccessNode>();
      access->target = std::move(expr);
      access->index = std::move(index);
      expr = std::move(access);
      continue;
    }

    if (current().type == TokenType::Dot) {
      advance();

      if (current().type != TokenType::Identifier) {
        error("Expected member name after '.'");
        return expr;
      }
      string memberName = current().value;
      advance();

      if (current().type == TokenType::LParen) {
        advance();
        auto call = make_unique<MemberCallNode>();
        call->target = std::move(expr);
        call->methodName = memberName;
        parseArguments(call->arguments);
        expr = std::move(call);
      } else {
        auto access = make_unique<MemberAccessNode>();
        access->target = std::move(expr);
        access->propertyName = memberName;
        expr = std::move(access);
      }
      continue;
    }

    break;
  }

  return expr;
}

unique_ptr<Node> Parser::buildStringLiteral(const string &raw) {
  vector<unique_ptr<Node>> parts;
  string text;

  for (size_t i = 0; i < raw.size(); i++) {
    if (raw[i] == '$' && i + 1 < raw.size() && raw[i + 1] == '{') {
      size_t end = raw.find('}', i + 2);
      if (end == string::npos) {
        text += raw[i];
        continue;
      }

      if (!text.empty()) {
        parts.push_back(make_unique<StringLiteralNode>(text));
        text.clear();
      }

      // Parse the embedded expression with a fresh lexer + parser.
      string exprSource = raw.substr(i + 2, end - (i + 2));
      Lexer exprLexer(exprSource);
      auto exprTokens = exprLexer.tokenize();
      Parser exprParser(exprTokens);
      auto expr = exprParser.parseExpression();
      if (expr) {
        parts.push_back(std::move(expr));
      }

      i = end;
    } else {
      text += raw[i];
    }
  }

  if (parts.empty()) {
    return make_unique<StringLiteralNode>(text);
  }

  if (!text.empty()) {
    parts.push_back(make_unique<StringLiteralNode>(text));
  }

  // Force string context so "${1}${2}" concatenates instead of adding.
  if (!dynamic_cast<StringLiteralNode *>(parts[0].get())) {
    parts.insert(parts.begin(), make_unique<StringLiteralNode>(""));
  }

  auto result = std::move(parts[0]);
  for (size_t i = 1; i < parts.size(); i++) {
    result =
        make_unique<BinaryOpNode>(std::move(result), std::move(parts[i]), "+");
  }
  return result;
}

unique_ptr<Node> Parser::parseFor() {
  advance(); // 'for'

  if (current().type != TokenType::LParen) {
    error("Expected '(' after 'for'");
    return nullptr;
  }
  advance();

  if (current().type != TokenType::Identifier) {
    error("Expected loop variable name");
    return nullptr;
  }

  auto node = make_unique<ForInNode>();
  node->varName = current().value;
  advance();

  if (current().type != TokenType::In) {
    error("Expected 'in' in for loop");
    return nullptr;
  }
  advance();

  node->iterable = parseExpression();

  if (current().type != TokenType::RParen) {
    error("Expected ')' after for loop");
    return nullptr;
  }
  advance();

  parseBlock(node->body);
  return node;
}
