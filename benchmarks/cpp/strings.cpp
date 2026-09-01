#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
int main() {
    std::string text;
    for (int i = 0; i < 200000; i++) {
        text += "word";
        text += std::to_string(i % 10);
        text += " ";
    }
    while (!text.empty() && text.back() == ' ') text.pop_back();
    std::vector<std::string> words;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) words.push_back(word);
    std::string joined;
    for (size_t i = 0; i < words.size(); i++) {
        if (i) joined += "-";
        joined += words[i];
    }
    printf("%zu %zu %zu\n", text.size(), words.size(), joined.size());
}
