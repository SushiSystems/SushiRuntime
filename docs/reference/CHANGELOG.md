# Changelog

## Unreleased

- 2026-11-25 — general: Fault a host dereference of Device memory on the host backend (RW-175).
- 2026-11-25 — api: Added RuntimeConfig::guard_device_memory and HostRuntimeConfig::guard_device_memory, off by default; Runtime::create() refuses it over a non-CPU device (RW-175, `runtime_config.hpp`).

## v0.7.2 — 2026-11-24

- 2026-11-23 — tests: The worker-agent rig waits for the HELLO of the new connection, which a worker that reconnects on its own can send before the rig counts; the pull test leaves quarantine (RW-174, KI-50, `test_worker_agent.cpp`).
- 2026-11-24 — cli: Required SushiCore 0.17.1 with no upper bound and pinned SushiLog and SushiProfiler 0.5.2, which install beside it (`pyproject.toml`, `sushistack.deps.toml`).
