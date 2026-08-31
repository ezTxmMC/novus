#pragma once
#include <memory>
#include <string>
#include <utility>
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

struct AnnotationUse {
  string name;
  vector<pair<string, string>> args; // key=value (string literals)
};

struct MethodNode : Node {
  string name;
  Type returnType;
  vector<ParameterNode> parameters;
  vector<unique_ptr<Node>> body;
  bool isAbstract = false;
  vector<struct AnnotationUse> annotations;

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
  string op;

  BinaryOpNode(unique_ptr<Node> l, unique_ptr<Node> r, string o)
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

struct BoolLiteralNode : Node {
  bool value;

  BoolLiteralNode(bool v) : value(v) {}
};

struct UnaryOpNode : Node {
  string op;
  unique_ptr<Node> operand;

  UnaryOpNode(const string &o, unique_ptr<Node> e)
      : op(o), operand(std::move(e)) {}
};

struct AssignmentNode : Node {
  string name;
  unique_ptr<Node> value;

  AssignmentNode(const string &n, unique_ptr<Node> v)
      : name(n), value(std::move(v)) {}
};

struct IfNode : Node {
  unique_ptr<Node> condition;
  vector<unique_ptr<Node>> thenBranch;
  vector<unique_ptr<Node>> elseBranch;

  IfNode(unique_ptr<Node> cond) : condition(std::move(cond)) {}
};

struct WhileNode : Node {
  unique_ptr<Node> condition;
  vector<unique_ptr<Node>> body;

  WhileNode(unique_ptr<Node> cond) : condition(std::move(cond)) {}
};

struct ArrayLiteralNode : Node {
  vector<unique_ptr<Node>> elements;
};

struct IndexAccessNode : Node {
  unique_ptr<Node> target;
  unique_ptr<Node> index;
};

struct IndexAssignmentNode : Node {
  string name;
  unique_ptr<Node> index;
  unique_ptr<Node> value;
};

struct MapLiteralNode : Node {
  vector<pair<unique_ptr<Node>, unique_ptr<Node>>> entries;
};

struct ForInNode : Node {
  string varName;
  unique_ptr<Node> iterable;
  vector<unique_ptr<Node>> body;
};

struct BreakNode : Node {};

struct ContinueNode : Node {};

struct FieldNode {
  Type type;
  string name;
  bool isPrivate = false;
  bool isFinal = false;
  bool hasGet = false;
  bool hasSet = false;
};

struct ClassNode : Node {
  string name;
  string baseName;
  bool isAbstract = false;
  vector<FieldNode> fields;
  unique_ptr<MethodNode> constructor;
  vector<unique_ptr<MethodNode>> methods;
};

struct ObjectLiteralNode : Node {
  string className;
  vector<pair<string, unique_ptr<Node>>> fields;
};

struct PropertyAssignmentNode : Node {
  string objectName;
  string propertyName;
  unique_ptr<Node> value;
};

struct EnumConstant {
  string name;
  vector<unique_ptr<Node>> args;
};

struct EnumNode : Node {
  string name;
  vector<EnumConstant> constants;
  vector<FieldNode> fields;
  unique_ptr<MethodNode> constructor;
  vector<unique_ptr<MethodNode>> methods;
};

struct MemberCallNode : Node {
  unique_ptr<Node> target;
  string methodName;
  vector<unique_ptr<Node>> arguments;
};

struct MemberAccessNode : Node {
  unique_ptr<Node> target;
  string propertyName;
};

struct InterfaceNode : Node {
  string name;
  vector<string> methodNames;
};

struct AnnotationDefNode : Node {
  string name;
};
