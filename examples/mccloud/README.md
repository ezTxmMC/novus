# mccloud - a tiny Minecraft server cloud

A command line "cloud" for Minecraft servers written in Novus: server groups
with a template each, numbered instances (`lobby-1`, `lobby-2`, ...) that
are copied from the template and started detached, plus stop / list / log /
console access.

- **Linux / macOS**: every instance runs in a `screen` session (`mc-lobby-1`);
  `cmd` types into its console, `attach` opens it (`Ctrl+A D` to detach),
  `stop` sends the `stop` command.
- **Windows**: every instance runs in its own console window titled
  `mc-lobby-1` (`start ... cmd /c java ...`); `list` checks it with
  `tasklist`, `stop` closes it with `taskkill`. Console commands are typed
  into the window directly.

```
novusc build examples/mccloud/main.nv -o mccloud
./mccloud init
./mccloud group create lobby 1024 25565 2      # 1 GB, ports from 25565, max 2
./mccloud template import lobby ~/Downloads/paper.jar
./mccloud start lobby                          # -> lobby-1 on 25565
./mccloud start lobby                          # -> lobby-2 on 25566
./mccloud list
./mccloud cmd lobby-1 say hello
./mccloud log lobby-1 20
./mccloud stop all
```

Layout of the cloud directory (`./mccloud-data`, or `$MCCLOUD_HOME`):

```
mccloud-data/
  cloud.json          java command and the groups
  servers.json        running instances
  templates/<group>/  server.jar, eula.txt, server.properties, plugins/ ...
  servers/<name>/     one directory per instance (kept after stop, `delete` removes it)
```

Requirements: Java on `PATH` (or set `"java"` in `cloud.json`), `screen` on
Linux/macOS. Starting a server accepts the Minecraft EULA (`eula.txt`).
