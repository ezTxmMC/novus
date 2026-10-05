"""Semantic invariants of LSP results (beyond "valid JSON-RPC"): every position/range lies inside the document, edits do not
overlap and are applied without error, formatting keeps the code and is idempotent, semantic tokens decode inside the
text, folding ranges are ordered, signature indexes are in range, snippets are well formed.

check(method, params, result, doc, legend) -> list of problem strings. `doc` is a Doc built from the text sent to the server.
"""
import json
import re

LINE_BREAK = re.compile(r"\r\n|\n|\r")


class Doc:
    def __init__(self, text):
        self.text = text
        self.lines = []
        start = 0
        for match in LINE_BREAK.finditer(text):
            self.lines.append(text[start:match.start()])
            start = match.end()
        self.lines.append(text[start:])
        self.starts = []
        offset = 0
        pieces = LINE_BREAK.split(text)
        breaks = [m.group(0) for m in LINE_BREAK.finditer(text)]
        for index, piece in enumerate(pieces):
            self.starts.append(offset)
            offset += len(piece) + (len(breaks[index]) if index < len(breaks) else 0)

    @staticmethod
    def utf16_len(piece):
        return len(piece.encode("utf-16-le", "surrogatepass")) // 2

    def line_units(self, line):
        return self.utf16_len(self.lines[line])

    def valid_position(self, position):
        if not isinstance(position, dict) or not isinstance(position.get("line"), int) or not isinstance(position.get("character"), int):
            return False
        line, character = position["line"], position["character"]
        return 0 <= line < len(self.lines) and 0 <= character <= self.line_units(line)

    def offset_of(self, position):
        """Character offset of a valid position (UTF-16 units to code points); a position inside a surrogate pair is rounded down."""
        line = self.lines[position["line"]]
        units = 0
        for index, char in enumerate(line):
            if units >= position["character"]:
                return self.starts[position["line"]] + index
            units += 2 if ord(char) > 0xFFFF else 1
        return self.starts[position["line"]] + len(line)


def check_range(doc, rng, what, problems):
    if not isinstance(rng, dict) or not doc.valid_position(rng.get("start")) or not doc.valid_position(rng.get("end")):
        problems.append("%s: range outside the document or malformed: %s" % (what, json.dumps(rng)[:120]))
        return False
    a, b = rng["start"], rng["end"]
    if (a["line"], a["character"]) > (b["line"], b["character"]):
        problems.append("%s: reversed range %s" % (what, json.dumps(rng)[:120]))
        return False
    return True


def contains(rng, position):
    a, b = rng["start"], rng["end"]
    return (a["line"], a["character"]) <= (position["line"], position["character"]) <= (b["line"], b["character"])


def apply_edits(doc, edits, what, problems):
    """The text after the edits, or None when they are invalid (out of range, overlapping)."""
    spans = []
    for edit in edits:
        if not check_range(doc, edit.get("range"), what, problems) or not isinstance(edit.get("newText"), str):
            return None
        spans.append((doc.offset_of(edit["range"]["start"]), doc.offset_of(edit["range"]["end"]), edit["newText"]))
    spans.sort(key=lambda item: (item[0], item[1]))
    for first, second in zip(spans, spans[1:]):
        if second[0] < first[1]:
            problems.append("%s: overlapping edits %s and %s" % (what, first[:2], second[:2]))
            return None
    out, cursor = [], 0
    for start, end, new in spans:
        out.append(doc.text[cursor:start])
        out.append(new)
        cursor = end
    out.append(doc.text[cursor:])
    return "".join(out)


def check_snippet(text, label, problems):
    depth = 0
    index = 0
    while index < len(text):
        char = text[index]
        if char == "\\":
            index += 2
            continue
        if char == "$" and index + 1 < len(text) and text[index + 1] == "{":
            depth += 1
            index += 2
            continue
        if char == "}" and depth > 0:
            depth -= 1
        index += 1
    if depth != 0:
        problems.append("completion item %r: unbalanced snippet placeholder in %r" % (label, text[:80]))


def check_completion(doc, result, problems):
    items = result.get("items") if isinstance(result, dict) else result
    if not isinstance(items, list):
        if result is not None:
            problems.append("completion result is neither list nor CompletionList")
        return
    for item in items[:300]:
        label = item.get("label")
        if not isinstance(label, str) or label == "":
            problems.append("completion item without label: %s" % json.dumps(item)[:100])
            continue
        edit = item.get("textEdit")
        if edit is not None:
            rng = edit.get("range") or edit.get("replace")
            if check_range(doc, rng, "completion %r textEdit" % label, problems) and rng["start"]["line"] != rng["end"]["line"]:
                problems.append("completion %r: textEdit range spans lines" % label)
            text = edit.get("newText", "")
        else:
            text = item.get("insertText", label)
        if item.get("insertTextFormat") == 2 and isinstance(text, str):
            check_snippet(text, label, problems)
        for extra in item.get("additionalTextEdits") or []:
            check_range(doc, extra.get("range"), "completion %r additionalTextEdit" % label, problems)


def check_locations(doc, uri, result, what, problems):
    items = result if isinstance(result, list) else ([result] if result else [])
    for location in items:
        target = location.get("targetUri") or location.get("uri")
        rng = location.get("targetSelectionRange") or location.get("range")
        if not isinstance(target, str) or not target.startswith("file://") and not target.startswith("std:") and not target.startswith("untitled:"):
            problems.append("%s: location without a usable uri: %s" % (what, json.dumps(location)[:120]))
            continue
        if target == uri:
            check_range(doc, rng, what, problems)
        elif not isinstance(rng, dict) or rng.get("start", {}).get("line", -1) < 0:
            problems.append("%s: malformed range in another file: %s" % (what, json.dumps(location)[:120]))


def check_symbols(doc, result, problems):
    stack = list(result or [])
    while stack:
        symbol = stack.pop()
        if not isinstance(symbol.get("name"), str):
            problems.append("symbol without name: %s" % json.dumps(symbol)[:100])
        if "location" in symbol:
            if symbol["location"].get("uri", "").startswith("file://"):
                pass
            continue
        if check_range(doc, symbol.get("range"), "symbol %r range" % symbol.get("name"), problems):
            selection = symbol.get("selectionRange")
            if check_range(doc, selection, "symbol %r selectionRange" % symbol.get("name"), problems):
                a, b, c, d = symbol["range"]["start"], symbol["range"]["end"], selection["start"], selection["end"]
                if not (contains(symbol["range"], c) and contains(symbol["range"], d)):
                    problems.append("symbol %r: selectionRange not inside range" % symbol.get("name"))
        stack.extend(symbol.get("children") or [])


def check_semantic_tokens(doc, result, legend, problems):
    data = (result or {}).get("data") if isinstance(result, dict) else None
    if data is None:
        return
    if len(data) % 5:
        problems.append("semantic tokens: data length %d is not a multiple of 5" % len(data))
        return
    line = character = 0
    for index in range(0, len(data), 5):
        delta_line, delta_start, length, kind, modifiers = data[index:index + 5]
        if delta_line < 0 or delta_start < 0 or length <= 0:
            problems.append("semantic tokens: negative delta or empty token at #%d: %s" % (index // 5, data[index:index + 5]))
            return
        line += delta_line
        character = delta_start if delta_line else character + delta_start
        if line >= len(doc.lines) or character + length > doc.line_units(line):
            problems.append("semantic tokens: token #%d at %d:%d length %d lies outside the text" % (index // 5, line, character, length))
            return
        if legend and kind >= legend[0]:
            problems.append("semantic tokens: token type %d is not in the legend (%d types)" % (kind, legend[0]))
            return
        if legend and modifiers >= (1 << legend[1]):
            problems.append("semantic tokens: modifier bits %d outside the legend (%d modifiers)" % (modifiers, legend[1]))
            return


def check_formatting(doc, result, problems):
    if result is None:
        return None
    new_text = apply_edits(doc, result, "formatting", problems)
    if new_text is None:
        return None
    strip = lambda text: re.sub(r"\s+", "", text)  # noqa: E731
    if strip(new_text) != strip(doc.text):
        problems.append("formatting changed more than whitespace (%d -> %d non-blank characters)" % (len(strip(doc.text)), len(strip(new_text))))
    return new_text


def check(method, params, result, doc, legend=None, position=None):
    problems = []
    if position and not doc.valid_position(position):
        position = clamp(doc, position)
    try:
        if method == "textDocument/completion":
            check_completion(doc, result, problems)
        elif method in ("textDocument/definition", "textDocument/references", "textDocument/documentHighlight"):
            uri = params["textDocument"]["uri"]
            items = result if isinstance(result, list) else ([result] if result else [])
            if method == "textDocument/documentHighlight":
                for highlight in items:
                    check_range(doc, highlight.get("range"), "highlight", problems)
            else:
                check_locations(doc, uri, items, method, problems)
        elif method == "textDocument/documentSymbol":
            check_symbols(doc, result, problems)
        elif method == "textDocument/foldingRange":
            for fold in result or []:
                if not (0 <= fold.get("startLine", -1) <= fold.get("endLine", -2) < len(doc.lines)):
                    problems.append("folding range outside the text: %s" % json.dumps(fold))
        elif method.startswith("textDocument/semanticTokens"):
            check_semantic_tokens(doc, result, legend, problems)
        elif method == "textDocument/formatting":
            check_formatting(doc, result, problems)
        elif method == "textDocument/rangeFormatting":
            check_formatting(doc, result, problems)
            asked = params["range"]
            for edit in result or []:
                if edit["range"]["start"]["line"] < asked["start"]["line"] or edit["range"]["end"]["line"] > asked["end"]["line"] + 1:
                    problems.append("rangeFormatting edit %s outside the requested lines %s-%s" % (json.dumps(edit["range"]), asked["start"]["line"], asked["end"]["line"]))
                    break
        elif method == "textDocument/rename":
            edits = ((result or {}).get("changes") or {}).get(params["textDocument"]["uri"], []) if isinstance(result, dict) else []
            if isinstance(result, dict) and "documentChanges" in result:
                for change in result["documentChanges"]:
                    if change.get("textDocument", {}).get("uri") == params["textDocument"]["uri"]:
                        edits = change.get("edits", [])
            new_text = apply_edits(doc, edits, "rename", problems) if edits else None
            for edit in edits:
                if edit.get("newText") != params["newName"]:
                    problems.append("rename: edit with newText %r, expected %r" % (edit.get("newText"), params["newName"]))
                    break
            for edit in edits[:50]:
                rng = edit.get("range")
                if doc.valid_position(rng.get("start")) and rng["start"]["line"] == rng["end"]["line"]:
                    old = doc.lines[rng["start"]["line"]]
                    if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", doc.lines[rng["start"]["line"]][_cut(doc, rng["start"]):_cut(doc, rng["end"])] or "-"):
                        problems.append("rename replaces a non-identifier %r" % old[_cut(doc, rng["start"]):_cut(doc, rng["end"])])
                        break
        elif method == "textDocument/prepareRename" and result:
            rng = result.get("range", result) if isinstance(result, dict) else None
            if check_range(doc, rng, "prepareRename", problems) and position and not contains(rng, position):
                problems.append("prepareRename: range does not contain the position")
        elif method == "textDocument/hover" and result:
            if "range" in result and check_range(doc, result["range"], "hover", problems) and position and not contains(result["range"], position):
                problems.append("hover: range does not contain the position")
            contents = result.get("contents")
            if contents in (None, "", [], {}) or (isinstance(contents, dict) and not contents.get("value")):
                problems.append("hover: empty contents")
        elif method == "textDocument/signatureHelp" and result:
            signatures = result.get("signatures") or []
            active = result.get("activeSignature", 0) or 0
            if signatures and not 0 <= active < len(signatures):
                problems.append("signatureHelp: activeSignature %s out of %d" % (active, len(signatures)))
            elif signatures:
                parameters = signatures[active].get("parameters") or []
                parameter = result.get("activeParameter", signatures[active].get("activeParameter", 0))
                if parameter is not None and parameters and not 0 <= parameter <= len(parameters):
                    problems.append("signatureHelp: activeParameter %s with %d parameters" % (parameter, len(parameters)))
        elif method == "textDocument/codeAction":
            uri = params["textDocument"]["uri"]
            for action in result or []:
                if not isinstance(action.get("title"), str) or not action["title"]:
                    problems.append("codeAction without title: %s" % json.dumps(action)[:100])
                workspace_edit = action.get("edit") or {}
                for edit in (workspace_edit.get("changes") or {}).get(uri, []):
                    check_range(doc, edit.get("range"), "codeAction %r edit" % action.get("title"), problems)
                for change in workspace_edit.get("documentChanges") or []:
                    if change.get("textDocument", {}).get("uri") == uri:
                        apply_edits(doc, change.get("edits", []), "codeAction %r" % action.get("title"), problems)
                if (workspace_edit.get("changes") or {}).get(uri):
                    apply_edits(doc, workspace_edit["changes"][uri], "codeAction %r" % action.get("title"), problems)
        elif method == "textDocument/documentLink":
            for link in result or []:
                check_range(doc, link.get("range"), "documentLink", problems)
    except Exception as error:  # noqa: BLE001
        problems.append("invariant checker raised %s: %s (result %s)" % (type(error).__name__, error, json.dumps(result)[:120]))
    return problems


def _cut(doc, position):
    units = 0
    for index, char in enumerate(doc.lines[position["line"]]):
        if units >= position["character"]:
            return index
        units += 2 if ord(char) > 0xFFFF else 1
    return len(doc.lines[position["line"]])


def formatted_text(doc, result):
    """The text after a formatting result, or None (no edits or invalid ones)."""
    if not result:
        return None
    return apply_edits(doc, result, "formatting", [])


def clamp(doc, position):
    """A position past the end of a line or of the text is clamped by the server (LSP 3.17)."""
    line = min(max(position["line"], 0), len(doc.lines) - 1)
    return {"line": line, "character": min(max(position["character"], 0), doc.line_units(line))}
