#include "file.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace std;

string readFile(const string &path) {
  ifstream file(path);

  if (!file.is_open())
    return "";

  stringstream buffer;
  buffer << file.rdbuf();

  return buffer.str();
}

void writeFile(const string &path, const string &content) {
  ofstream file(path);

  if (!file.is_open())
    throw runtime_error("Cannot write file '" + path + "'");

  file << content;
}

bool fileExists(const string &path) { return filesystem::exists(path); }
