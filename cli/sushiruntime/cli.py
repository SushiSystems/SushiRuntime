# cli.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""SushiRuntime developer CLI (`sr`).

Thin Typer layer: commands only parse arguments and delegate to the service
layer in ``sushiruntime.services``.
"""

from __future__ import annotations

from typing import Callable, NamedTuple, Optional

import typer
from sushicore import entry
from sushicore.diag_commands import register_diagnostic_commands
from sushicore.docs_bundle import ApiSource, register_docs_commands
from sushicore.provision.commands import ModuleProvision, register_provision_commands
from sushicore.root_options import register_root_options
from sushicore.typer_help import help_group

from . import console
from .aliases import ALIASES
from .config import PROFILE, find_project_root, load_config
from .services import diag as diag_svc
from .services import docker as docker_svc
from .services import packaging as packaging_svc
from .services import project as project_svc
from .services import select as select_svc
from .services.project import BuildType, DistributedRole, Label

K_DISTRIBUTION = "sushiruntime-cli"
K_TOOLCHAIN = "Toolchain"
K_PROJECT = "Project"
K_DIAGNOSTICS = "Diagnostics"
K_CONTAINER = "Container"
K_API_XML = "build/docs/api-site/xml"

_help_group = help_group(console.current)

app = typer.Typer(
    name="sr",
    cls=_help_group,
    help="SushiRuntime developer CLI — build, test, run, and container management.",
    no_args_is_help=True,
    add_completion=False,
    rich_markup_mode="rich",
)

register_root_options(app, distribution=K_DISTRIBUTION)


def _group(help_text: str) -> typer.Typer:
    """Returns a sub-application that draws the shared help page under *help_text*."""
    return typer.Typer(
        cls=_help_group,
        help=help_text,
        no_args_is_help=True,
        rich_markup_mode="rich",
    )


container_app = _group("Build and run the development container.")
app.add_typer(container_app, name="container", rich_help_panel=K_CONTAINER)

docker_alias_app = _group("The old spelling of `sr container`.")
app.add_typer(docker_alias_app, name="docker", hidden=True)


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


@app.command(
    "build",
    rich_help_panel=K_PROJECT,
    epilog="sr build\n"
           "sr build --type debug --clean  # Rebuild from an empty tree\n"
           "sr build --no-cuda  # Portable CPU/OpenCL path\n"
           "sr build --jobs 8  # Compile with eight parallel jobs\n"
           "sr build --distributed --no-test\n"
           "sr build --build-dir build/pkg-x --lean  # What a consumer's package build runs",
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
    jobs: Optional[int] = typer.Option(
        None, "--jobs", "-j", min=1,
        help="Parallel compile jobs. When omitted the build tool decides, and "
             "CMAKE_BUILD_PARALLEL_LEVEL is honoured."),
    build_dir: Optional[str] = typer.Option(
        None, "--build-dir", metavar="PATH",
        help="Build in this tree, below the checkout, instead of build/ or build-asan/."),
    lean: bool = typer.Option(
        False, "--lean",
        help="Build no tests, examples or benchmarks; what a package build of the runtime uses."),
):
    """Configure and build the project."""
    raise typer.Exit(project_svc.build(type, distributed, no_cuda, clean, force,
                                       tests=not no_test, jobs=jobs, lean=lean,
                                       build_dir=build_dir))


@app.command(
    "test",
    rich_help_panel=K_PROJECT,
    epilog="sr test  # Every size, without the quarantined tests\n"
           "sr test --label small\n"
           "sr test --module scheduler  # The tests of one module\n"
           "sr test --filter 'Suite.Case'  # ctest -R regex\n"
           "sr test --repeat 5  # Hunt a flaky test",
)
def test(
    label: Optional[Label] = typer.Option(
        None, "--label", "-l", case_sensitive=False,
        help="Run one size (small, medium, large), the quarantined tests or the package check."),
    distributed: bool = typer.Option(
        False, "--distributed", help="Run against a distributed-enabled build."),
    filter: Optional[str] = typer.Option(
        None, "--filter", "-f",
        help="ctest -R regex over 'Suite.Case' test names."),
    module: Optional[str] = typer.Option(
        None, "--module", "-m",
        help="Run the tests of one module (its folder name); replaces --label."),
    asan: bool = typer.Option(
        False, "--asan", help="Run the build-asan tree instead of build."),
    repeat: int = typer.Option(
        0, "--repeat", "-r", min=0,
        help="Re-run each test up to N times, stopping on the first failure "
             "(ctest --repeat until-fail). Handy for hunting flaky tests."),
    jobs: Optional[int] = typer.Option(
        None, "--jobs", "-j", min=1,
        help="Tests run at once. When omitted ctest decides, and "
             "CTEST_PARALLEL_LEVEL is honoured."),
):
    """Run the tests via CTest labels.

    For GTest-level knobs (shuffle, break-on-failure) run the binary directly:
    sr run sr_api_tests -- --gtest_shuffle --gtest_break_on_failure
    """
    raise typer.Exit(project_svc.test(label, distributed, filter, asan, repeat, module, jobs))


@app.command(
    "bench",
    rich_help_panel=K_PROJECT,
    epilog="sr bench\n"
           "sr bench --filter 'Stress_*'  # --gtest_filter for every program",
)
def bench(
    distributed: bool = typer.Option(
        False, "--distributed", help="Run against a distributed-enabled build."),
    filter: Optional[str] = typer.Option(
        None, "--filter", "-f", help="--gtest_filter passed to every benchmark program."),
):
    """Run every benchmark program the build produced."""
    raise typer.Exit(project_svc.bench(distributed, filter))


@app.command(
    "run",
    rich_help_panel=K_PROJECT,
    epilog="sr run sr_api_benchmarks  # Exact name, then substring match\n"
           "sr run --sort  # Pick an executable interactively\n"
           "sr run sr_api_benchmarks -- --gtest_filter='Performance_*'  # Arguments after -- go to it",
    context_settings={"allow_extra_args": True, "ignore_unknown_options": True},
)
def run(
    ctx: typer.Context,
    target: Optional[str] = typer.Argument(
        None, help="Executable name to run (exact, then substring match)."),
    sort: bool = typer.Option(
        False, "--sort", help="Interactively pick an executable."),
    distributed: Optional[DistributedRole] = typer.Option(
        None, "--distributed", metavar="ROLE", case_sensitive=False,
        help="Forward a role (master|worker) to the application."),
    asan: bool = typer.Option(
        False, "--asan", help="Run binaries from the build-asan tree."),
):
    """Run a built executable. Args after `--` are forwarded to it."""
    role = distributed.value if distributed else None
    raise typer.Exit(
        project_svc.run(target=target, sort=sort, distributed_role=role,
                        asan=asan, app_args=list(ctx.args))
    )


@app.command(
    "package",
    rich_help_panel=K_PROJECT,
    epilog="sr package\n"
           "sr package --prefix build/dist --no-archive  # Install only, no CPack\n"
           "sr package --build-dir build/pkg-x --prefix ../staging --no-archive --force\n"
           "sr package --prefix ../staging --force  # Clear a directory sr did not stage",
)
def package(
    build_dir: Optional[str] = typer.Option(
        None, "--build-dir", metavar="PATH",
        help="The built tree to install from, below the checkout. Defaults to build."),
    prefix: Optional[str] = typer.Option(
        None, "--prefix",
        help="Where to install; the directory is emptied first. Defaults to build/dist."),
    no_archive: bool = typer.Option(
        False, "--no-archive", help="Install only; do not run CPack."),
    force: bool = typer.Option(
        False, "--force",
        help="Clear a --prefix that holds files this command did not stage."),
):
    """Install the built tree to a prefix and archive it for redistribution.

    This produces the artifact; it does not check it. `sr test --label package`
    is the check — it builds tests/package/ against an installed prefix and runs
    it with nothing on PATH but the prefix, which is what catches a package that
    only works because this machine already has the SYCL runtime installed.
    """
    raise typer.Exit(
        packaging_svc.package(prefix=prefix, archive=not no_archive, force=force,
                              build_dir=build_dir))


@app.command(
    "clean",
    rich_help_panel=K_PROJECT,
    epilog="sr clean",
)
def clean():
    """Remove the build/ and build-asan/ trees."""
    raise typer.Exit(project_svc.clean())


def _api_source() -> ApiSource:
    """Returns how `sr` builds the API reference and where Doxygen leaves its XML."""
    return ApiSource(build=project_svc.docs, xml_dir=find_project_root() / K_API_XML)


def _docs_alias() -> None:
    """Builds the API reference; the callback behind the old spelling `sr doxygen`."""
    raise typer.Exit(project_svc.docs())


register_docs_commands(
    app,
    program=PROFILE.program,
    panel=K_PROJECT,
    project_root=find_project_root,
    api=_api_source,
    report=lambda line: console.success(line),
    group_cls=_help_group,
)


@container_app.command(
    "build",
    epilog="sr container build\n"
           "sr container build --no-cache --oneapi  # Every layer again, with icpx",
)
def container_build(
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


@container_app.command(
    "run",
    epilog="sr container run\n"
           "sr container run --no-gpu  # Headless or CI machines\n"
           "sr container run --admin  # Privileged, for profiling",
)
def container_run(
    admin: bool = typer.Option(
        False, "--admin",
        help="Run with --privileged --cap-add=SYS_ADMIN (for profiling)."),
    no_gpu: bool = typer.Option(
        False, "--no-gpu",
        help="Omit --gpus all (useful when CUDA is disabled or no GPU is available)."),
):
    """Start the developer container with the repo mounted."""
    raise typer.Exit(docker_svc.run(admin=admin, no_gpu=no_gpu))


class _AliasRow(NamedTuple):
    """Holds where an old spelling answers, its word there, and the command it runs."""

    parent: typer.Typer
    word: str
    callback: Callable[..., None]
    old: str


_ALIAS_ROWS: tuple[_AliasRow, ...] = (
    _AliasRow(app, "doxygen", _docs_alias, "sr doxygen"),
    _AliasRow(docker_alias_app, "build", container_build, "sr docker build"),
    _AliasRow(docker_alias_app, "run", container_run, "sr docker run"),
)

for _row in _ALIAS_ROWS:
    ALIASES.command(_row.parent, _row.word, _row.callback, _row.old, hidden=_row.parent is app)


register_diagnostic_commands(
    app, diag_svc.DIAGNOSTICS, program=PROFILE.program, panel=K_DIAGNOSTICS, env=False)


@app.command(
    "env",
    rich_help_panel=K_DIAGNOSTICS,
    epilog="sr env\n"
           "sr env --all  # Every variable, not just the build-relevant ones\n"
           "sr env --asan  # The build-asan tree's environment",
)
def env(
    all: bool = typer.Option(
        False, "--all", "-a", help="Show every variable, not just build-relevant ones."),
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


def _print_error(message: str) -> None:
    """Prints one error line through the CLI's console."""
    console.error(message)


def main() -> None:
    """Runs `sr`, turning a failure the user can act on into one line and exit code 1."""
    entry.run(app, _print_error)


if __name__ == "__main__":
    main()
