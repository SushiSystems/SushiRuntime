# SushiRuntime Documentation Update Map (Vibecoding Guide)

This document is a guide indicating **which documentation files must be updated when specific parts of the codebase change**. It is especially important during large architectural refactoring or when an AI agent is performing "vibecoding" on the project.

For an AI agent to truly understand the project, avoid losing context, and keep the documentation synchronized with the code, the rules in this map must be strictly followed.

## Code to Documentation Mapping

The table below lists which markdown files you must review and update whenever you modify a specific directory or module in the codebase:

| Modified Directory / Module | Documents to Update | Reason / Section to Check |
| :--- | :--- | :--- |
| `include/SushiRuntime/core/`<br>`src/core/` | `docs/guides/ARCHITECTURE.md` | **Section 4 (Core):** Update this if changes occur in `RefCounted`, `sushi_ptr`, `OpID`, memory alignment, or base constants (`common.hpp`). |
| `include/SushiRuntime/topology/`<br>`src/topology/` | `docs/guides/ARCHITECTURE.md` | **Section 5 (Topology):** Update this if there are changes to hardware discovery, NUMA mapping strategies, or thread pinning (affinity) logic. |
| `include/SushiRuntime/memory/`<br>`src/memory/` | `docs/guides/ARCHITECTURE.md` | **Section 6 (Memory):** Any changes to `USMAllocator`, lock-free `BitmaskMemoryPool`, or Epoch-Based Reclamation (EBR) logic must be reflected here. |
| `include/SushiRuntime/graph/`<br>`src/graph/` | `docs/guides/ARCHITECTURE.md`<br>`docs/guides/INTRODUCTION.md` | **Section 7 (Graph):** Update Architecture if `Node` structure, lock-free `DependencyTracker`, or `TaskGraph` internal API changes. If public APIs (`add_task`, `add_node`) change, you MUST update the code snippets in `INTRODUCTION.md`. |
| `include/SushiRuntime/scheduler/`<br>`src/scheduler/` | `docs/guides/ARCHITECTURE.md` | **Section 8 (Scheduler):** Update this if there are modifications to the work-stealing logic, NUMA-aware hierarchy, `EventPolling`, or `WorkloadCoordinator` algorithms. |
| `include/SushiRuntime/execution/`<br>`src/execution/` | `docs/guides/ARCHITECTURE.md`<br>`docs/guides/INTRODUCTION.md` | **Section 9 (Execution):** Update Architecture for changes in `RuntimeContext` lifecycle or queue management. If the public `RuntimeContext` API changes, update `INTRODUCTION.md`. |
| `include/SushiRuntime/distributed/`<br>`src/distributed/` | `docs/guides/DISTRIBUTED_GUIDE.md`<br>`docs/guides/ARCHITECTURE.md` | If the distributed architecture, master-worker protocol, transport layer, or offload policies change, update `DISTRIBUTED_GUIDE.md` **first**, then summarize the changes in **Section 11** of `ARCHITECTURE.md`. |
| `cli/` folder or installer scripts (`install.sh`, `install.ps1`, `cli/install.py`) | `docs/guides/CLI_GUIDE.md`<br>`README.md`<br>`docs/guides/INTRODUCTION.md` | If CLI commands (e.g., `sr project build`) or setup steps change, update `CLI_GUIDE.md`. Also check the Quick Start sections in `INTRODUCTION.md` and `README.md`. |
| Example projects (`examples/`) | `docs/guides/INTRODUCTION.md`<br>`docs/guides/DISTRIBUTED_GUIDE.md` | If examples change, ensure the tutorial code blocks in `INTRODUCTION.md` (e.g., "Your first program") are updated to match. |

---

## Special Instructions for Vibecoding

If you are an AI assistant (Agent) executing a task on this codebase, you MUST adhere to the following rules:

1. **Public API Changes (`include/SushiRuntime/`)**: If you change public methods of a class, struct parameters, or user-facing objects like `TaskMetadata`, immediately open `docs/guides/INTRODUCTION.md` and correct the code snippets to match the new API.
2. **Lock-free and Concurrency Changes**: If you modify atomic operations or pool algorithms in `memory/` or `scheduler/`, update the relevant design explanations in `docs/guides/ARCHITECTURE.md`.
3. **Self-Check Before Completion**: Before finishing a task, ask yourself: *"Where does the code I just changed fit in the `DOCS_UPDATE_MAP.md` table? Which markdown files do I need to update?"*
4. **New Files/Modules**: If you add a completely new module, update both **Section 15 (Directory map)** in `ARCHITECTURE.md` and this `DOCS_UPDATE_MAP.md` file.
