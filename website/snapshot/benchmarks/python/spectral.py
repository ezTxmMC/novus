import math


def a(i, j):
    return 1.0 / ((i + j) * (i + j + 1) // 2 + i + 1)


def mul_av(n, v, out):
    for i in range(n):
        s = 0.0
        for j in range(n):
            s = s + a(i, j) * v[j]
        out[i] = s


def mul_atv(n, v, out):
    for i in range(n):
        s = 0.0
        for j in range(n):
            s = s + a(j, i) * v[j]
        out[i] = s


def mul_atav(n, v, out, tmp):
    mul_av(n, v, tmp)
    mul_atv(n, tmp, out)


n = 500
u = [1.0] * n
v = [0.0] * n
tmp = [0.0] * n
for _ in range(10):
    mul_atav(n, u, v, tmp)
    mul_atav(n, v, u, tmp)
vbv = 0.0
vv = 0.0
for i in range(n):
    vbv = vbv + u[i] * v[i]
    vv = vv + v[i] * v[i]
print("%.9f" % math.sqrt(vbv / vv))
