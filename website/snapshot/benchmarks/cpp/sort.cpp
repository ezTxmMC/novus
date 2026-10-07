#include <algorithm>
#include <cstdio>
#include <vector>
int main() {
    std::vector<long long> values;
    long long seed = 12345;
    for (int i = 0; i < 300000; i++) {
        seed = seed * 48271 % 2147483647;
        values.push_back(seed % 1000000);
    }
    std::sort(values.begin(), values.end());
    printf("%lld %lld %lld\n", values.front(), values[values.size() / 2], values.back());
}
