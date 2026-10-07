# Security Policy

## Reporting a vulnerability

**Please do not open a public GitHub issue or pull request for a security
vulnerability.** Public issues are visible to everyone immediately, including
anyone who might exploit the problem before a fix ships.

Instead, use GitHub's private reporting for this repository:

1. Go to the [Security tab](https://github.com/ezTxmMC/novus/security) of
   `ezTxmMC/novus`.
2. Click **"Report a vulnerability"** to open a private advisory.

This reaches maintainers directly and keeps the report confidential until a
fix is ready.

If private reporting is not enabled yet for the repository or is unavailable
to you, open a regular issue asking only for a private contact channel —
without any details of the vulnerability itself — and a maintainer will
follow up.

## What to include

To help us triage quickly, include where possible:

- A description of the vulnerability and its potential impact.
- Steps to reproduce, or a minimal `.nv`/`.nvh` program that demonstrates it.
- The affected component (e.g. `novusc`, a specific `std/` module, the
  runtime, `novus-lsp`, the `.nvh` web server) and version/commit.
- Whether it requires untrusted input, a malicious dependency, a malicious
  `project.nv`, or similar to trigger.

## Scope

Given Novus compiles to C and some programs embed raw C (`c { ... }` blocks),
the trust boundary is important to get right:

- **In scope:** memory safety or correctness bugs in `novusc`, the runtime
  (`runtime/*.h`), the standard library (`std/*.nv`), `novus-lsp`, or the
  `.nvh` web server (`std/web.nv`) that are reachable from Novus source code
  or network input *without* the program itself using `c { ... }` blocks or
  calling untrusted native code.
- **Out of scope:** the behavior of `c { ... }` blocks themselves. As the
  README states, a C block is not checked by Novus — a program with C blocks
  is exactly as safe as the C written in it. This is a documented, intentional
  escape hatch, not a vulnerability in the language.
- **Out of scope:** issues that require an attacker to already control the
  `project.nv` manifest, the `NOVUS_CACHE` directory, or a dependency the
  project owner chose to `require` — those are supply-chain/configuration
  trust decisions made by the project owner, though we're still interested in
  hardening reports (e.g. making unsafe configurations harder to reach by
  accident).

## Supported versions

Novus is pre-1.0 (`0.1.0-pre.alpha.*`); there is currently one actively
maintained line — the `master` branch and the latest tagged release. Security
fixes are released as a new patch/pre-release as soon as they're ready, not
backported to older pre-release tags.

## Disclosure

We aim to acknowledge reports within a few days and to agree on a disclosure
timeline with the reporter once the issue is confirmed. Credit is given in
the advisory unless you'd prefer to stay anonymous.
