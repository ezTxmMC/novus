# Novus Language

Novus is a small custom programming language written in C++ with its own lexer, parser, and runtime.  
It is designed as a learning project to understand how programming languages work internally.

---

## 🚀 Features (current state)

- Custom lexer (tokenization)
- Custom parser (AST generation)
- Method-based structure (`method main`)
- Basic `println` statement
- File execution via CLI
- Simple runtime execution (only `main` is executed)

---

## 📦 Example syntax

Concept syntax in [test/syntax.nv](test/syntax.nv)

```nv
package main

method main {
    println "Hello"
}
```
