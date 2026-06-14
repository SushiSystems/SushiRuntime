"""SushiRuntime developer CLI (`sr`).

Thin Typer layer: commands only parse arguments and delegate to the service
layer in ``sushiruntime.services``.
"""

from __future__ import annotations

import enum
import sys
from typing import List, Optional

import typer

from . import console
from .services import docker as docker_svc
from .services import project as project_svc
from .services.project import BuildType, Suite

app = typer.Typer(
    name="sr",
    help="SushiRuntime developer CLI — build, test, run, and Docker management.",
    no_args_is_help=True,
    add_completion=False,
    rich_markup_mode="rich",
)

project_app = typer.Typer(help="C++ project build / test / run management.",
                          no_args_is_help=True)
docker_app = typer.Typer(help="Docker environment management.",
                         no_args_is_help=True)
app.add_typer(project_app, name="project")
app.add_typer(docker_app, name="docker")


class DockerAction(str, enum.Enum):
    build = "build"
    run = "run"


# --------------------------------------------------------------------------- #
# project
# --------------------------------------------------------------------------- #
@project_app.command("build")
def project_build(
    type: BuildType = typer.Option(
        BuildType.release, "--type", "-t", case_sensitive=False,
        help="Build type. 'asan' uses an isolated build-asan tree (CUDA off)."),
    distributed: bool = typer.Option(
        False, "--distributed", help="Enable the master-worker distributed layer."),
    no_cuda: bool = typer.Option(
        False, "--no-cuda", help="Disable the SYCL CUDA backend (Linux only)."),
    clean: bool = typer.Option(
        False, "--clean", help="Delete the build tree before configuring."),
):
    """Configure and build the project."""
    raise typer.Exit(project_svc.build(type, distributed, no_cuda, clean))


@project_app.command("test")
def project_test(
    suite: Suite = typer.Option(
        Suite.functional, "--suite", "-s", case_sensitive=False,
        help="Which CTest label group to run ('all' runs every test)."),
    distributed: bool = typer.Option(
        False, "--distributed", help="Run against a distributed-enabled build."),
    filter: Optional[str] = typer.Option(
        None, "--filter", "-f",
        help="ctest -R regex over 'Suite.Case' test names."),
    asan: bool = typer.Option(
        False, "--asan", help="Run the build-asan tree instead of build."),
):
    """Run the test suite via CTest labels."""
    raise typer.Exit(project_svc.test(suite, distributed, filter, asan))


@project_app.command(
    "run",
    context_settings={"allow_extra_args": True, "ignore_unknown_options": True},
)
def project_run(
    ctx: typer.Context,
    target: Optional[str] = typer.Argument(
        None, help="Executable name to run (exact, then substring match)."),
    sort: bool = typer.Option(
        False, "--sort", help="Interactively pick an executable."),
    distributed: Optional[str] = typer.Option(
        None, "--distributed", metavar="ROLE",
        help="Forward a role (master|worker) to the application."),
    asan: bool = typer.Option(
        False, "--asan", help="Run binaries from the build-asan tree."),
):
    """Run a built executable. Args after `--` are forwarded to it.

    Example: sr project run sr_benchmark_tests -- --gtest_filter='Bench.*'
    """
    role = distributed if distributed and distributed.lower() in ("master", "worker") else None
    if distributed and role is None:
        console.warn(f"Unknown --distributed role '{distributed}'; not forwarding.")
    raise typer.Exit(
        project_svc.run(target=target, sort=sort, distributed_role=role,
                        asan=asan, app_args=list(ctx.args))
    )


@project_app.command("clean")
def project_clean():
    """Remove the build/ and build-asan/ trees."""
    raise typer.Exit(project_svc.clean())


@project_app.command("doxygen")
def project_doxygen():
    """Generate Doxygen documentation."""
    raise typer.Exit(project_svc.doxygen())


# --------------------------------------------------------------------------- #
# docker
# --------------------------------------------------------------------------- #
@docker_app.command("build")
def docker_build():
    """Build the sushiruntime Docker image."""
    raise typer.Exit(docker_svc.build())


@docker_app.command("run")
def docker_run(
    admin: bool = typer.Option(
        False, "--admin",
        help="Run with --privileged --cap-add=SYS_ADMIN (for profiling)."),
):
    """Start the developer container with the repo mounted."""
    raise typer.Exit(docker_svc.run(admin=admin))


if __name__ == "__main__":
    app()
