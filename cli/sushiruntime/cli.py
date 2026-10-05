# cli.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""SushiRuntime developer CLI (`sr`).

Thin Typer layer: commands only parse arguments and delegate to the service
layer in ``sushiruntime.services``.
"""

from __future__ import annotations

import enum
import sys
from typing import List, Optional

import typer
from sushicore.provision.commands import ModuleProvision, register_provision_commands
from sushicore.typer_help import help_group

from . import console
from .config import PROFILE, find_project_root, load_config
from .services import diag as diag_svc
from .services import docker as docker_svc
from .services import packaging as packaging_svc
from .services import project as project_svc
from .services import select as select_svc
from .services.project import BuildType, Suite

K_TOOLCHAIN = "Toolchain"
K_PROJECT = "Project"
K_DIAGNOSTICS = "Diagnostics"
K_CONTAINER = "Container"

_help_group = help_group(console.current)

app = typer.Typer(
    name="sr",
    cls=_help_group,
    help="SushiRuntime developer CLI — build, test, run, and Docker management.",
    no_args_is_help=True,
    add_completion=False,
    rich_markup_mode="rich",
)

docker_app = typer.Typer(
    cls=_help_group,
    help="Docker environment management.",
    no_args_is_help=True,
    rich_markup_mode="rich",
)
app.add_typer(docker_app, name="docker", rich_help_panel=K_CONTAINER)


class DockerAction(str, enum.Enum):
    build = "build"
    run = "run"


# --------------------------------------------------------------------------- #
# toolchain (SYCL toolchain)
# --------------------------------------------------------------------------- #
@app.command(
    "toolchain",
    rich_help_panel=K_TOOLCHAIN,
    epilog="sr toolchain  # List the choices and show the active one\n"
           "sr toolchain adaptivecpp  # Persist a toolchain to config.local.toml",
)
def toolchain(
    name: Optional[str] = typer.Argument(
        None, help="SYCL toolchain: intel-llvm | adaptivecpp | oneapi. "
                   "Omit to list choices and show the active one."),
):
    """Choose the SYCL toolchain used for builds (persisted to config.local.toml)."""
    if name is None:
        raise typer.Exit(select_svc.show())
    raise typer.Exit(select_svc.select(name))



# --------------------------------------------------------------------------- #
# build / test / run / clean / doxygen
# --------------------------------------------------------------------------- #
@app.command(
    "build",
    rich_help_panel=K_PROJECT,
    epilog="sr build\n"
           "sr build --type debug --clean  # Rebuild from an empty tree\n"
           "sr build --no-cuda  # Portable CPU/OpenCL path\n"
           "sr build --distributed --no-test",
)
def build(
    type: BuildType = typer.Option(
        BuildType.release, "--type", "-t", case_sensitive=False,
        help="Build type. 'asan' uses an isolated build-asan tree (CUDA off)."),
    distributed: bool = typer.Option(
        False, "--distributed", help="Enable the master-worker distributed layer."),
    no_cuda: bool = typer.Option(
        False, "--no-cuda",
        help="Build the portable CPU/OpenCL path instead of detecting accelerators."),
    clean: bool = typer.Option(
        False, "--clean", help="Delete the build tree before configuring."),
    force: bool = typer.Option(
        False, "--force",
        help="If the selected toolchain's compiler is missing, fall back to "
             "clang++/clang instead of stopping."),
    no_test: bool = typer.Option(
        False, "--no-test",
        help="Skip compiling the test suite (SR_BUILD_TESTS=OFF). Tests are built by default."),
):
    """Configure and build the project."""
    raise typer.Exit(project_svc.build(type, distributed, no_cuda, clean, force, tests=not no_test))


@app.command(
    "test",
    rich_help_panel=K_PROJECT,
    epilog="sr test\n"
           "sr test --suite all\n"
           "sr test --filter 'Suite.Case'  # ctest -R regex\n"
           "sr test --repeat 5  # Hunt a flaky test",
)
def test(
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
    repeat: int = typer.Option(
        0, "--repeat", "-r", min=0,
        help="Re-run each test up to N times, stopping on the first failure "
             "(ctest --repeat until-fail). Handy for hunting flaky tests."),
):
    """Run the test suite via CTest labels.

    For GTest-level knobs (shuffle, break-on-failure) run the binary directly:
    sr run sr_functional_tests -- --gtest_shuffle --gtest_break_on_failure
    """
    raise typer.Exit(project_svc.test(suite, distributed, filter, asan, repeat))


@app.command(
    "run",
    rich_help_panel=K_PROJECT,
    epilog="sr run sr_benchmark_tests  # Exact name, then substring match\n"
           "sr run --sort  # Pick an executable interactively\n"
           "sr run sr_benchmark_tests -- --gtest_filter='Bench.*'  # Arguments after -- go to it",
    context_settings={"allow_extra_args": True, "ignore_unknown_options": True},
)
def run(
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
    """Run a built executable. Args after `--` are forwarded to it."""
    role = distributed if distributed and distributed.lower() in ("master", "worker") else None
    if distributed and role is None:
        console.warn(f"Unknown --distributed role '{distributed}'; not forwarding.")
    raise typer.Exit(
        project_svc.run(target=target, sort=sort, distributed_role=role,
                        asan=asan, app_args=list(ctx.args))
    )


@app.command(
    "package",
    rich_help_panel=K_PROJECT,
    epilog="sr package\n"
           "sr package --prefix build/dist --no-archive  # Install only, no CPack",
)
def package(
    prefix: Optional[str] = typer.Option(
        None, "--prefix", help="Where to install. Defaults to build/dist."),
    no_archive: bool = typer.Option(
        False, "--no-archive", help="Install only; do not run CPack."),
):
    """Install the built tree to a prefix and archive it for redistribution.

    This produces the artifact; it does not check it. `sr test --suite package`
    is the check — it builds tests/package/ against an installed prefix and runs
    it with nothing on PATH but the prefix, which is what catches a package that
    only works because this machine already has the SYCL runtime installed.
    """
    raise typer.Exit(packaging_svc.package(prefix=prefix, archive=not no_archive))


@app.command(
    "clean",
    rich_help_panel=K_PROJECT,
    epilog="sr clean",
)
def clean():
    """Remove the build/ and build-asan/ trees."""
    raise typer.Exit(project_svc.clean())


@app.command(
    "doxygen",
    rich_help_panel=K_PROJECT,
    epilog="sr doxygen",
)
def doxygen():
    """Generate Doxygen documentation."""
    raise typer.Exit(project_svc.doxygen())


# --------------------------------------------------------------------------- #
# docker
# --------------------------------------------------------------------------- #
@docker_app.command(
    "build",
    epilog="sr docker build\n"
           "sr docker build --no-cache --oneapi  # Every layer again, with icpx",
)
def docker_build(
    no_cache: bool = typer.Option(
        False, "--no-cache",
        help="Rebuild every layer from scratch (ignore Docker's cache)."),
    oneapi: bool = typer.Option(
        False, "--oneapi",
        help="Also install the Intel oneAPI DPC++ compiler (icpx) in the image "
             "(several GB; needed only for the 'oneapi' toolchain)."),
):
    """Build the sushiruntime Docker image."""
    raise typer.Exit(docker_svc.build(no_cache=no_cache, oneapi=oneapi))


@docker_app.command(
    "run",
    epilog="sr docker run\n"
           "sr docker run --no-gpu  # Headless or CI machines\n"
           "sr docker run --admin  # Privileged, for profiling",
)
def docker_run(
    admin: bool = typer.Option(
        False, "--admin",
        help="Run with --privileged --cap-add=SYS_ADMIN (for profiling)."),
    no_gpu: bool = typer.Option(
        False, "--no-gpu",
        help="Omit --gpus all (useful when CUDA is disabled or no GPU is available)."),
):
    """Start the developer container with the repo mounted."""
    raise typer.Exit(docker_svc.run(admin=admin, no_gpu=no_gpu))


# --------------------------------------------------------------------------- #
# config / env (diagnostics)
# --------------------------------------------------------------------------- #
@app.command(
    "config",
    rich_help_panel=K_DIAGNOSTICS,
    epilog="sr config",
)
def config():
    """Print the resolved config and where each value came from."""
    raise typer.Exit(diag_svc.config_show())


@app.command(
    "env",
    rich_help_panel=K_DIAGNOSTICS,
    epilog="sr env --all  # Every variable, not just the build-relevant ones\n"
           "sr env --asan  # The build-asan tree's environment",
)
def env(
    all: bool = typer.Option(
        False, "--all", help="Show every variable, not just build-relevant ones."),
    asan: bool = typer.Option(
        False, "--asan", help="Inspect the build-asan tree's environment."),
):
    """Print the environment cmake/ctest/run subprocesses execute under."""
    raise typer.Exit(diag_svc.env_dump(show_all=all, asan=asan))


register_provision_commands(app, ModuleProvision(
    profile=PROFILE,
    project_root=find_project_root,
    load_config=load_config,
    console=lambda: console,
    panel=K_DIAGNOSTICS,
    uses_base=True,
))


if __name__ == "__main__":
    app()
