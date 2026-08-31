# Novus examples

| Example | Shows | Run with |
|---|---|---|
| [shapes](shapes/main.nv) | Interfaces, abstract classes, inheritance, polymorphism, enums, interpolation | `novus run examples/shapes/main.nv` |
| [todo](todo/main.nv) | CLI args, JSON persistence (`json` module), maps/arrays, `else if` chains | `novus run examples/todo/main.nv add "buy milk"` then `list` / `done 0` (writes `todo.json` into the working directory) |
| [wordcount](wordcount/main.nv) | The self-hosting subset: identical output interpreted and compiled | `novus run examples/wordcount/main.nv file.txt` or `novus build examples/wordcount/main.nv -o wc && ./wc file.txt` |

`novus build` compiles a **self-contained** program (no `import "file.nv"` resolution yet) that sticks to the compiler subset: `integer`, `float`, `bool`, `string`, `array<string>`, string-keyed maps, all control flow, functions, `readFile`/`writeFile`-free I/O via `readFile`/`args`, `chr`/`ord`/`parseInt`. Classes, enums and `${}` interpolation currently run in the interpreter only.
