#pragma once
#include <memory>
#include <string>
#include <vector>

using namespace std;

struct Node {
  virtual ~Node() = default;
};

struct Type {
  string name;
  bool isArray = false;

  Type() : name("void"), isArray(false) {}
  Type(const string &n, bool arr = false) : name(n), isArray(arr) {}

  bool operator==(const Type &other) const {
    return name == other.name && isArray == other.isArray;
  }
};

struct ParameterNode {
  Type type;
  string name;

  ParameterNode() : type("unknown"), name("") {}
  ParameterNode(const Type &t, const string &n) : type(t), name(n) {}
};

struct VariableNode : Node {
  Type type;
  string name;
  bool isFinal = false;
  bool isPrivate = false;
  unique_ptr<Node> value;

  VariableNode(const Type &t, const string &n, unique_ptr<Node> v = nullptr)
      : type(t), name(n), value(std::move(v)) {}
};

struct MethodNode : Node {
  string name;
  Type returnType;
  vector<ParameterNode> parameters;
  vector<unique_ptr<Node>> body;

  MethodNode(const string &n, const Type &rt = Type("void"))
      : name(n), returnType(rt) {}
};

struct MethodCallNode : Node {
  string objectName;
  string methodName;
  vector<unique_ptr<Node>> arguments;

  MethodCallNode(const string &obj, const string &method)
      : objectName(obj), methodName(method) {}
};

struct PropertyAccessNode : Node {
  string objectName;
  string propertyName;

  PropertyAccessNode(const string &obj, const string &prop)
      : objectName(obj), propertyName(prop) {}
};

struct IdentifierNode : Node {
  string name;

  IdentifierNode(const string &n) : name(n) {}
};

struct PrintNode : Node {
  string value;

  PrintNode(const string &v) : value(v) {}
};

struct BlockNode : Node {
  vector<unique_ptr<Node>> statements;
};

struct ReturnNode : Node {
  unique_ptr<Node> value;

  ReturnNode(unique_ptr<Node> v = nullptr) : value(std::move(v)) {}
};

struct BinaryOpNode : Node {
  unique_ptr<Node> left;
  unique_ptr<Node> right;
  char op;

  BinaryOpNode(unique_ptr<Node> l, unique_ptr<Node> r, char o)
      : left(std::move(l)), right(std::move(r)), op(o) {}
};

struct IntegerLiteralNode : Node {
  int value;

  IntegerLiteralNode(int v) : value(v) {}
};

struct FloatLiteralNode : Node {
  double value;

  FloatLiteralNode(double v) : value(v) {}
};

struct StringLiteralNode : Node {
  string value;

  StringLiteralNode(const string &v) : value(v) {}
};

struct ProgramNode : Node {
  vector<unique_ptr<Node>> methods;
};
