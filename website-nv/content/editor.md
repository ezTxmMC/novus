# Editor support

Novus has one language server, `novus-lsp`. It is written in Novus, speaks
LSP 3.17 over stdio and works in every editor that can start a command.
The VS Code extension is a thin client around it.

| Editor | How |
|---|---|
| VS Code | the [extension](#vs-code) (highlighting, run/build and the server) |
| Neovim | `vim.lsp.config` ([setup](#neovim)) |
| Helix | `languages.toml` ([setup](#helix)) |
| IntelliJ IDEA and the other JetBrains IDEs | the LSP4IJ plugin ([setup](#intellij-and-the-jetbrains-ides)) |
| Zed | a Zed extension has to name the server ([notes](#zed)) |
| Emacs, Sublime Text, Kate, ... | any LSP client: [the contract](#any-other-editor) |

Everything the server can do is on the [language server](/docs/projects/language-server)
page, together with its settings.

## Get the server

```bash
make lsp                      # builds build/novus-lsp
make install                  # novusc and novus-lsp into /usr/local/bin
build/novus-lsp --version     # novus-lsp 0.1.0
```

Releases carry a `novus-lsp-<target>` binary next to every `novusc-<target>`
(`novus-lsp-x86_64-linux-gnu`, `novus-lsp-aarch64-macos`,
`novus-lsp-x86_64-windows-gnu.exe`, ...). Rename the file to `novus-lsp`, make
it executable and put it on the `PATH`.

:::note
The server uses the standard library that was embedded in the compiler it was
built with. Rebuild it after you rebuild `novusc`.
:::

## VS Code

Build the extension and install it through *Extensions: Install from VSIX...*:

```bash
cd vscode-novus
npm install
npm run package
```

The extension looks for the server in this order:

1. the `novus.server.path` setting (absolute, relative to the workspace, or a command name on the `PATH`),
2. `build/novus-lsp` in the workspace folder,
3. `novus-lsp` on the `PATH`.

If none exists the extension says so and offers to open the setting or to fall
back to the previous TypeScript server (`novus.server.implementation`:
`lsp` or `typescript`). Changing either setting restarts the server;
*Novus: Restart Language Server* does it by hand. The static snippet file is no
longer contributed: the server offers the same snippets through completion, so
they appear once.

`novus.executablePath` still names `novusc` for *Run Novus File* and
*Build Novus File*; the server uses it for `novusc check` as well.

## Neovim

Neovim 0.11 or newer:

```lua
vim.filetype.add()

vim.lsp.config('novus', {
  cmd = ,
  filetypes = ,
  root_markers = ,
  settings = {
    novus = {
      check = ,
      pureline = ,
    },
  },
})
vim.lsp.enable('novus')
```

Older versions start the client from a `FileType` autocommand:

```lua
vim.api.nvim_create_autocmd('FileType', {
  pattern = 'novus',
  callback = function(args)
    vim.lsp.start({
      name = 'novus',
      cmd = ,
      root_dir = vim.fs.root(args.buf, ),
      settings = ,
    })
  end,
})
```

Semantic tokens colour symbols through the `@lsp.type.*` highlight groups
(`@lsp.type.enumMember`, `@lsp.mod.deprecated`, ...).

## Helix

Add to `~/.config/helix/languages.toml`:

```toml
[language-server.novus-lsp]
command = "novus-lsp"
args = ["--stdio"]

[language-server.novus-lsp.config]
check = 

[[language]]
name = "novus"
scope = "source.novus"
file-types = ["nv"]
roots = ["project.nv", ".git"]
comment-token = "//"
indent = 
language-servers = ["novus-lsp"]
```

Helix has no grammar for Novus, so there is no syntax colouring; completion,
diagnostics, hover, navigation, rename and formatting come from the server.

## IntelliJ and the JetBrains IDEs

Install the **LSP4IJ** plugin, then *Settings > Languages & Frameworks >
Language Servers > + New Language Server*:

| Field | Value |
|---|---|
| Name | `novus-lsp` |
| Command | `novus-lsp --stdio` |
| Mappings > File name patterns | `*.nv` with language id `novus`, `*.nvh` with language id `novus-html` |

The options go into the *Configuration* tab as JSON, for example
``.

## Zed

Zed starts language servers through extensions, so a Zed extension has to
register the language `Novus` and return `novus-lsp --stdio` as its server
command. There is no such extension yet. A server that an extension registers
can be pointed at the binary and given settings in `settings.json`:

```json
{
  "lsp": {
    "novus-lsp": {
      "binary": ,
      "settings": 
    }
  }
}
```

## Any other editor

The contract is small:

| | |
|---|---|
| command | `novus-lsp --stdio` (the flag is accepted and ignored; `--clientProcessId=N` too) |
| transport | stdio, `Content-Length` framing |
| language ids | `novus` for `.nv` (and `project.nv`), `novus-html` for `.nvh` |
| root | the folder with `project.nv`, else the opened folder |
| positions | UTF-16, or UTF-8 when the client offers it in `general.positionEncodings` |
| settings | `initializationOptions`, `workspace/didChangeConfiguration` or `workspace/configuration`, all under `novus` |

Logs go to stderr; `NOVUS_LSP_LOG=debug` raises the level
(`debug`, `info`, `warn`, `error`).

## Syntax highlighting without the server

The TextMate grammar this site uses for its code blocks is
`vscode-novus/syntaxes/novus.tmLanguage.json`; point any editor that reads
TextMate grammars at it.
