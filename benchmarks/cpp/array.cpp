#include <cstdio>
#include <vector>
int main() {
    std::vector<long long> values;
    for (long long i = 0; i < 2000000; i++) values.push_back(i);
    long long sum = 0;
    for (long long v : values) sum += v;
    printf("%lld\n", sum);
}
