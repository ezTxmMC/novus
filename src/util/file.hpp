#pragma once
#include <string>

using namespace std;

string readFile(const string &path);
void writeFile(const string &path, const string &content);
bool fileExists(const string &path);
