"""`sr setup` service: thin wrapper that runs the installer pipeline.

Mirrors the other services (project/docker/diag): the CLI command parses flags,
this builds the pipeline via the composition root, runs it, and maps success to
an exit code. All the real logic lives in the ``sushiruntime.setup`` package.
"""

from __future__ import annotations

from .. import console
from ..setup import build_pipeline


def run(step: str = "all", gpu: bool = False, dry_run: bool = False) -> int:
    """Run one step (or the whole pipeline) and return a process exit code."""
    console.header("SushiRuntime Setup")
    if dry_run:
        console.info("Dry-run: showing actions without changing the system.")
    if gpu:
        console.info("GPU mode: CUDA-capable dependencies included.")

    try:
        pipeline, ctx = build_pipeline(only=step, gpu=gpu, dry_run=dry_run)
    except (ValueError, FileNotFoundError) as exc:
        console.error(str(exc))
        return 1

    ok = pipeline.run(ctx)
    if ok:
        console.success("Setup completed.")
        if step in ("all", "configure"):
            console.info("Next: `sr project build` (already run by verify) "
                         "or `sr project test --suite functional`.")
        return 0
    console.error("Setup did not complete. See messages above.")
    return 1
