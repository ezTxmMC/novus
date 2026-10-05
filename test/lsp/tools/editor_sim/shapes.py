"""Structural validators for the results of the LSP requests (what a client would reject or mis-render)."""
from urllib.parse import unquote, urlparse

from textmodel import Model, width


class Doc:
    def __init__(self, text, encoding):
        self.model = Model(text, encoding)
        self.lines = self.model.lines()
        self.encoding = encoding

    def valid_position(self, pos):
        if not (isinstance(pos, dict) and isinstance(pos.get("line"), int) and isinstance(pos.get("character"), int)):
            return "position is not {line, character} integers"
        if pos["line"] < 0 or pos["character"] < 0:
            return "negative position"
        if pos["line"] > len(self.lines) - 1:
            return "line %d beyond %d lines" % (pos["line"], len(self.lines))
        line_width = width(self.lines[pos["line"]], self.encoding)
        if pos["character"] > line_width:
            return "character %d beyond line width %d (line %d)" % (pos["character"], line_width, pos["line"])
        return None

    def valid_range(self, rng):
        if not isinstance(rng, dict) or "start" not in rng or "end" not in rng:
            return "range without start/end"
        for key in ("start", "end"):
            problem = self.valid_position(rng[key])
            if problem:
                return "%s: %s" % (key, problem)
        a, b = rng["start"], rng["end"]
        if (b["line"], b["character"]) < (a["line"], a["character"]):
            return "end before start"
        return None


def uri_path(uri):
    parsed = urlparse(uri)
    return unquote(parsed.path) if parsed.scheme == "file" else None


def check_symbols(symbols, doc, problems, depth=0):
    for symbol in symbols:
        if not symbol.get("name"):
            problems.append("symbol without name")
        if not (1 <= symbol.get("kind", 0) <= 26):
            problems.append("symbol kind %r" % symbol.get("kind"))
        for key in ("range", "selectionRange"):
            problem = doc.valid_range(symbol.get(key))
            if problem:
                problems.append("%s of %s: %s" % (key, symbol.get("name"), problem))
        r, sel = symbol.get("range"), symbol.get("selectionRange")
        if r and sel and doc.valid_range(r) is None and doc.valid_range(sel) is None:
            inside = (r["start"]["line"], r["start"]["character"]) <= (sel["start"]["line"], sel["start"]["character"]) and \
                     (sel["end"]["line"], sel["end"]["character"]) <= (r["end"]["line"], r["end"]["character"])
            if not inside:
                problems.append("selectionRange of %s outside range (LSP requires containment)" % symbol.get("name"))
        check_symbols(symbol.get("children") or [], doc, problems, depth + 1)


def check_semantic_tokens(data, doc, legend, problems):
    if len(data) % 5:
        problems.append("data length not a multiple of 5")
        return
    line, col, last = 0, 0, None
    for i in range(0, len(data), 5):
        dl, dc, length, kind, mods = data[i:i + 5]
        if dl < 0 or dc < 0 or length <= 0:
            problems.append("token %d has negative delta or zero length: %r" % (i // 5, data[i:i + 5]))
            return
        line += dl
        col = dc if dl else col + dc
        if kind >= len(legend["tokenTypes"]) or mods >= (1 << len(legend["modifiers"] if "modifiers" in legend else legend["tokenModifiers"])):
            problems.append("token %d outside the legend" % (i // 5))
        if line >= len(doc.lines):
            problems.append("token %d on line %d beyond the document" % (i // 5, line))
            return
        if col + length > width(doc.lines[line], doc.encoding):
            problems.append("token %d (%d:%d+%d) crosses the end of its line (width %d)" % (i // 5, line, col, length, width(doc.lines[line], doc.encoding)))
        if last and (line, col) < (last[0], last[1] + last[2]) and last[0] == line:
            problems.append("token %d overlaps the previous token at %d:%d" % (i // 5, line, col))
        last = (line, col, length)


def check_edits(edits, doc, problems, label):
    previous_end = (-1, -1)
    for edit in edits or []:
        problem = doc.valid_range(edit.get("range"))
        if problem:
            problems.append("%s edit range: %s" % (label, problem))
            continue
        if not isinstance(edit.get("newText"), str):
            problems.append("%s edit without newText" % label)
        start = (edit["range"]["start"]["line"], edit["range"]["start"]["character"])
        if start < previous_end:
            problems.append("%s edits overlap or are unsorted at %r" % (label, start))
        previous_end = (edit["range"]["end"]["line"], edit["range"]["end"]["character"])


def check_completion_item(item, doc, line, character, problems):
    if not isinstance(item.get("label"), str) or item["label"] == "":
        problems.append("completion item without label: %r" % (item,))
        return
    label = item["label"]
    if "kind" in item and not (1 <= item["kind"] <= 25):
        problems.append("%s: kind %r" % (label, item["kind"]))
    if item.get("insertTextFormat") not in (None, 1, 2):
        problems.append("%s: insertTextFormat %r" % (label, item.get("insertTextFormat")))
    for key in ("sortText", "filterText", "detail", "insertText"):
        if key in item and not isinstance(item[key], str):
            problems.append("%s: %s not a string" % (label, key))
    edit = item.get("textEdit")
    if edit:
        problem = doc.valid_range(edit.get("range"))
        if problem:
            problems.append("%s: textEdit range %s" % (label, problem))
        else:
            r = edit["range"]
            if r["start"]["line"] != r["end"]["line"]:
                problems.append("%s: textEdit range spans lines (VS Code rejects that)" % label)
            if not (r["start"]["line"] == line and r["start"]["character"] <= character <= r["end"]["character"]):
                problems.append("%s: textEdit range %r does not contain the cursor %d:%d (VS Code drops such items)" % (label, r, line, character))
    check_edits(item.get("additionalTextEdits"), doc, problems, label + " additionalTextEdits")
    docs = item.get("documentation")
    if isinstance(docs, dict) and docs.get("kind") not in ("markdown", "plaintext"):
        problems.append("%s: documentation kind %r" % (label, docs.get("kind")))


def check_hover(result, doc, problems):
    contents = result.get("contents")
    if isinstance(contents, dict):
        if contents.get("kind") not in ("markdown", "plaintext") or not isinstance(contents.get("value"), str):
            problems.append("hover MarkupContent malformed: %r" % (contents,))
    elif not isinstance(contents, (str, list)):
        problems.append("hover contents malformed")
    if "range" in result:
        problem = doc.valid_range(result["range"])
        if problem:
            problems.append("hover range: " + problem)
