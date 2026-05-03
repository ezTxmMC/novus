#include <string>
#include <iostream>
#include "lexer/lexer.hpp"
#include "logger/logger.hpp"
#include "parser/parser.hpp"
#include "interpreter/interpreter.hpp"
#include "util/file.hpp"

using namespace std;

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

        Interpreter interpreter;
        interpreter.interpret(ast);
    }
    catch (const exception &exception)
    {
        logError(exception.what());
        return 1;
    }

    return 0;
}
