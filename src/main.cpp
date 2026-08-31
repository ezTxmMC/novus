#include "interpreter/interpreter.hpp"
#include "lexer/lexer.hpp"
#include "logger/logger.hpp"
#include "parser/parser.hpp"
#include "util/file.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

// Run a Novus script in-process and capture everything it prints.
static string runNovusCaptured(const string &scriptPath,
                               const vector<string> &args) {
  string code = readFile(scriptPath);

  Lexer lexer(code);
  auto tokens = lexer.tokenize();

  Parser parser(tokens);
  parser.setBaseDir(filesystem::path(scriptPath).parent_path().string());
  auto ast = parser.parse();

  stringstream captured;
  auto *oldBuf = cout.rdbuf(captured.rdbuf());
  Interpreter interpreter(args);
  interpreter.interpret(ast);
  cout.rdbuf(oldBuf);

  return captured.str();
}

// novus build <file.nv> [-o out]: compile via the Novus-written pipeline
// (tools/novusc.nv) and hand the generated C to the system C compiler.
static int buildCommand(const string &exePath, const string &filePath,
                        const string &outPath) {
  filesystem::path toolsDir;
  const char *home = getenv("NOVUS_HOME");
  if (home) {
    toolsDir = filesystem::path(home) / "tools";
  } else {
    toolsDir =
        filesystem::canonical(exePath).parent_path().parent_path() / "tools";
  }

  if (!filesystem::exists(toolsDir / "novusc.nv")) {
    logError("Cannot find tools/novusc.nv (set NOVUS_HOME to the repo root)");
    return 1;
  }

  string cCode =
      runNovusCaptured((toolsDir / "novusc.nv").string(), {filePath});
  string runtime = runNovusCaptured((toolsDir / "emitrt.nv").string(), {});

  filesystem::path out(outPath);
  filesystem::path cFile = out;
  cFile += ".c";
  writeFile(cFile.string(), cCode);

  filesystem::path rtFile = out.parent_path().empty()
                                ? filesystem::path("novus_rt.h")
                                : out.parent_path() / "novus_rt.h";
  writeFile(rtFile.string(), runtime);

  string cmd = "cc \"" + cFile.string() + "\" -o \"" + out.string() + "\"";
  if (system(cmd.c_str()) != 0) {
    logError("C compilation failed (generated file kept at " + cFile.string() +
             ")");
    return 1;
  }

  logInfo("Built " + out.string());
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    logInfo("Usage: novus run <file.nv> [args...]");
    logInfo("       novus build <file.nv> [-o <output>]");
    return 1;
  }

  string command = argv[1];
  string filePath = argv[2];

  try {
    if (command == "run") {
      string code = readFile(filePath);

      Lexer lexer(code);
      auto tokens = lexer.tokenize();

      Parser parser(tokens);
      parser.setBaseDir(filesystem::path(filePath).parent_path().string());
      auto ast = parser.parse();

      vector<string> programArgs(argv + 3, argv + argc);
      Interpreter interpreter(programArgs);
      interpreter.interpret(ast);
      return 0;
    }

    if (command == "build") {
      string outPath = filesystem::path(filePath).stem().string();
      if (argc >= 5 && string(argv[3]) == "-o") {
        outPath = argv[4];
      }
      return buildCommand(argv[0], filePath, outPath);
    }

    logError("Unknown command.");
    return 1;
  } catch (const exception &exception) {
    logError(exception.what());
    return 1;
  }

  return 0;
}
