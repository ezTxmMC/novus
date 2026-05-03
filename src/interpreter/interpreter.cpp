#include "interpreter.hpp"
#include <iostream>
#include <cmath>

void Interpreter::interpret(const unique_ptr<Node> &ast)
{
    if (!ast)
        return;

    if (auto program = dynamic_cast<const ProgramNode *>(ast.get()))
    {
        visitProgram(program);
    }
}

void Interpreter::visitProgram(const ProgramNode *node)
{
    if (!node)
        return;

    for (const auto &method : node->methods)
    {
        if (auto methodNode = dynamic_cast<const MethodNode *>(method.get()))
        {
            methods[methodNode->name] = const_cast<MethodNode *>(methodNode);
        }
    }

    if (methods.find("main") != methods.end())
    {
        visitMethod(methods["main"]);
    }
}

void Interpreter::visitMethod(const MethodNode *node)
{
    if (!node)
        return;

    for (const auto &stmt : node->body)
    {
        if (!stmt)
            continue;

        if (auto varNode = dynamic_cast<const VariableNode *>(stmt.get()))
        {
            visitVariable(varNode);
        }
        else if (auto returnNode = dynamic_cast<const ReturnNode *>(stmt.get()))
        {
            visitReturn(returnNode);
            break;
        }
        else
        {
            visitPrintNode(stmt.get());
        }
    }
}

void Interpreter::visitVariable(const VariableNode *node)
{
    if (!node)
        return;

    Value value;
    value.type = node->type;

    if (node->value)
    {
        Value evalResult = evalExpression(const_cast<unique_ptr<Node> &>(node->value));
        value.data = evalResult.data;
    }
    else
    {
        value.data = "";
    }

    setValue(node->name, value);
}

void Interpreter::visitPrint(const PrintNode *node)
{
    if (!node)
        return;

    Value val = getValue(node->value);
    if (val.type.name != "unknown")
    {
        std::cout << val.data << std::endl;
    }
    else
    {
        std::cout << node->value << std::endl;
    }
}

void Interpreter::visitPrintNode(const Node *node)
{
    if (!node)
        return;

    if (auto printNode = dynamic_cast<const PrintNode *>(node))
    {
        visitPrint(printNode);
        return;
    }

    if (auto binOpNode = dynamic_cast<const BinaryOpNode *>(node))
    {
        Value val = visitBinaryOp(binOpNode);
        std::cout << val.data << std::endl;
        return;
    }

    if (auto methodCallNode = dynamic_cast<const MethodCallNode *>(node))
    {
        Value val = visitMethodCall(const_cast<MethodCallNode *>(methodCallNode));
        std::cout << val.data << std::endl;
        return;
    }

    if (auto intLitNode = dynamic_cast<const IntegerLiteralNode *>(node))
    {
        Value val = visitIntegerLiteral(intLitNode);
        std::cout << val.data << std::endl;
        return;
    }

    if (auto floatLitNode = dynamic_cast<const FloatLiteralNode *>(node))
    {
        Value val = visitFloatLiteral(floatLitNode);
        std::cout << val.data << std::endl;
        return;
    }

    if (auto strLitNode = dynamic_cast<const StringLiteralNode *>(node))
    {
        Value val = visitStringLiteral(strLitNode);
        std::cout << val.data << std::endl;
        return;
    }

    if (auto idNode = dynamic_cast<const IdentifierNode *>(node))
    {
        Value val = getValue(idNode->name);
        std::cout << val.data << std::endl;
        return;
    }
}

void Interpreter::visitIdentifier(const IdentifierNode *node)
{
    if (!node)
        return;

    Value val = getValue(node->name);
    std::cout << val.data << std::endl;
}

Value Interpreter::visitMethodCall(const MethodCallNode *node)
{
    if (!node)
        return Value();

    vector<Value> args;
    for (const auto &arg : node->arguments)
    {
        args.push_back(evalExpression(arg));
    }

    return callMethod(node->methodName, args);
}

void Interpreter::visitPropertyAccess(const PropertyAccessNode *node)
{
    if (!node)
        return;

    std::cout << node->objectName << "." << node->propertyName << std::endl;
}

Value Interpreter::evalExpression(const unique_ptr<Node> &node)
{
    if (!node)
        return Value();

    if (auto printNode = dynamic_cast<const PrintNode *>(node.get()))
    {
        auto varVal = getValue(printNode->value);
        if (varVal.type.name != "unknown")
        {
            return varVal;
        }

        Value val;
        val.data = printNode->value;
        return val;
    }

    if (auto idNode = dynamic_cast<const IdentifierNode *>(node.get()))
    {
        return getValue(idNode->name);
    }

    if (auto methodCallNode = dynamic_cast<const MethodCallNode *>(node.get()))
    {
        return const_cast<Interpreter *>(this)->visitMethodCall(methodCallNode);
    }

    if (auto binOpNode = dynamic_cast<const BinaryOpNode *>(node.get()))
    {
        return const_cast<Interpreter *>(this)->visitBinaryOp(binOpNode);
    }

    if (auto intLitNode = dynamic_cast<const IntegerLiteralNode *>(node.get()))
    {
        return const_cast<Interpreter *>(this)->visitIntegerLiteral(intLitNode);
    }

    if (auto floatLitNode = dynamic_cast<const FloatLiteralNode *>(node.get()))
    {
        return const_cast<Interpreter *>(this)->visitFloatLiteral(floatLitNode);
    }

    if (auto strLitNode = dynamic_cast<const StringLiteralNode *>(node.get()))
    {
        return const_cast<Interpreter *>(this)->visitStringLiteral(strLitNode);
    }

    if (auto retNode = dynamic_cast<const ReturnNode *>(node.get()))
    {
        return const_cast<Interpreter *>(this)->visitReturn(retNode);
    }

    return Value();
}

Value Interpreter::getValue(const string &varName)
{
    auto it = variables.find(varName);
    if (it != variables.end())
    {
        return it->second;
    }
    return Value(Type("unknown"), "");
}

void Interpreter::setValue(const string &varName, const Value &value)
{
    variables[varName] = value;
}

Value Interpreter::coerceType(const Value &val, const Type &targetType)
{
    if (val.type == targetType)
    {
        return val;
    }

    if (targetType.name == "integer" && val.type.name == "float")
    {
        double floatVal = stod(val.data);
        int intVal = static_cast<int>(floatVal);
        return Value(Type("integer"), to_string(intVal));
    }

    if (targetType.name == "float" && val.type.name == "integer")
    {
        int intVal = stoi(val.data);
        double floatVal = static_cast<double>(intVal);
        return Value(Type("float"), to_string(floatVal));
    }

    if (targetType.name == "string")
    {
        return Value(Type("string"), val.data);
    }

    return val;
}

Value Interpreter::callMethod(const string &methodName, const vector<Value> &args)
{
    auto it = methods.find(methodName);
    if (it == methods.end())
    {
        return Value();
    }

    MethodNode *method = it->second;

    map<string, Value> savedVars = variables;

    for (size_t i = 0; i < method->parameters.size() && i < args.size(); i++)
    {
        Value coercedArg = coerceType(args[i], method->parameters[i].type);
        variables[method->parameters[i].name] = coercedArg;
    }

    Value methodReturnValue(method->returnType, "");
    hasReturned = false;
    returnValue = Value();

    for (const auto &stmt : method->body)
    {
        if (!stmt)
            continue;

        if (auto varNode = dynamic_cast<const VariableNode *>(stmt.get()))
        {
            visitVariable(varNode);
        }
        else if (auto printNode = dynamic_cast<const PrintNode *>(stmt.get()))
        {
            visitPrint(printNode);
        }
        else if (auto methodCallNode = dynamic_cast<const MethodCallNode *>(stmt.get()))
        {
            methodReturnValue = visitMethodCall(methodCallNode);
        }
        else if (auto returnNode = dynamic_cast<const ReturnNode *>(stmt.get()))
        {
            methodReturnValue = visitReturn(returnNode);
            methodReturnValue = coerceType(methodReturnValue, method->returnType);
            hasReturned = true;
            break;
        }

        if (hasReturned)
            break;
    }

    variables = savedVars;

    return methodReturnValue;
}

Value Interpreter::visitBinaryOp(const BinaryOpNode *node)
{
    if (!node)
        return Value();

    Value left = evalExpression(node->left);
    Value right = evalExpression(node->right);

    bool isFloat = left.type.name == "float" || right.type.name == "float";

    if (isFloat)
    {
        double leftNum = stod(left.data);
        double rightNum = stod(right.data);
        double result = 0;

        switch (node->op)
        {
        case '+':
            result = leftNum + rightNum;
            break;
        case '-':
            result = leftNum - rightNum;
            break;
        case '*':
            result = leftNum * rightNum;
            break;
        case '/':
            result = rightNum != 0 ? leftNum / rightNum : 0;
            break;
        case '%':
            result = fmod(leftNum, rightNum);
            break;
        default:
            return Value();
        }

        return Value(Type("float"), to_string(result));
    }
    else
    {
        int leftNum = stoi(left.data);
        int rightNum = stoi(right.data);
        int result = 0;

        switch (node->op)
        {
        case '+':
            result = leftNum + rightNum;
            break;
        case '-':
            result = leftNum - rightNum;
            break;
        case '*':
            result = leftNum * rightNum;
            break;
        case '/':
            result = rightNum != 0 ? leftNum / rightNum : 0;
            break;
        case '%':
            result = rightNum != 0 ? leftNum % rightNum : 0;
            break;
        default:
            return Value();
        }

        return Value(Type("integer"), to_string(result));
    }
}

Value Interpreter::visitIntegerLiteral(const IntegerLiteralNode *node)
{
    if (!node)
        return Value();
    return Value(Type("integer"), to_string(node->value));
}

Value Interpreter::visitFloatLiteral(const FloatLiteralNode *node)
{
    if (!node)
        return Value();
    return Value(Type("float"), to_string(node->value));
}

Value Interpreter::visitStringLiteral(const StringLiteralNode *node)
{
    if (!node)
        return Value();
    return Value(Type("string"), node->value);
}

Value Interpreter::visitReturn(const ReturnNode *node)
{
    if (!node || !node->value)
    {
        return Value();
    }

    return evalExpression(node->value);
}
