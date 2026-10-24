# Layered execution: plan of the run

**Status:** Open — R1 landed, R3 next
**Implements:** LAYERED_EXECUTION#R1, LAYERED_EXECUTION#R2, LAYERED_EXECUTION#R3, LAYERED_EXECUTION#R4, LAYERED_EXECUTION#R5, LAYERED_EXECUTION#R6, LAYERED_EXECUTION#R7, LAYERED_EXECUTION#R8, LAYERED_EXECUTION#R9, LAYERED_EXECUTION#R10, LAYERED_EXECUTION#R11, LAYERED_EXECUTION#R12, LAYERED_EXECUTION#R13

## How it runs

The owner chose on 2026-10-24: one orchestrator session over the four repositories, one worker
per task, tasks in sequence. A worker checks its C++ with `clang -fsyntax-only` and builds
nothing; the orchestrator runs `sr build -j 4` and `sr test` to close each task and commits.
Work lands on `feature/layered_execution` in each repository.

The order across repositories follows ruling 10 of the design: this repository's R1 to R10
first, then SushiBLAS, then SushiAI. A SushiBLAS, SushiAI or SushiEngine task starts once the
runtime tasks it waits on are built; each of those repositories keeps its own work folder.

## Tasks

| Id | Waits on | State |
| --- | --- | --- |
| R1 | nothing | built |
| R3 | nothing | planned |
| R2 | R1 | planned |
| R4 | R1, R3 | planned |
| R5 | R2, R4 | planned |
| R6 | R5 | planned |
| R7 | R3 | planned |
| R8 | R7 | planned |
| R9 | R7 | planned |
| R10 | R5, R8, R9 | planned |
| R11 | nothing | planned |
| R12 | nothing | planned |
| R13 | R10, two of the owner's machines | planned |
