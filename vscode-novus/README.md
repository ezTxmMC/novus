# Novus Language for VS Code

Full editor support for the [Novus](../README.md) programming language (`.nv` files).

## Features

- **Syntax highlighting** – TextMate grammar covering the complete concept syntax: packages, imports,
  methods, `define class|enum|interface|abstract|annotation`, `based`, constructors, annotations,
  accessors (`: get, set`), generics (`array<T>`), string interpolation (`${…}`), comments and operators.
- **Semantic highlighting** – classes, enums, interfaces, methods, fields, parameters, locals and modules
  are colored by what they *are*, not by how they look.
- **IntelliSense**
  - completion for keywords, types, methods, fields, locals, enum constants, modules and annotations,
    context aware (top level / class body / statement / after `.` / after `:` / after `@`)
  - object initializers: completing a class offers `Key{…}` which expands to `Key{ field=… }` with a
    placeholder per field, and inside `Key{ … }` (or `@Annotation{ … }`) the remaining field names are suggested
  - signature help with parameter highlighting (including overloads)
  - hover documentation (`///` and `/** */` doc comments are picked up)
- **Diagnostics** – syntax errors with recovery, unresolved names, duplicate declarations,
  methods without a body, assignments to `final` values, unused locals, deprecated members.
- **Auto import** – symbols from other packages are offered in completion and inserted together with the
  matching `import <package>` line; using an unimported symbol yields a warning with an *Import '…'* quick
  fix (plus *Add all missing imports*). Go to definition and hover work even before the import is added.
- **Navigation** – go to definition, find references, document highlights, rename (workspace wide),
  outline / breadcrumbs, workspace symbol search, folding.
- **Formatter** – *Format Document* / *Format Selection* (and `editor.formatOnSave`) normalize indentation,
  spacing and blank lines in the style of `test/syntax.nv` (`a + b`, `if (x) {`, `Key{field="value"}`,
  `array<Person> friends`, `name: get, set`). Line breaks, comments and strings are left as written, and it
  also works on files that do not parse yet.
- **Run & Build** – `Novus: Run Novus File` (editor play button, `Ctrl+Alt+N`) compiles and runs the
  current file (`novusc run`); `Novus: Build Novus File` compiles it to a native binary (`novusc build`).
  Both use `build/novusc` in the workspace, then `novusc` on `PATH`, or `novus.executablePath`.
- **Snippets** – `main`, `method`, `class`, `enum`, `interface`, `for`, `if`, `field`, `@Deprecated`, …

The whole workspace is indexed, so symbols defined in other `.nv` files are available for completion,
navigation and rename.

## Settings

| Setting                              | Default | Description                                               |
| ------------------------------------ | ------- | --------------------------------------------------------- |
| `novus.executablePath`               | `""`    | `novusc` used by run/build (empty = auto-detect)          |
| `novus.diagnostics.enabled`          | `true`  | Report problems                                           |
| `novus.diagnostics.undefinedSymbols` | `true`  | Warn about names that cannot be resolved                  |
| `novus.diagnostics.unusedVariables`  | `true`  | Fade out unused local variables                           |
| `novus.format.namedArgumentSpacing`  | `none`  | `Key{field="v"}` (`none`) or `Key{field = "v"}` (`spaces`) |
| `novus.format.maxBlankLines`         | `1`     | Consecutive blank lines kept by the formatter             |
| `novus.trace.server`                 | `off`   | LSP message tracing                                       |

## Language notes

Novus is self-hosting: `novusc`, written in Novus, compiles Novus (and itself) to C and is the only
implementation. It covers all of `test/syntax.nv` (a golden test): classes, inheritance, interfaces,
abstract classes, enums, annotations, overloading, `${}` interpolation, comments, modules (`json`, `path`,
`os`, `http`) and the free builtins (`readFile`, `writeFile`, `fileExists`, `removeFile`, `readLine`, `args`, `parseInt`,
`parseFloat`, `chr`, `ord`, `typeOf`, `exec`, `env`, `exit`, `platform`). Hover texts still mark the few
remaining concept-only pieces (`public`/`protected`/`static`, `null`, `image`).

Packages: a file declares `package name`; its top-level symbols are visible to files of the same package
and to files that `import name`. Files without a `package` line are visible everywhere. The builtin modules
`http`, `json`, `os` and `path` should be imported as well.

Statements are newline terminated (a `;` is optional). A binary operator that starts a new line begins a
new statement, so put operators at the end of the line when wrapping expressions.

## Development

```bash
cd vscode-novus
npm install
npm run compile      # or: npm run watch
npm test             # parser / analyzer smoke tests against test/*.nv
```

Open the `vscode-novus` folder in VS Code and press **F5** to launch an Extension Development Host with
the extension loaded (it opens the repository's `test/` folder). To install it permanently, build a `.vsix`
with `npm run package` and use *Extensions: Install from VSIX…*.

Project layout:

```
vscode-novus/
├─ package.json                  extension manifest (language, grammar, commands, settings)
├─ language-configuration.json   brackets, comments, indentation
├─ syntaxes/novus.tmLanguage.json TextMate grammar
├─ snippets/novus.json
└─ src/
   ├─ extension.ts               VS Code client + run command
   └─ server/
      ├─ lexer.ts / parser.ts / ast.ts   Novus front end (TypeScript port + concept syntax)
      ├─ analyzer.ts             scopes, symbols, type inference, diagnostics
      ├─ builtins.ts             keywords, primitive types, concept std-lib (http, json, path)
      ├─ completion.ts / features.ts     LSP providers
      ├─ formatter.ts            token based formatter
      └─ server.ts               LSP wiring
```
