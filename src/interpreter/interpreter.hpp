#pragma once
#include <string>
#include <map>
#include <memory>
#include "../ast/ast.hpp"

using namespace std;

struct Value
{
    Type type;
    string data;

    Value() : type("unknown"), data("") {}
    Value(const Type &t, const string &d) : type(t), data(d) {}
};

class Interpreter
{
public:
    Interpreter() = default;

    void interpret(const unique_ptr<Node> &ast);
    void visitProgram(const ProgramNode *node);
    void visitMethod(const MethodNode *node);
    void visitVariable(const VariableNode *node);
    void visitPrint(const PrintNode *node);
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
    map<string, Value> variables;
    map<string, MethodNode *> methods;

    Value getValue(const string &varName);
    void setValue(const string &varName, const Value &value);
    Value evalExpression(const unique_ptr<Node> &node);
    Value callMethod(const string &methodName, const vector<Value> &args);
    Value coerceType(const Value &val, const Type &targetType);
};
