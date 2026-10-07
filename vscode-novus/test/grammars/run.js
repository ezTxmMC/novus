"use strict";
// Grammar tests: `node test/grammars/run.js` (npm run test:grammars).

const { createRegistry, tokenizeLines } = require("./tokenize");

let failures = 0;
let checks = 0;

function check(name, condition, detail) {
    checks += 1;
    if (condition) {
        return;
    }
    failures += 1;
    console.error(`FAIL ${name}${detail ? `\n  ${detail}` : ""}`);
}

// The token whose text is `needle` (the first one, or the `nth` when given).
function tokenOf(lines, needle, nth = 0) {
    const all = lines.flat().filter((token) => token.text.trim() === needle);
    return all[nth];
}

function hasScope(token, part) {
    return token !== undefined && token.scopes.some((scope) => scope.includes(part));
}

// The grammar of C names its scopes `....c`: the root scope `source.c` is not part of an included rule's scopes.
function isC(token) {
    return token !== undefined && token.scopes.some((scope) => scope.endsWith(".c"));
}

function expectC(name, lines, needle) {
    const token = tokenOf(lines, needle);
    check(name, isC(token), `'${needle}' -> ${token ? token.scopes.join(" ") : "no such token"} (wanted a .c scope)`);
}

function expectScope(name, lines, needle, part, nth = 0) {
    const token = tokenOf(lines, needle, nth);
    check(name, hasScope(token, part), `'${needle}' -> ${token ? token.scopes.join(" ") : "no such token"} (wanted ${part})`);
}

function expectNoScope(name, lines, needle, part, nth = 0) {
    const token = tokenOf(lines, needle, nth);
    check(name, token !== undefined && hasScope(token, part) === false, `'${needle}' -> ${token ? token.scopes.join(" ") : "no such token"} (must not have ${part})`);
}

function novusCases(grammar) {
    const block = tokenizeLines(grammar, "method bump(integer by) {\n    c { int n = $by + 1; $total = nv_int(n); if (n) { n++; } }\n    println total\n}");
    expectScope("c-block/opener-is-a-keyword", block, "c", "keyword.control.c-block.novus");
    expectC("c-block/c-code-is-embedded-c", block, "int");
    expectScope("c-block/variable-is-a-novus-name", block, "$by", "variable.other.readwrite.novus");
    expectScope("c-block/assigned-variable", block, "$total", "variable.other.readwrite.novus");
    expectC("c-block/c-call-stays-c", block, "nv_int");
    expectScope("c-block/inner-braces-do-not-close", block, "++", "meta.embedded.block.c");
    expectScope("c-block/code-after-the-block-is-novus-again", block, "println", "novus");
    expectNoScope("c-block/code-after-the-block-is-no-c", block, "println", "meta.embedded.block.c");
    expectNoScope("c-block/c-token-is-not-novus-keyword", block, "int", "support.function.builtin.novus");

    const dollars = tokenizeLines(grammar, "method m() {\n    c { x = $$ + 1; }\n}");
    expectScope("c-block/double-dollar", dollars, "$$", "variable.other.readwrite.novus");

    const names = tokenizeLines(grammar, "method m() {\n    var c = 1\n    c = c + 1\n}\nmethod c {\n}");
    expectNoScope("c-block/c-as-a-variable-name", names, "c", "keyword.control.c-block.novus", 0);
    expectNoScope("c-block/c-assigned", names, "c", "keyword.control.c-block.novus", 1);
    expectNoScope("c-block/method-named-c", names, "c", "keyword.control.c-block.novus", 3);

    const top = tokenizeLines(grammar, "c {\n    #include <math.h>\n    static int twice(int v) { return v * 2; }\n}\nmethod main {\n}");
    expectC("c-block/top-level-block", top, "static");
    expectScope("c-block/top-level-ends", top, "method", "storage.type.method.novus");

    const cascade = tokenizeLines(grammar, "method m() {\n    var b = Box()..add(1)..width = 2\n    var d = b.width\n}");
    expectScope("cascade/operator", cascade, "..", "keyword.operator.cascade.novus");
    expectScope("cascade/second-operator", cascade, "..", "keyword.operator.cascade.novus", 1);
    expectScope("cascade/call-section", cascade, "add", "entity.name.function.member.novus");
    expectScope("cascade/assignment-section", cascade, "width", "variable.other.property.novus");
    expectNoScope("cascade/plain-dot-is-no-cascade", cascade, ".", "keyword.operator.cascade.novus");
}

function nvmdCases(grammar) {
    const text = [
        "---",
        "title: \"Control flow\"",
        "---",
        "<?nv",
        "prop integer level = 1",
        "?>",
        "# {title}",
        "",
        "Plain **text** and {level + 1}.",
        "",
        "{#if level > 0}",
        "shown",
        "{/if}",
        "",
        "```nv",
        "var x = c { int y; }",
        "```",
    ].join("\n");
    const lines = tokenizeLines(grammar, text);
    check("nvmd/frontmatter-is-yaml", lines[1].some((token) => hasScope(token, "yaml")), JSON.stringify(lines[1]));
    check("nvmd/header-is-novus", lines[4].some((token) => hasScope(token, "novus")), JSON.stringify(lines[4]));
    check("nvmd/heading-is-markdown", lines[6].some((token) => hasScope(token, "markup.heading")), JSON.stringify(lines[6]));
    check("nvmd/expression-in-a-heading-is-novus", lines[6].some((token) => token.text.includes("title") && hasScope(token, "novus")), JSON.stringify(lines[6]));
    check("nvmd/bold-is-markdown", lines[8].some((token) => hasScope(token, "markup.bold")), JSON.stringify(lines[8]));
    check("nvmd/expression-in-text-is-novus", lines[8].some((token) => token.text.includes("level") && hasScope(token, "novus")), JSON.stringify(lines[8]));
    check("nvmd/template-block-is-novus", lines[10].some((token) => hasScope(token, "novus")), JSON.stringify(lines[10]));
    check("nvmd/fence-is-novus", lines[15].some((token) => hasScope(token, "meta.embedded.block.novus")), JSON.stringify(lines[15]));
}

async function main() {
    const registry = await createRegistry();
    const novus = await registry.loadGrammar("source.novus");
    const nvmd = await registry.loadGrammar("source.nvmd");
    check("grammars/novus-loads", novus !== null);
    check("grammars/nvmd-loads", nvmd !== null);
    if (novus) {
        novusCases(novus);
    }
    if (nvmd) {
        nvmdCases(nvmd);
    }
    console.log(`${checks} checks, ${failures} failed`);
    process.exit(failures === 0 ? 0 : 1);
}

main();
