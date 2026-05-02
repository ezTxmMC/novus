#include <string>
#include <iostream>
#include "lexer/lexer.hpp"
#include "logger/logger.hpp"
#include "parser/parser.hpp"
#include "util/file.hpp"

using namespace std;

void execute(Node *node)
{
    if (auto print = dynamic_cast<PrintNode *>(node))
    {
        std::cout << print->value << "\n";
    }

    if (auto method = dynamic_cast<MethodNode *>(node))
    {
        if (method->name == "main")
        {
            for (auto &stmt : method->body)
            {
                execute(stmt.get());
            }
        }
    }

    if (auto program = dynamic_cast<ProgramNode *>(node))
    {
        for (auto &m : program->methods)
        {
            execute(m.get());
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        logInfo("Usage: novus run <file.nv>");
        return 1;
    }

    string command = argv[1];
    string filePath = argv[2];

    if (command != "run")
    {
        logError("Unknown command.");
        return 1;
    }

    try
    {
        string code = readFile(filePath);

        Lexer lexer(code);
        auto tokens = lexer.tokenize();

        Parser parser(tokens);
        auto ast = parser.parse();

        execute(ast.get());
    }
    catch (const exception &exception)
    {
        logError(exception.what());
        return 1;
    }

    return 0;
}
