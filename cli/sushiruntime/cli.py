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
from .services import diag as diag_svc
from .services import docker as docker_svc
from .services import project as project_svc
from .services import select as select_svc
from .services import setup as setup_svc
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
config_app = typer.Typer(help="Inspect resolved configuration.",
                         no_args_is_help=True)
env_app = typer.Typer(help="Inspect the build environment.",
                      no_args_is_help=True)
setup_app = typer.Typer(
    help="One-shot installer: detect, install deps, configure, verify.",
    no_args_is_help=True)
app.add_typer(project_app, name="project")
app.add_typer(docker_app, name="docker")
app.add_typer(config_app, name="config")
app.add_typer(env_app, name="env")
app.add_typer(setup_app, name="setup")


class DockerAction(str, enum.Enum):
    build = "build"
    run = "run"


class Profile(str, enum.Enum):
    """Install profile: which SYCL toolchains `sr setup` provisions.

    lightweight = AdaptiveCpp only (smallest), normal = intel-llvm + AdaptiveCpp
    (default), full = the above plus the heavy Intel oneAPI DPC++ compiler.
    """
    lightweight = "lightweight"
    normal = "normal"
    full = "full"


# --------------------------------------------------------------------------- #
# toolchain (SYCL toolchain)
# --------------------------------------------------------------------------- #
@app.command("toolchain")
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
    repeat: int = typer.Option(
        0, "--repeat", "-r", min=0,
        help="Re-run each test up to N times, stopping on the first failure "
             "(ctest --repeat until-fail). Handy for hunting flaky tests."),
):
    """Run the test suite via CTest labels.

    For GTest-level knobs (shuffle, break-on-failure) run the binary directly:
    sr project run sr_functional_tests -- --gtest_shuffle --gtest_break_on_failure
    """
    raise typer.Exit(project_svc.test(suite, distributed, filter, asan, repeat))


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


@docker_app.command("run")
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
# setup (one-shot installer)
# --------------------------------------------------------------------------- #
@setup_app.command("all")
def setup_all(
    profile: Profile = typer.Option(
        Profile.normal, "--profile", "-p", case_sensitive=False,
        help="SYCL toolchain profile: lightweight (acpp only) | normal "
             "(intel-llvm + acpp) | full (+ oneAPI)."),
    gpu: bool = typer.Option(True, "--gpu/--no-gpu", help="Include GPU/CUDA deps (on by default; use --no-gpu to skip)."),
    oneapi: bool = typer.Option(False, "--oneapi", help="Alias for --profile full (back-compat)."),
    dry_run: bool = typer.Option(False, "--dry-run", help="Show, don't change."),
):
    """Detect, install dependencies, write config, then build and smoke-test."""
    raise typer.Exit(setup_svc.run("all", gpu=gpu, dry_run=dry_run, oneapi=oneapi, profile=profile.value))


@setup_app.command("detect")
def setup_detect(
    profile: Profile = typer.Option(
        Profile.normal, "--profile", "-p", case_sensitive=False,
        help="SYCL toolchain profile: lightweight | normal | full."),
    gpu: bool = typer.Option(True, "--gpu/--no-gpu", help="Include GPU/CUDA deps (on by default; use --no-gpu to skip)."),
    oneapi: bool = typer.Option(False, "--oneapi", help="Alias for --profile full (back-compat)."),
):
    """Inventory the machine: tools, compiler, dependencies, GPU."""
    raise typer.Exit(setup_svc.run("detect", gpu=gpu, dry_run=False, oneapi=oneapi, profile=profile.value))


@setup_app.command("install")
def setup_install(
    profile: Profile = typer.Option(
        Profile.normal, "--profile", "-p", case_sensitive=False,
        help="SYCL toolchain profile: lightweight | normal | full."),
    gpu: bool = typer.Option(True, "--gpu/--no-gpu", help="Include GPU/CUDA deps (on by default; use --no-gpu to skip)."),
    oneapi: bool = typer.Option(False, "--oneapi", help="Alias for --profile full (back-compat)."),
    dry_run: bool = typer.Option(False, "--dry-run", help="Show, don't change."),
):
    """Install missing dependencies (apt on Linux, winget+vcpkg on Windows)."""
    raise typer.Exit(setup_svc.run("install", gpu=gpu, dry_run=dry_run, oneapi=oneapi, profile=profile.value))


@setup_app.command("configure")
def setup_configure(
    profile: Profile = typer.Option(
        Profile.normal, "--profile", "-p", case_sensitive=False,
        help="SYCL toolchain profile: lightweight | normal | full."),
    gpu: bool = typer.Option(True, "--gpu/--no-gpu", help="Include GPU/CUDA deps (on by default; use --no-gpu to skip)."),
    oneapi: bool = typer.Option(False, "--oneapi", help="Alias for --profile full (back-compat)."),
    dry_run: bool = typer.Option(False, "--dry-run", help="Show, don't change."),
):
    """Generate config.local.toml from probed tool paths."""
    raise typer.Exit(setup_svc.run("configure", gpu=gpu, dry_run=dry_run, oneapi=oneapi, profile=profile.value))


@setup_app.command("verify")
def setup_verify(
    profile: Profile = typer.Option(
        Profile.normal, "--profile", "-p", case_sensitive=False,
        help="SYCL toolchain profile: lightweight | normal | full."),
    gpu: bool = typer.Option(True, "--gpu/--no-gpu", help="Include GPU/CUDA deps (on by default; use --no-gpu to skip)."),
    oneapi: bool = typer.Option(False, "--oneapi", help="Alias for --profile full (back-compat)."),
):
    """Build the project and run the functional smoke test."""
    raise typer.Exit(setup_svc.run("verify", gpu=gpu, dry_run=False, oneapi=oneapi, profile=profile.value))


@setup_app.command("uninstall")
def setup_uninstall(
    gpu: bool = typer.Option(False, "--gpu", help="Include GPU-only deps in removal."),
    dry_run: bool = typer.Option(False, "--dry-run", help="Show what would be removed without changing anything."),
    everything: bool = typer.Option(
        False, "--everything",
        help=(
            "[bold red]WARNING:[/bold red] Also removes cmake, git, and ninja "
            "that were installed by the direct-download fallback. "
            "Do not use this flag if those tools were already present before "
            "running the installer — it will remove your existing installation."
        ),
    ),
):
    """Remove packages, vcpkg ports, and config files placed by `sr setup`.

    By default only vcpkg C++ library ports and [cyan]config.local.toml[/cyan]
    are removed. Pass [bold]--everything[/bold] to also uninstall the toolchain
    binaries (cmake, git, ninja) that the installer may have downloaded.

    [bold yellow]Warning:[/bold yellow] [bold]--everything[/bold] is irreversible
    and will remove cmake and git system-wide on Windows. Only use it on machines
    where SushiRuntime was the sole reason those tools were installed.
    """
    raise typer.Exit(setup_svc.uninstall(gpu=gpu, dry_run=dry_run, everything=everything))


# --------------------------------------------------------------------------- #
# config / env (diagnostics)
# --------------------------------------------------------------------------- #
@config_app.command("show")
def config_show():
    """Print the resolved config and where each value came from."""
    raise typer.Exit(diag_svc.config_show())


@env_app.command("dump")
def env_dump(
    all: bool = typer.Option(
        False, "--all", help="Show every variable, not just build-relevant ones."),
    asan: bool = typer.Option(
        False, "--asan", help="Inspect the build-asan tree's environment."),
):
    """Print the environment cmake/ctest/run subprocesses execute under."""
    raise typer.Exit(diag_svc.env_dump(show_all=all, asan=asan))


if __name__ == "__main__":
    app()
