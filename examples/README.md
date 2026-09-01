# Novus examples

| Example | Shows | Run with |
|---|---|---|
| [shapes](shapes/main.nv) | Interfaces, abstract classes, inheritance, polymorphism, enums, interpolation | `novusc run examples/shapes/main.nv` |
| [todo](todo/main.nv) | CLI args, JSON persistence (`json` module), maps/arrays, `else if` chains | `novusc run examples/todo/main.nv add "buy milk"` then `list` / `done 0` (writes `todo.json` into the working directory) |
| [wordcount](wordcount/main.nv) | Files, maps and string processing | `novusc run examples/wordcount/main.nv file.txt` or `novusc build examples/wordcount/main.nv -o wc && ./wc file.txt` |
| [mccloud](mccloud/README.md) | A multi-file CLI project: classes, `os`/`path`/`json`, processes via `screen` (Linux/macOS) or console windows (Windows) | `novusc build examples/mccloud/main.nv -o mccloud && ./mccloud init` |

Every example is a golden test: `main.golden` next to it holds the expected
output and `test/run_tests.sh` compares against it. `novusc build` produces a
self-contained native executable; `novusc build ... --target x86_64-windows-gnu`
cross compiles it with zig.
