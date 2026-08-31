#pragma once
#include "../ast/ast.hpp"
#include <map>
#include <memory>
#include <set>
#include <string>

using namespace std;

struct Value {
  Type type;
  string data;
  vector<Value> elements;     // set when type.isArray
  map<string, Value> entries; // set when type.name == "map"

  Value() : type("unknown"), data("") {}
  Value(const Type &t, const string &d) : type(t), data(d) {}
};

class Interpreter {
public:
  Interpreter() = default;
  explicit Interpreter(const vector<string> &args) : programArgs(args) {}

  void interpret(const unique_ptr<Node> &ast);
  void visitProgram(const ProgramNode *node);
  void visitMethod(const MethodNode *node);
  void visitVariable(const VariableNode *node);
  void visitAssignment(const AssignmentNode *node);
  void visitIndexAssignment(const IndexAssignmentNode *node);
  void visitPropertyAssignment(const PropertyAssignmentNode *node);
  Value constructObject(ClassNode *cls, const vector<Value> &args);
  Value callMemberOn(Value &target, const string &methodName,
                     vector<Value> &args);
  MethodNode *selectOverload(const vector<MethodNode *> &candidates,
                             const vector<Value> &args);
  Value constructInstance(const string &typeName,
                          const vector<FieldNode> &fields, MethodNode *ctor,
                          const vector<Value> &args);
  vector<FieldNode> collectFields(ClassNode *cls);
  MethodNode *findConstructorFor(ClassNode *cls);
  Value callModuleFunction(const string &module, const string &fn,
                           vector<Value> &args);
  void warnIfDeprecated(const MethodNode *m);
  void visitIf(const IfNode *node);
  void visitWhile(const WhileNode *node);
  void visitForIn(const ForInNode *node);
  void visitPrintNode(const Node *node);
  void visitIdentifier(const IdentifierNode *node);
  Value visitMethodCall(const MethodCallNode *node);
  void visitPropertyAccess(const PropertyAccessNode *node);
  Value visitBinaryOp(const BinaryOpNode *node);
  Value visitIntegerLiteral(const IntegerLiteralNode *node);
  Value visitFloatLiteral(const FloatLiteralNode *node);
  Value visitStringLiteral(const StringLiteralNode *node);
  Value visitReturn(const ReturnNode *node);

private:
  Value returnValue;
  bool hasReturned = false;
  bool hasBroken = false;
  bool hasContinued = false;
  vector<map<string, Value>> scopes;
  map<string, vector<MethodNode *>> methods;
  map<string, ClassNode *> classes;
  map<string, EnumNode *> enums;
  map<string, InterfaceNode *> interfaces;
  map<string, AnnotationDefNode *> annotationDefs;
  map<string, Value> globals;
  set<const MethodNode *> deprecatedWarned;
  map<string, map<string, Value>> enumValues;
  vector<string> programArgs;

  void pushScope();
  void popScope();
  Value *findVariable(const string &name);
  void defineVariable(const string &name, const Value &value);

  static Value boolValue(bool b);
  static bool isTruthy(const Value &v);
  static string display(const Value &v);

  void executeStatement(const Node *stmt);
  void executeBlock(const vector<unique_ptr<Node>> &statements);

  Value getValue(const string &varName);
  Value evalNode(const Node *node);
  Value evalExpression(const unique_ptr<Node> &node);
  Value callMethod(const string &methodName, const vector<Value> &args);
  Value coerceType(const Value &val, const Type &targetType);
};
