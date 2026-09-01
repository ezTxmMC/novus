#include <cstdio>
long long fib(long long n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int main() { printf("%lld\n", fib(30)); }
