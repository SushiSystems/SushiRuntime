# Security Policy

We take the security of SushiRuntime seriously. This document explains how to
report a vulnerability and what to expect in return.

## Reporting a vulnerability

**Please do not open a public GitHub issue for a security problem.** Public
disclosure before a fix is available puts every user at risk.

Instead, report it privately through one of:

- **Email (preferred):** **mustafagarip@sushisystems.io**
- **Discord:** reach the maintainer privately on the community server —
  invite: `https://discord.gg/y5639YuaPh`
If you prefer, you may also use GitHub's **private vulnerability reporting**
("Report a vulnerability" under the repository's Security tab).

### What to include

A good report lets us reproduce and assess quickly:

- A clear description of the issue and its impact.
- The affected component (e.g. the TCP transport, the memory pool, the
  dependency tracker) and the version / commit hash.
- Step-by-step reproduction, ideally with a minimal proof of concept.
- The platform, toolchain (oneAPI / intel-llvm), and build configuration
  (`Release` / `asan` / `--distributed` / CUDA on or off).
- Any suggested mitigation, if you have one.

## What to expect

- **Acknowledgement** within **72 hours**.
- An initial assessment and severity classification within **7 days**.
- Regular updates while we work on a fix, and credit in the release notes once a
  fix ships (unless you ask to remain anonymous).

Please give us a reasonable window to release a fix before any public
disclosure. We are happy to coordinate timing with you.

## Scope and threat model

A few properties are intentional design decisions rather than vulnerabilities —
please keep them in mind:

- **The distributed layer targets a trusted LAN.** The `TcpTransport` is a
  raw-socket layer **without encryption or authentication**, designed for a
  trusted cluster network (see
  [`DISTRIBUTED_GUIDE.md`](DISTRIBUTED_GUIDE.md) §12).
  Do **not** expose master/worker ports to the public internet. Reports about the
  *absence* of transport encryption are known and out of scope; reports about
  *memory-safety* bugs reachable through the protocol (malformed frames, buffer
  over-reads in deserialization, etc.) are firmly **in** scope.
- **The runtime executes the code you give it.** Tasks are arbitrary C++/SYCL you
  compile in. We are interested in memory-safety and concurrency defects *inside*
  the runtime (use-after-free, data races, out-of-bounds in the pool / EBR /
  scheduler / tracker), not in code a user chooses to run.

In scope, and especially valued:

- Memory-safety bugs in any layer (the lock-free pool, EBR, dependency tracker,
  scheduler, refcounting).
- Data races or ordering bugs that corrupt state.
- Parsing / deserialization flaws in the distributed wire protocol.
- Anything that lets a malicious worker or master corrupt the peer's memory.

## Supported versions

SushiRuntime is pre-1.0 and under active development. Security fixes are applied
to the latest `main`; there is no long-term support branch yet. Always test
against the current `main` before reporting.

## Contact

For anything that isn't sensitive, a GitHub issue or Discussion is fine. For
security matters, use the private channels above or email
**mustafagarip@sushisystems.io**.
