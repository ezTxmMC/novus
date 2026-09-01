#include <cstdio>
#include <memory>
#include <vector>
struct Point {
    long long x, y;
    Point(long long x, long long y) : x(x), y(y) {}
    long long sum() const { return x + y; }
};
int main() {
    std::vector<std::unique_ptr<Point>> points;
    for (long long i = 0; i < 1000000; i++) points.push_back(std::make_unique<Point>(i, i * 2));
    long long total = 0;
    for (const auto &p : points) total += p->sum();
    printf("%lld\n", total);
}
