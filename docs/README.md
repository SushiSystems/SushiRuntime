# SushiRuntime manual

What SushiRuntime is, its requirements and its build line are in the root
[`README.md`](../README.md). This page lists every live document under `docs/`.

## Getting started

| Document | Holds |
| --- | --- |
| [Introduction](getting_started/INTRODUCTION.md) | Setup, a first program and a tour of the fluent API |

## Architecture

| Document | Holds |
| --- | --- |
| [Architecture](architecture/ARCHITECTURE.md) | The modules under `modules/`, the execution model, build-time configuration and the directory map |

## Guides

| Document | Holds |
| --- | --- |
| [CLI guide](guides/CLI_GUIDE.md) | Every `sr` command, its flags and the configuration it reads |
| [Integration](guides/INTEGRATION.md) | Consuming an installed runtime through `find_package(SushiRuntime)` |
| [Distributed guide](guides/DISTRIBUTED_GUIDE.md) | Building and running the optional master-worker layer, and its v1 limits |
| [FAQ](guides/FAQ.md) | The questions a newcomer asks, each answered from this manual |

## Reference

| Document | Holds |
| --- | --- |
| [Changelog](reference/CHANGELOG.md) | What changed, by release |
| [Glossary](reference/GLOSSARY.md) | The words these documents use |
| [Known issues](reference/KNOWN_ISSUES.md) | Defects that are recorded and not yet fixed |
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
| [Remaining work](design/REMAINING_WORK.md) | The backlog |

Agent work folders are under `agent/`. Finished material is under `archive/` and is not edited:
the archived changelog sections in `archive/changelog/`, the closed plans and reviews in
`archive/slop/` and `archive/superpowers/specs/`, and two task reports in
`archive/agent/reports/`.
