#include <cstdio>
#include <string>
#include <unordered_map>
int main() {
    std::unordered_map<std::string, long long> counts;
    for (long long i = 0; i < 300000; i++) counts["key" + std::to_string(i)] = i;
    long long sum = 0;
    for (long long i = 0; i < 300000; i++) sum += counts["key" + std::to_string(i)];
    printf("%zu %lld\n", counts.size(), sum);
}
