"""Expansion of LSP snippet syntax to plain text (first choice, default text, mirrors) for the e2e checks."""


def parse(text, index=0, stop=None, state=None):
    out = []
    state = state if state is not None else {}
    while index < len(text):
        char = text[index]
        if char == "\\" and index + 1 < len(text) and text[index + 1] in "\\$}|,":
            out.append(text[index + 1])
            index += 2
            continue
        if stop and char == stop:
            return "".join(out), index
        if char == "$":
            piece, index = dollar(text, index, state)
            out.append(piece)
            continue
        out.append(char)
        index += 1
    return "".join(out), index


def dollar(text, index, state):
    nxt = text[index + 1] if index + 1 < len(text) else ""
    if nxt.isdigit():
        end = index + 1
        while end < len(text) and text[end].isdigit():
            end += 1
        number = int(text[index + 1:end])
        return state.get(number, ""), end
    if nxt != "{":
        return "$", index + 1
    end = index + 2
    while end < len(text) and text[end].isdigit():
        end += 1
    number = int(text[index + 2:end])
    if end < len(text) and text[end] == "}":
        return state.get(number, ""), end + 1
    if text[end] == ":":
        value, close = parse(text, end + 1, "}", state)
        state.setdefault(number, value)
        return value, close + 1
    if text[end] == "|":
        close = end + 1
        options = []
        current = []
        while text[close] != "|" or text[close + 1] != "}":
            if text[close] == "\\":
                current.append(text[close + 1])
                close += 2
                continue
            if text[close] == ",":
                options.append("".join(current))
                current = []
            else:
                current.append(text[close])
            close += 1
        options.append("".join(current))
        state.setdefault(number, options[0])
        return options[0], close + 2
    raise ValueError("bad snippet %r at %d" % (text, index))


def expand(text):
    return parse(text)[0]


def tabstops(text):
    """Numbers of the tabstops of an LSP snippet (diagnostic helper)."""
    import re
    return sorted({int(m) for m in re.findall(r"\$\{?(\d+)", text)})
