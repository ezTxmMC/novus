counts = {}
for i in range(300_000):
    counts["key" + str(i)] = i
total = 0
for i in range(300_000):
    total += counts["key" + str(i)]
print(len(counts), total)
