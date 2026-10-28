# SushiRuntime manual

What SushiRuntime is and its build line are in the root [`README.md`](../README.md). This page lists every live document under `docs/`.

The public interface is not stable below 1.0.0: pin an exact tag and reach the library through
one adapter module of your own, as [the integration guide](guides/INTEGRATION.md#version-compatibility) says.

## Getting started

| Document | Holds |
| --- | --- |
| [Install](getting_started/INSTALL.md) | The two ways to install, standalone and with SushiHub, and what each needs |
| [Introduction](getting_started/INTRODUCTION.md) | The build, a first program and each part of the public interface |

## Architecture

| Document | Holds |
| --- | --- |
| [Architecture](architecture/ARCHITECTURE.md) | The modules and which are public, how a failure travels, the path of a run, the build options |

## Guides

| Document | Holds |
| --- | --- |
| [CLI guide](guides/CLI_GUIDE.md) | Every `sr` command, its flags and the configuration it reads |
| [Integration](guides/INTEGRATION.md) | Consuming the installed package, and writing the one adapter module that reaches the library, shown on `applications/examples/adapter/` |
| [Distributed guide](guides/DISTRIBUTED_GUIDE.md) | Building and running the optional master and worker layer, and its limits |
| [Distributed failures](guides/DISTRIBUTED_FAILURES.md) | What the distributed layer does when a worker, a link or the master fails |
| [Placement guide](guides/PLACEMENT_GUIDE.md) | Placement classes, scopes and the placement rules that send work to a device or a machine |
| [Profiling guide](guides/PROFILING_GUIDE.md) | Seeing where a run spent its time: observers, `register_profiler_parts()`, what is recorded and what it costs |
| [FAQ](guides/FAQ.md) | The questions a newcomer asks, each answered from this manual |

## Reference

| Document | Holds |
| --- | --- |
| [Changelog](reference/CHANGELOG.md) | What changed, by release |
| [Glossary](reference/GLOSSARY.md) | The words these documents use |
| [Profiler keys](reference/PROFILER_KEYS.md) | Every name the runtime writes into a SushiProfiler record: tracks, attributes, zones, values, counters and facts |
| [Known issues](backlog/README.md) | Defects that are recorded and not yet fixed |
| [Security policy](../.github/SECURITY.md) | How to report a vulnerability, and what is in scope |
| [Code of conduct](../.github/CODE_OF_CONDUCT.md) | The conduct expected of contributors |
| [API main page](reference/API_MAINPAGE.md) | The front page of the Doxygen site that `sr docs` builds |

## Process

| Document | Holds |
| --- | --- |
| [Contributing](CONTRIBUTING.md) | Setup, coding style, what a change must document, commits and dependencies |
| [Documentation style guide](DOCUMENTATION_STYLE_GUIDE.md) | How a page here is written and where it goes |
| [Agent instructions](../CLAUDE.md) | How an agent is asked to work in this repository |

## Design

| Document | Holds |
| --- | --- |
| [Design map](design/README.md) | Which design document covers which topic, with its status |
| [Remaining work](backlog/README.md) | The backlog |

Agent work folders are under `agent/`. Finished material is under `archive/` and is not edited:
the archived changelog sections in `archive/changelog/`, the closed plans and reviews in
`archive/slop/` and `archive/superpowers/specs/`, and two task reports in
`archive/agent/reports/`.
