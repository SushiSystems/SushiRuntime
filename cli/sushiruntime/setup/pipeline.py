"""Pipeline core: the ``Step`` contract, shared context, and the runner.

These are the abstractions the rest of the installer depends on. Concrete steps
live in ``steps.py``; concrete package managers and dependency sources live in
their own modules. Nothing here imports a concrete implementation, which keeps
the dependency direction pointing at the abstractions (Dependency Inversion).
"""

from __future__ import annotations

import enum
from abc import ABC, abstractmethod
from dataclasses import dataclass, field

from .. import console
from ..config import Config


class StepResult(enum.Enum):
    """Outcome of a single pipeline step.

    ``SKIPPED`` is distinct from ``OK`` so the summary can say "nothing to do"
    versus "did work"; both let the pipeline continue. ``FAILED`` stops it.
    """

    OK = "ok"
    SKIPPED = "skipped"
    FAILED = "failed"


@dataclass
class InstallContext:
    """State shared across steps for one installer run.

    Earlier steps populate fields that later steps read (e.g. ``DetectStep``
    fills ``detected``; ``ConfigureStep`` fills ``resolved_paths``). Keeping the
    shared state in one object means steps stay decoupled from one another — they
    talk through the context, never directly.
    """

    cfg: Config
    gpu: bool = False
    dry_run: bool = False
    everything: bool = False
    # Opt-in: also install the heavy Intel oneAPI DPC++ compiler (icx/icpx).
    # Off by default — the lean intel-llvm + adaptivecpp toolchains are installed
    # unconditionally, mirroring the Dockerfile's WITH_ONEAPI=0 default.
    oneapi: bool = False

    # Populated by DetectStep: tool/dependency name -> present?
    detected: dict[str, bool] = field(default_factory=dict)
    # Populated by InstallDepsStep: package names actually installed this run.
    installed: list[str] = field(default_factory=list)
    # Populated by ConfigureStep: config field name -> resolved absolute path.
    resolved_paths: dict[str, str] = field(default_factory=dict)


class Step(ABC):
    """One unit of installer work.

    Every step honors the same contract — ``run(ctx) -> StepResult`` — so the
    pipeline can drive any sequence of steps without knowing what each does
    (Liskov / Open-Closed).
    """

    #: Human-readable name, shown in the pipeline log.
    name: str = "step"

    @abstractmethod
    def run(self, ctx: InstallContext) -> StepResult:  # pragma: no cover - abstract
        raise NotImplementedError


class InstallPipeline:
    """Runs an ordered list of steps, stopping on the first failure."""

    def __init__(self, steps: list[Step]) -> None:
        self._steps = steps

    @property
    def steps(self) -> list[Step]:
        return list(self._steps)

    def run(self, ctx: InstallContext) -> bool:
        """Execute every step in order. Return True if none failed."""
        from rich.progress import Progress, SpinnerColumn, TextColumn, BarColumn, TimeElapsedColumn

        with Progress(
            SpinnerColumn(),
            TextColumn("[progress.description]{task.description}"),
            BarColumn(),
            TextColumn("[progress.percentage]{task.percentage:>3.0f}%"),
            TimeElapsedColumn(),
            console=console.console,
            transient=False,
        ) as progress:
            task = progress.add_task("[bold cyan]Starting Setup...", total=len(self._steps))
            
            for step in self._steps:
                progress.update(task, description=f"[bold cyan]Running Setup...[/bold cyan] [yellow]({step.name})[/yellow]")
                console.header(f"setup: {step.name}")
                result = step.run(ctx)
                if result is StepResult.FAILED:
                    progress.update(task, description=f"[bold red]Setup failed at '{step.name}'[/bold red]")
                    console.error(f"Step '{step.name}' failed; stopping pipeline.")
                    return False
                if result is StepResult.SKIPPED:
                    console.info(f"Step '{step.name}' skipped (nothing to do).")
                else:
                    console.success(f"Step '{step.name}' done.")
                progress.advance(task)
            
            progress.update(task, description="[bold green]Setup Complete![/bold green]")
        return True
