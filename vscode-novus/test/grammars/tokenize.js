"use strict";
// Tokenizes text with the real TextMate engine (vscode-textmate + oniguruma) and the real grammars: the ones of this
// extension and, for the embedded languages, those of the tm-grammars bundle (the grammars VS Code ships).

const fs = require("node:fs");
const path = require("node:path");
const vscodeTextmate = require("vscode-textmate");
const oniguruma = require("vscode-oniguruma");

const ROOT = path.resolve(__dirname, "..", "..");
const BUNDLE = path.join(ROOT, "node_modules", "tm-grammars", "grammars");

const GRAMMAR_FILES = {
    "source.novus": path.join(ROOT, "syntaxes", "novus.tmLanguage.json"),
    "text.html.novus": path.join(ROOT, "syntaxes", "nvh.tmLanguage.json"),
    "source.nvmd": path.join(ROOT, "syntaxes", "nvmd.tmLanguage.json"),
    "source.c": path.join(BUNDLE, "c.json"),
    "source.yaml": path.join(BUNDLE, "yaml.json"),
    "text.html.markdown": path.join(BUNDLE, "markdown.json"),
    "text.html.basic": path.join(BUNDLE, "html.json"),
};

async function createRegistry() {
    const wasm = fs.readFileSync(path.join(ROOT, "node_modules", "vscode-oniguruma", "release", "onig.wasm"));
    await oniguruma.loadWASM(wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength));
    return new vscodeTextmate.Registry({
        onigLib: Promise.resolve({
            createOnigScanner: (patterns) => new oniguruma.OnigScanner(patterns),
            createOnigString: (text) => new oniguruma.OnigString(text),
        }),
        loadGrammar: async (scopeName) => {
            const file = GRAMMAR_FILES[scopeName];
            if (!file) {
                return null;
            }
            return vscodeTextmate.parseRawGrammar(fs.readFileSync(file, "utf8"), file);
        },
    });
}

// The lines of `text` as lists of { text, scopes }.
function tokenizeLines(grammar, text) {
    let ruleStack = vscodeTextmate.INITIAL;
    return text.split("\n").map((line) => {
        const result = grammar.tokenizeLine(line, ruleStack);
        ruleStack = result.ruleStack;
        return result.tokens.map((token) => ({ text: line.slice(token.startIndex, token.endIndex), scopes: token.scopes }));
    });
}

module.exports = { createRegistry, tokenizeLines };
