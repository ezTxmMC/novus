#pragma once
#include <string>
#include <memory>

using namespace std;

struct Node
{
    virtual ~Node() = default;
};

struct PrintNode : Node
{
    string value;

    PrintNode(const string &v) : value(v) {}
};

struct BlockNode : Node {
    vector<unique_ptr<Node>> statements;
};

struct MethodNode : Node {
    string name;
    vector<unique_ptr<Node>> body;

    MethodNode(const string& n)
        : name(n) {}
};

struct ProgramNode : Node {
    vector<unique_ptr<Node>> methods;
};
