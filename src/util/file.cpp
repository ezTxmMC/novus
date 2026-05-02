#include "file.hpp"
#include <fstream>
#include <sstream>

using namespace std;

string readFile(const string& path)
{
    ifstream file(path);

    if (!file.is_open())
        return "";

    stringstream buffer;
    buffer << file.rdbuf();

    return buffer.str();
}