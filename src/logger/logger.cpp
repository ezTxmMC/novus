#include "logger.hpp"
#include <iostream>
#include <ctime>
#include <string>

using std::cout;
using std::endl;
using std::string;

static inline const char* getTimeString()
{
    static char buffer[32];

    std::time_t now = std::time(nullptr);

    std::tm timeInfo{};
    localtime_r(&now, &timeInfo);

    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeInfo);

    return buffer;
}

static inline void logMessage(
    const char* color,
    const char* level,
    const string& text
)
{
    cout
        << color
        << "[" << level << ' ' << getTimeString() << "] "
        << text
        << RESET
        << '\n';
}

void logInfo(const string& text)
{
    logMessage("", "INFO", text);
}

void logWarn(const string& text)
{
    logMessage(YELLOW, "WARN", text);
}

void logError(const string& text)
{
    logMessage(RED, "ERROR", text);
}