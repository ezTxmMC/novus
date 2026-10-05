# Pureline Specification 1.2

## Definition

**Pureline** is an opinionated coding style focused on linear control flow, explicit intent, compact formatting, small units, clear ownership, and minimal syntactic or architectural noise.

Pureline does not aim to minimize characters. It aims to minimize **unnecessary code**.

The preferred reading direction is:

```text
Input
↓
Validation
↓
Transformation
↓
Execution
↓
Result
```

Not:

```text
if
 └─ if
     └─ else
         └─ if
             └─ callback
                 └─ branch
```

The central Pureline principles are:

> **Linear Flow. Explicit Intent. Minimal Noise. Small Units. Clear Ownership.**

---

# 1. Pureline Core

These rules apply to all supported languages where the language allows them naturally.

| ID | Rule |
|---|---|
| PL-CORE-001 | Control flow should be as linear as possible |
| PL-CORE-002 | Avoid `else` |
| PL-CORE-003 | Prefer guard clauses |
| PL-CORE-004 | Use the clearest control-flow form for the language; brace-based languages use explicit blocks |
| PL-CORE-005 | Idiomatic language-native shorthand is allowed when it remains immediately readable |
| PL-CORE-006 | Functions should be small and focused |
| PL-CORE-007 | Names must carry meaning |
| PL-CORE-008 | Avoid unnecessary abstractions |
| PL-CORE-009 | Avoid God Classes and God Modules |
| PL-CORE-010 | Prefer immutable state |
| PL-CORE-011 | Prefer modern language features where they improve clarity |
| PL-CORE-012 | Avoid unnecessary vertical formatting |
| PL-CORE-013 | Comments must not compensate for unclear code |
| PL-CORE-014 | Handle invalid and error states early |
| PL-CORE-015 | Keep important side effects visible |
| PL-CORE-016 | Resources must have a clear lifecycle |
| PL-CORE-017 | Prefer framework-native APIs |
| PL-CORE-018 | Avoid magic values |
| PL-CORE-019 | Make dependencies explicit |
| PL-CORE-020 | Optimize hot paths deliberately, not accidentally |
| PL-CORE-021 | Keep packages and source folders small enough to scan at a glance |

---

# 2. Control Flow

Control flow is one of the most important parts of Pureline.

Pureline standardizes **clarity and structure, not identical syntax across languages**. Java, JavaScript, TypeScript, and Go use explicit blocks for control flow. Languages with established readable shorthand, such as Crystal postfix guards, may use that shorthand when the condition and action remain simple.

## PL-CF-001 — Prefer Guard Clauses

Preferred:

```java
if (request == null) {
    return Result.empty();
}

if (!request.valid()) {
    return Result.invalid();
}

var session = sessions.create(request.user());
return executor.execute(session, request);
```

Avoid:

```java
if (request != null) {
    if (request.valid()) {
        var session = sessions.create(request.user());
        return executor.execute(session, request);
    } else {
        return Result.invalid();
    }
}

return Result.empty();
```

The happy path should remain as close to the base indentation level as possible.

## PL-CF-002 — Avoid `else`

Avoid:

```java
if (valid) {
    execute();
} else {
    reject();
}
```

Prefer:

```java
if (!valid) {
    reject();
    return;
}

execute();
```

A ternary expression may be used for simple value selection:

```java
var state = valid ? State.READY : State.INVALID;
```

Complex nested ternaries are not Pureline.

## PL-CF-003 — Explicit Blocks in Brace-Based Languages

In Java, JavaScript, TypeScript, Go, and other brace-based languages, control-flow bodies use explicit blocks.

Not Pureline:

```java
if (!valid) return;
```

Also not Pureline:

```java
if (!valid)
    return;
```

Pureline:

```java
if (!valid) {
    return;
}
```

The same principle applies to `for`, `while`, and comparable control-flow constructs.

This rule does **not** ban idiomatic shorthand in languages where that shorthand is a normal, readable part of the language. Crystal postfix guards such as `return unless valid` are Pureline-compliant when kept simple.

---

# 3. Naming

## PL-NAME-001 — Names Must Be Meaningful

Good:

```text
request
session
player
packet
handler
storage
result
connection
worker
```

Avoid:

```text
r
s
p
x
tmp
obj
data2
thing
```

Also avoid names that are unnecessarily long:

```text
currentIncomingPlayerConnectionRequestObject
```

The rule is:

> **As short as possible, as explicit as necessary.**

Exceptions are allowed for established local conventions such as loop counters:

```java
for (int i = 0; i < size; i++) {
}
```

---

# 4. Functions

## PL-FN-001 — One Clear Responsibility

Preferred names:

```text
validateRequest()
createSession()
sendPacket()
loadUser()
closeConnection()
```

Avoid vague names:

```text
handleEverything()
process()
doStuff()
executeAll()
```

A function should normally represent one clearly identifiable action.

### Size guidance

```text
~5–30 lines  → normal
~30–50 lines → review
50+ lines    → usually split
```

These are heuristics, not absolute limits.

---

# 5. Classes and Modules

## PL-STRUCT-001 — One Clear Responsibility

Avoid structures like:

```text
ServerManager
 ├─ database
 ├─ players
 ├─ packets
 ├─ commands
 ├─ configuration
 ├─ HTTP
 └─ logging
```

Prefer:

```text
server/
    ServerRuntime

network/
    Connection
    PacketRegistry
    PacketDispatcher

session/
    Session
    SessionService

storage/
    UserStorage
    SessionStorage
```

Classes above roughly **200 lines** should be reviewed for multiple responsibilities.

---


# 5.1 Package and Folder Density

## PL-STRUCT-002 — Keep Namespaces Small Enough to Scan

Pureline treats file and type density as part of code readability.

A package or source folder should be small enough that a developer can understand its contents at a glance without searching through a long flat list of unrelated files.

When the limit is exceeded, split the package or folder by responsibility instead of allowing it to become a dumping ground.

### Default limits

| Language | Pureline default |
|---|---|
| Java | Maximum 8 production top-level types/source files per package |
| JavaScript | Maximum 10 production source modules per feature/source folder |
| TypeScript | Maximum 10 production source modules per feature/source folder |
| Go | Maximum 12 non-test, non-generated `.go` files per package directory |
| Crystal | Maximum 10 production `.cr` files per source/namespace folder |

These files do not count toward the limit:

- tests and specs
- generated source files
- metadata-only files
- `package-info.java`
- `module-info.java`
- JavaScript/TypeScript barrel files such as `index.ts` or `index.js`
- generated bindings or protocol output
- migration snapshots and comparable machine-generated artifacts

The purpose of this rule is not to force arbitrary folder nesting. A split should introduce a real responsibility boundary.

Bad:

```text
network/
    Connection.java
    ConnectionFactory.java
    ConnectionState.java
    Packet.java
    PacketCodec.java
    PacketDecoder.java
    PacketEncoder.java
    PacketRegistry.java
    PacketDispatcher.java
    PacketHandler.java
    PacketQueue.java
    PacketMetrics.java
    SocketConfig.java
    SocketRuntime.java
```

Preferred:

```text
network/
    Connection.java
    ConnectionFactory.java
    ConnectionState.java

network/packet/
    Packet.java
    PacketCodec.java
    PacketDecoder.java
    PacketEncoder.java
    PacketRegistry.java
    PacketDispatcher.java

network/runtime/
    PacketQueue.java
    PacketMetrics.java
    SocketConfig.java
    SocketRuntime.java
```

A project may override these thresholds through a future Pureline configuration file, but the default rule should remain intentionally strict.

---

# 6. Formatting

## PL-FMT-001 — Prefer Compact Horizontal Calls

Preferred:

```java
var box = Block.box(min, min, max, max, max, 16);
```

Avoid unnecessary vertical expansion:

```java
var box = Block.box(
    min,
    min,
    max,
    max,
    max,
    16
);
```

Vertical formatting is allowed when it reveals meaningful semantic structure.

Example:

```java
var server = Server.builder()
    .host(config.host())
    .port(config.port())
    .executor(executor)
    .build();
```

Pureline distinguishes between unnecessary vertical expansion and meaningful semantic grouping.

---

# 7. Comments

## PL-DOC-001 — Comments Explain Why, Not What

Avoid:

```java
// Check if the player exists
if (player == null) {
    return;
}
```

Useful comments explain things such as protocol constraints, workarounds, external bugs, non-obvious mathematical behavior, or unusual performance constraints.

Code should explain **what** happens. Comments should explain **why** something unusual is necessary.

---

# 8. Java Profile

## PL-JAVA-001 — Package by Responsibility

Preferred:

```text
src/main/java/
    space/example/project/
        api/
        runtime/
        service/
        network/
        storage/
        model/
```

Avoid generic dumping grounds such as `util/`, `manager/`, `misc/`, or `helper/` unless the package has a real and narrow purpose.

## PL-JAVA-002 — Naming

Classes:

```java
SessionService
PacketRegistry
UserStorage
ConnectionState
```

Interfaces use the `I` prefix:

```java
ISession
IStorage
IPacket
IConnection
```

Methods:

```java
open()
close()
load()
save()
execute()
resolve()
```

Variables:

```java
request
session
player
connection
```

Single-letter variables are disallowed unless they are established local conventions such as loop counters.

## PL-JAVA-003 — Prefer Records for Immutable Data

Preferred:

```java
public record SessionId(UUID value) {
}
```

Avoid boilerplate POJOs when a record is sufficient.

## PL-JAVA-004 — Explicit Null Handling

Avoid:

```java
Objects.requireNonNull(value);
```

for normal control-flow validation.

Prefer:

```java
if (value == null) {
    return;
}
```

or a domain-specific failure:

```java
if (config == null) {
    throw new ConfigurationException("Missing configuration");
}
```

## PL-JAVA-005 — Use Imports Instead of FQCNs

Avoid:

```java
java.util.concurrent.CompletableFuture<Result> future;
```

Prefer:

```java
import java.util.concurrent.CompletableFuture;
```

## PL-JAVA-006 — Prefer Modern Java

Use modern Java features when they improve the code:

```text
records
sealed types
pattern matching
switch expressions
var
virtual threads
text blocks
streams
```

Modern syntax should reduce noise, not create novelty for its own sake.

## PL-JAVA-007 — Prefer Switch for Real State Branching

Preferred:

```java
return switch (state) {
    case READY -> start();
    case CLOSED -> stop();
    case FAILED -> recover();
};
```

Prefer this over long chains of unrelated state checks.

## PL-JAVA-008 — Concurrency

Do not block main or tick threads with HTTP, database operations, filesystem access, or blocking network I/O.

Virtual threads are preferred for blocking I/O where appropriate.

## PL-JAVA-009 — Streams Only When Clearer

Streams are encouraged when they improve readability. Use a loop if it is clearer, easier to debug, or better suited to the hot path.

## PL-JAVA-010 — Lifecycle Symmetry

Anything that is opened, registered, started, allocated, or subscribed must have an equally clear corresponding close, unregister, stop, release, or unsubscribe path.

---

# 9. JavaScript Profile

## PL-JS-001 — Braces Are Mandatory

Preferred:

```js
if (!user) {
    return;
}
```

Not Pureline:

```js
if (!user) return;
```

## PL-JS-002 — Use Semicolons

Preferred:

```js
const session = createSession(user);
session.start();
```

Pureline JavaScript uses semicolons consistently.

## PL-JS-003 — `const` First

Preferred:

```js
const user = loadUser();
```

Use `let` only for actual reassignment:

```js
let count = 0;
count++;
```

Do not use `var`.

## PL-JS-004 — Prefer `async` / `await`

Preferred:

```js
async function loadUser(id) {
    const response = await fetch(`/users/${id}`);

    if (!response.ok) {
        return null;
    }

    return response.json();
}
```

Avoid promise chains when `async` / `await` produces clearer control flow.

## PL-JS-005 — Arrow Functions Are Contextual

Normal named logic may use regular functions:

```js
function createSession(user) {
    return new Session(user);
}
```

Arrow functions are well suited for local callbacks:

```js
users.filter(user => user.active());
```

Do not use arrow functions everywhere by default.

## PL-JS-006 — Destructuring Must Improve Clarity

Good:

```js
const { id, name } = user;
```

Avoid destructuring large objects only because the language allows it.

---

## PL-JS-007 — Folder Density

A JavaScript feature/source folder may contain at most **10 production source modules** by default.

Barrel files such as `index.js`, tests, generated files, and metadata-only files do not count.

If the folder exceeds the limit, split it by responsibility rather than by arbitrary file count.

---

# 10. TypeScript Profile

TypeScript inherits all Pureline JavaScript rules.

## PL-TS-001 — Avoid `any`

Avoid:

```ts
function load(value: any): any {
}
```

Prefer:

```ts
function load(value: UserId): User {
}
```

Use `unknown` for truly unknown data:

```ts
function parse(input: unknown): Config {
}
```

## PL-TS-002 — Use Types Deliberately

Both inferred and explicit local types are Pureline-compliant.

```ts
const user = loadUser();
```

is valid when the inferred type is obvious.

Explicit typing is equally valid:

```ts
const user: User = loadUser();
```

Pureline does **not** treat an explicit annotation as redundant merely because TypeScript could infer it.

Explicit types are especially useful when they:

- make intent clearer
- document an expected contract
- protect a boundary from unintended widening
- improve refactoring confidence
- make code easier to understand without following an entire call chain

Type inference should remove noise. It should never become a rule against using TypeScript's type system.

## PL-TS-003 — No Java-Style `I` Prefix

Preferred:

```ts
interface Session {
}
```

Not:

```ts
interface ISession {
}
```

The `I` prefix remains a Java-specific Pureline convention.

## PL-TS-004 — `interface` vs `type`

Use `interface` for object-like contracts:

```ts
interface User {
    id: string;
    name: string;
}
```

Use `type` for unions, aliases, and callable shapes:

```ts
type SessionState = "ready" | "closed" | "failed";
```

```ts
type Handler = (packet: Packet) => Promise<void>;
```

## PL-TS-005 — Prefer Union Types Over Enums When Appropriate

Preferred:

```ts
type State = "idle" | "ready" | "closed";
```

Use an enum only when an actual enum construct provides value.

## PL-TS-006 — Strong Types at Boundaries, Freedom Internally

Public APIs, external data, persistence boundaries, and network payloads should normally expose clear types.

Inside an implementation, both explicit annotations and inference are valid. Choose the form that makes the code easiest to understand. Pureline does not score one as cleaner purely because it contains fewer type annotations.

---

## PL-TS-007 — Folder Density

A TypeScript feature/source folder may contain at most **10 production source modules** by default.

Barrel files such as `index.ts`, declaration-only support files, tests, generated files, and metadata-only files do not count.

A split should represent a real responsibility boundary such as:

```text
session/
session/state/
session/transport/
session/storage/
```

---

# 11. Go Profile

Pureline Go must remain idiomatic Go. Pureline does not try to make Go look like Java.

## PL-GO-001 — Error Guards

Preferred:

```go
user, err := loadUser(id)

if err != nil {
    return nil, err
}

session := createSession(user)
return session, nil
```

This naturally matches Pureline's guard-first philosophy.

## PL-GO-002 — Avoid `else` After Terminating Branches

Avoid:

```go
if err != nil {
    return nil, err
} else {
    return createSession(user), nil
}
```

Prefer:

```go
if err != nil {
    return nil, err
}

return createSession(user), nil
```

## PL-GO-003 — Naming Must Remain Idiomatic

Good:

```text
user
session
conn
server
packet
request
ctx
err
wg
tx
```

Avoid meaningless one-letter names unless they are strongly established local conventions.

## PL-GO-004 — Short Receiver Names Are Allowed

Preferred:

```go
func (s *Server) Start() error {
}
```

Receiver names are local, conventional, and unambiguous.

Pureline does not force:

```go
func (server *Server) Start() error {
}
```

when it adds no clarity.

## PL-GO-005 — Small Interfaces

Preferred:

```go
type Storage interface {
    Load(id ID) (*User, error)
    Save(user *User) error
}
```

Avoid oversized interfaces containing unrelated responsibilities.

## PL-GO-006 — Define Interfaces Near Consumers

Interfaces should generally be defined where they are consumed rather than globally pre-designed as large inheritance-style abstractions.

## PL-GO-007 — Use `defer` for Clear Resource Lifecycles

Preferred:

```go
file, err := os.Open(path)

if err != nil {
    return err
}

defer file.Close()
```

## PL-GO-008 — Goroutines Need Ownership

Avoid fire-and-forget goroutines without an explicit lifecycle.

A long-lived goroutine should have clear handling for cancellation, error propagation, shutdown, and ownership.

## PL-GO-009 — Avoid Premature Interface Abstraction

Do not create an interface merely because a concrete type might theoretically have another implementation later. Create abstractions when there is a real consumer need.

---

# 12. Crystal Profile

Crystal keeps its own language identity while following Pureline principles.

## PL-CR-001 — Idiomatic Postfix Guards Are Allowed

Crystal postfix conditions are Pureline-compliant when they express a short, obvious guard.

Good:

```crystal
return unless valid
return if closed
```

They fit Pureline well because they can keep guard logic compact and preserve a linear happy path.

Use an explicit block when either the condition or the guarded action becomes more complex:

```crystal
if session.closed? || session.expired?
  logger.info("Session is no longer usable")
  return
end
```

The rule is not "avoid postfix conditions." The rule is: use the form that keeps the intent immediately obvious.

## PL-CR-002 — Avoid `else`

Avoid:

```crystal
if valid
  execute
else
  reject
end
```

Prefer:

```crystal
if !valid
  reject
  return
end

execute
```

## PL-CR-003 — Idiomatic Naming

Variables and methods use `snake_case`:

```crystal
session_service
packet_registry
load_user
create_session
```

Types use PascalCase:

```crystal
SessionService
PacketRegistry
UserStorage
```

## PL-CR-004 — Prefer Type Inference

Preferred:

```crystal
session = create_session(user)
```

Avoid redundant annotations:

```crystal
session : Session = create_session(user)
```

when Crystal can infer the type clearly.

## PL-CR-005 — Explicit Nil Guards

Preferred:

```crystal
user = users.find(id)

if user.nil?
  return
end

session = create_session(user)
```

Avoid burying expected nil handling inside deeply nested chains.

## PL-CR-006 — Exceptions Are for Exceptional States

Expected outcomes should preferably use normal domain modeling such as `nil`, union types, result-like structures, or enums.

Exceptions should represent exceptional failure, not ordinary branching.

## PL-CR-007 — Avoid Macro Cleverness Without Need

Crystal macros are powerful, but Pureline avoids hiding ordinary behavior behind metaprogramming unless the abstraction produces a clear architectural benefit.

Generated behavior should remain predictable and discoverable.

---

## PL-CR-008 — Folder Density

A Crystal source or namespace folder may contain at most **10 production `.cr` files** by default.

Specs, generated files, bindings, and metadata-only files do not count.

When the limit is exceeded, split the namespace by responsibility while preserving idiomatic Crystal structure.

---

# 13. Abstraction Rules

## PL-ARCH-001 — Abstraction Requires a Reason

Avoid abstractions such as:

```text
AbstractBaseFactoryManagerProvider
```

merely to reduce duplication.

Good reasons for abstraction include:

```text
multiple real implementations
clear API boundaries
platform-specific implementations
test seams
dependency inversion
shared domain contracts
```

---

# 14. Architecture

Pureline prefers clear dependency direction.

Example:

```text
domain
↓
service
↓
infrastructure
```

or feature-oriented modules:

```text
session/
network/
storage/
command/
runtime/
api/
```

Avoid cycles such as:

```text
network → session → storage → network
```

---

# 15. Side Effects

Important side effects should remain visible.

Preferred:

```java
var user = users.load(id);
var session = sessions.create(user);

sessions.save(session);
events.publish(new SessionCreated(session.id()));
```

Potentially misleading:

```java
sessions.createAndPersistAndNotify(user);
```

if persistence and event publication are important externally observable actions.

Pureline does not require exposing every internal operation. It requires avoiding surprising hidden behavior.

---

# 16. Immutability

Prefer immutable values by default.

Mutation should be intentional, local, and easy to identify.

Preferred examples include Java records, TypeScript readonly data, `const` in JavaScript, immutable value objects, and short-lived mutable local state.

Avoid shared mutable state where practical.

---

# 17. Dependencies

Dependencies should be visible in constructors, function parameters, or explicit module wiring.

Avoid hidden global access patterns and unnecessary service locators.

Dependency injection is encouraged where it improves architecture, but Pureline does not require a heavy DI framework.

---

# 18. Performance

Pureline prioritizes readability by default, but performance-sensitive paths should be written deliberately.

In hot paths:

- avoid unnecessary allocations
- avoid repeated reflection
- cache expensive resolution
- use appropriate collections
- avoid hidden blocking
- reduce unnecessary copying
- keep ownership clear

Performance optimizations must remain understandable.

---

# 19. Language Profiles

```text
Pureline Core
│
├── Pureline Java
│   ├── Records
│   ├── I-prefixed interfaces
│   ├── Modern Java
│   ├── Explicit null handling
│   └── Virtual Threads
│
├── Pureline JavaScript
│   ├── const-first
│   ├── semicolons
│   ├── braces
│   └── async/await
│
├── Pureline TypeScript
│   ├── strong typing
│   ├── inference
│   ├── unknown > any
│   └── unions/types
│
├── Pureline Go
│   ├── idiomatic error guards
│   ├── small interfaces
│   ├── defer
│   └── explicit goroutine lifecycle
│
└── Pureline Crystal
    ├── idiomatic postfix guards
    ├── explicit blocks for complex flow
    ├── nil guards
    └── type inference
```

---

# 20. Tooling Model

Pureline tooling should share the same rule specification across:

```text
Pureline Checker
Pureline Formatter
Pureline CLI
IntelliJ Plugin
VS Code Extension
Eclipse Plugin
future IDE integrations
```

Diagnostics should reference stable rule IDs.

Example:

```text
PL-CF-002
Avoid else after a terminating branch.

Line 48:
} else {

Suggested fix:
Remove the else block and continue with the happy path.
```

This allows the website, CLI, formatter, linter, and IDE integrations to stay consistent.

---

# 21. Pureline Summary

Pureline code should feel:

- linear
- explicit
- compact
- modern
- low-noise
- modular
- predictable
- easy to scan
- easy to refactor
- hard to misunderstand

The core rule remains:

> **Code without noise.**

A second principle governs every language profile:

> **Pureline standardizes principles, not syntax.**

A language should remain idiomatic. Pureline removes unnecessary noise without stripping away the features that make Java, JavaScript, TypeScript, Go, or Crystal effective in the first place.
