#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
int main() {
    std::ifstream file("input.txt");
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();
    std::unordered_map<std::string, long long> counts;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) counts[word]++;
    std::vector<std::string> ranked;
    ranked.reserve(counts.size());
    for (const auto &entry : counts) {
        char line[64];
        snprintf(line, sizeof(line), "%8lld ", entry.second);
        ranked.push_back(std::string(line) + entry.first);
    }
    std::sort(ranked.begin(), ranked.end(), std::greater<std::string>());
    for (int i = 0; i < 3; i++) {
        std::string line = ranked[i];
        size_t start = line.find_first_not_of(' ');
        printf("%s\n", line.substr(start).c_str());
    }
    printf("%zu unique\n", counts.size());
}
