parts = []
for i in range(200_000):
    parts.append("word" + str(i % 10) + " ")
text = "".join(parts).strip()
words = text.split(" ")
joined = "-".join(words)
print(len(text), len(words), len(joined))
