values = []
seed = 12345
for _ in range(300_000):
    seed = seed * 48271 % 2147483647
    values.append(seed % 1_000_000)
values.sort()
print(values[0], values[len(values) // 2], values[-1])
