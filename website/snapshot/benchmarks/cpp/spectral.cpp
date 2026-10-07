#include <cmath>
#include <cstdio>
#include <vector>

static double a(int i, int j) { return 1.0 / ((i + j) * (i + j + 1) / 2 + i + 1); }

static void mulAv(int n, const std::vector<double>& v, std::vector<double>& out) {
    for (int i = 0; i < n; i++) {
        double s = 0.0;
        for (int j = 0; j < n; j++) s = s + a(i, j) * v[j];
        out[i] = s;
    }
}

static void mulAtv(int n, const std::vector<double>& v, std::vector<double>& out) {
    for (int i = 0; i < n; i++) {
        double s = 0.0;
        for (int j = 0; j < n; j++) s = s + a(j, i) * v[j];
        out[i] = s;
    }
}

static void mulAtAv(int n, const std::vector<double>& v, std::vector<double>& out, std::vector<double>& tmp) {
    mulAv(n, v, tmp);
    mulAtv(n, tmp, out);
}

int main() {
    int n = 500;
    std::vector<double> u(n, 1.0), v(n, 0.0), tmp(n, 0.0);
    for (int i = 0; i < 10; i++) {
        mulAtAv(n, u, v, tmp);
        mulAtAv(n, v, u, tmp);
    }
    double vBv = 0.0, vv = 0.0;
    for (int i = 0; i < n; i++) {
        vBv = vBv + u[i] * v[i];
        vv = vv + v[i] * v[i];
    }
    printf("%.9f\n", std::sqrt(vBv / vv));
}
