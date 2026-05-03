#ifndef LOGGER_H
#define LOGGER_H

#include <string>

using namespace std;

inline constexpr const char *RESET = "\033[0m";
inline constexpr const char *BOLD = "\033[1m";
inline constexpr const char *UNDERLINE = "\033[4m";

inline constexpr const char *BLACK = "\033[30m";
inline constexpr const char *RED = "\033[31m";
inline constexpr const char *GREEN = "\033[32m";
inline constexpr const char *YELLOW = "\033[33m";
inline constexpr const char *BLUE = "\033[34m";
inline constexpr const char *MAGENTA = "\033[35m";
inline constexpr const char *CYAN = "\033[36m";
inline constexpr const char *WHITE = "\033[37m";

void logInfo(const std::string &text);
void logWarn(const std::string &text);
void logError(const std::string &text);

#endif