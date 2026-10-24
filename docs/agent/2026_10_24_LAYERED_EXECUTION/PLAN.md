# Layered execution: plan of the run

**Status:** Open — R1 to R4 landed; R7 under way, with B1 and A0 in the other repositories
**Implements:** LAYERED_EXECUTION#R15, LAYERED_EXECUTION#R1, LAYERED_EXECUTION#R2, LAYERED_EXECUTION#R3, LAYERED_EXECUTION#R4, LAYERED_EXECUTION#R5, LAYERED_EXECUTION#R6, LAYERED_EXECUTION#R7, LAYERED_EXECUTION#R8, LAYERED_EXECUTION#R9, LAYERED_EXECUTION#R10, LAYERED_EXECUTION#R11, LAYERED_EXECUTION#R12, LAYERED_EXECUTION#R13

## How it runs

The owner chose on 2026-10-24: one orchestrator session over the four repositories, one worker
per task, up to four workers at once on tasks whose files are disjoint. A worker checks its C++ with `clang -fsyntax-only` and builds
nothing; the orchestrator runs the checkers under `tools/` and commits each task. The owner
ruled on 2026-10-24 that no task is built on its own: each repository is built and tested once,
at the end of the programme, and AdaptiveCpp is checked then too. Every task is written for
AdaptiveCpp as well as intel-llvm. Work lands on `feature/layered_execution` in each repository.

The order across repositories follows ruling 10 of the design: this repository's R1 to R10
first, then SushiBLAS, then SushiAI. A SushiBLAS, SushiAI or SushiEngine task starts once the
runtime tasks it waits on are built; each of those repositories keeps its own work folder.

## Tasks

| Id | Waits on | State |
| --- | --- | --- |
| R1 | nothing | built |
| R3 | nothing | built |
| R2 | R1 | built |
| R4 | R1, R3 | built |
| R5 | R2, R4 | built |
| R6 | R5 | built |
| R7 | R3 | built |
| R8 | R7 | built |
| R9 | R7 | built; KI-6 and RW-17 close when the final build passes its tests |
| R10 | R5, R8, R9 | built |
| R11 | nothing | planned |
| R12 | nothing | built |
| R13 | R10, two of the owner's machines | planned |
| R15 | R1 | built |
