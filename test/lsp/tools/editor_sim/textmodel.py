"""A client-side text model with LSP position conversion, used to check the server's document state."""
import re

LINE_BREAK = re.compile(r"\r\n|\n")


def width(text, encoding):
    if encoding == "utf-8":
        return len(text.encode("utf-8"))
    if encoding == "utf-32":
        return len(text)
    return len(text.encode("utf-16-le")) // 2


def prefix_for(line, column, encoding):
    """The python string prefix of `line` that ends at the LSP column (clamped, never inside a character)."""
    used = 0
    for index, char in enumerate(line):
        step = width(char, encoding)
        if used + step > column:
            return index
        used += step
    return len(line)


class Model:
    def __init__(self, text, encoding, lone_cr=False):
        self.text = text
        self.encoding = encoding
        self.splitter = re.compile(r"\r\n|\n|\r") if lone_cr else LINE_BREAK

    def lines(self):
        parts, starts, last = [], [], 0
        for match in self.splitter.finditer(self.text):
            parts.append(self.text[last:match.start()])
            last = match.end()
        parts.append(self.text[last:])
        return parts

    def offset(self, line, column):
        parts = self.lines()
        if line >= len(parts):
            return len(self.text)
        consumed = 0
        for index in range(line):
            consumed += len(parts[index])
        # add the break lengths of the preceding lines
        position, seen = 0, 0
        for match in self.splitter.finditer(self.text):
            if seen == line:
                break
            position = match.end()
            seen += 1
        start = position if line > 0 else 0
        return start + prefix_for(parts[line], column, self.encoding)

    def position(self, offset):
        parts = self.lines()
        position, line = 0, 0
        for match in self.splitter.finditer(self.text):
            if match.end() > offset:
                break
            position = match.end()
            line += 1
        return line, width(self.text[position:offset], self.encoding)

    def apply(self, start, end, replacement):
        a, b = self.offset(*start), self.offset(*end)
        self.text = self.text[:a] + replacement + self.text[b:]
