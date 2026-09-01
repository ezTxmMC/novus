with open("input.txt") as handle:
    text = handle.read()
counts = {}
for word in text.split():
    counts[word] = counts.get(word, 0) + 1
ranked = ["%8d %s" % (count, word) for word, count in counts.items()]
ranked.sort(reverse=True)
for line in ranked[:3]:
    print(line.strip())
print(len(counts), "unique")
