# test_build_trees.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests that each build type has its own tree, that Debug is the default and that a build runs
behind the memory gate."""

from __future__ import annotations

import os
from pathlib import Path
from types import SimpleNamespace

import pytest
from typer.testing import CliRunner

from sushiruntime import cli
from sushiruntime.config import Config
from sushiruntime.services import project
from sushiruntime.services.project import BuildType

pytestmark = pytest.mark.medium


class _Console:
    """Records the error lines a service prints and ignores the rest."""

    def __init__(self) -> None:
        """Starts with no recorded line."""
        self.errors: list[str] = []

    def error(self, message: str) -> None:
        """Records one error line."""
        self.errors.append(message)

    def __getattr__(self, name: str):
        """Returns a printer that prints nothing, for every other console call."""
        return lambda *args, **kwargs: None


class _Driver:
    """Records the configure and compile calls of a build over a tree that was never configured."""

    def __init__(self) -> None:
        """Starts with no call recorded."""
        self.configures: list[list[str]] = []
        self.compiles: list[tuple[Path, dict]] = []

    def cmake(self, cfg) -> str:
        """Returns the cmake name."""
        return "cmake"

    def needs_configure(self, build_dir, generator, root=None) -> bool:
        """Reports the tree as never configured."""
        return True

    def configure(self, args, cwd=None, env=None) -> int:
        """Records the arguments of one configure."""
        self.configures.append(list(args))
        return 0

    def compile(self, cfg, build_dir, **options) -> int:
        """Records the tree and the keyword arguments of one compile."""
        self.compiles.append((build_dir, options))
        return 0

    def clean_tree(self, tree) -> None:
        """Does nothing."""


@pytest.fixture
def build(monkeypatch, tmp_path):
    """Returns the driver, the console and the root of a build whose surroundings are stubbed."""
    driver, console = _Driver(), _Console()
    cfg = Config(platform="linux", generator="Ninja", cxx="clang++")
    packages = SimpleNamespace(definitions=lambda: [], run_dirs=lambda platform: [])
    monkeypatch.setattr(cfg, "sushi_packages", lambda *arguments, **options: packages)
    monkeypatch.setattr(project, "_DRIVER", driver)
    monkeypatch.setattr(project, "console", console)
    monkeypatch.setattr(project, "find_project_root", lambda: tmp_path)
    monkeypatch.setattr(project, "load_config", lambda: cfg)
    monkeypatch.setattr(project, "load_build_env", lambda cfg, tree: {})
    monkeypatch.setattr(project, "_resolve_compiler", lambda *a, **k: ("clang", "clang++"))
    monkeypatch.setattr(project, "configure_arguments", lambda *a: [])
    monkeypatch.setattr(project.library_packages, "record_run_dirs", lambda *a: None)
    monkeypatch.setattr(project.library_packages, "prepend_run_dirs", lambda *a: None)
    return SimpleNamespace(driver=driver, console=console, root=tmp_path)


def test_the_default_build_type_is_debug():
    """A plain build, test, run and package use Debug, and a benchmark uses Release."""
    assert project.K_DEFAULT_TYPE is BuildType.debug
    assert project.K_BENCH_TYPE is BuildType.release


@pytest.mark.parametrize("build_type, folder", [
    (BuildType.debug, "default-debug"),
    (BuildType.release, "default-release"),
    (BuildType.relwithdebinfo, "default-relwithdebinfo"),
    (BuildType.asan, "asan-release"),
    (BuildType.tsan, "tsan-relwithdebinfo"),
])
def test_each_build_type_has_its_own_tree(build_type, folder, tmp_path):
    """The tree of a build type is build/<name>-<type>, and ASan keeps its own name."""
    assert project.build_tree(tmp_path, build_type) == tmp_path / "build" / folder


def test_an_explicit_build_dir_overrides_the_tree_of_the_type(tmp_path):
    """--build-dir names the tree whatever the build type."""
    assert project.build_tree(tmp_path, BuildType.debug, "build/pkg-x") == tmp_path / "build" / "pkg-x"


@pytest.mark.parametrize("command, expected", [
    (["build"], BuildType.debug),
    (["build", "--type", "release"], BuildType.release),
])
def test_the_build_command_defaults_to_debug(command, expected, monkeypatch):
    """`sr build` hands the Debug type to the service unless another is named."""
    seen: list[BuildType] = []
    monkeypatch.setattr(project, "build", lambda build_type, *a, **k: seen.append(build_type) or 0)
    assert CliRunner().invoke(cli.app, command).exit_code == 0
    assert seen == [expected]


def test_the_tree_commands_default_to_debug_and_bench_to_release(monkeypatch):
    """`sr test`, `sr run` and `sr package` find the Debug tree, and `sr bench` the Release one."""
    seen: dict[str, BuildType] = {}
    monkeypatch.setattr(project, "test", lambda *a, **k: seen.update(test=a[3]) or 0)
    monkeypatch.setattr(project, "run", lambda **k: seen.update(run=k["build_type"]) or 0)
    monkeypatch.setattr(project, "bench", lambda *a, **k: seen.update(bench=a[2]) or 0)
    monkeypatch.setattr(cli.packaging_svc, "package",
                        lambda **k: seen.update(package=k["build_type"]) or 0)
    for command in (["test"], ["run"], ["bench"], ["package"]):
        assert CliRunner().invoke(cli.app, command).exit_code == 0
    assert seen == {"test": BuildType.debug, "run": BuildType.debug,
                    "bench": BuildType.release, "package": BuildType.debug}


def test_asan_selects_the_sanitizer_tree_in_the_tree_commands(monkeypatch):
    """`--asan` and `--type asan` name the same tree."""
    seen: list[BuildType] = []
    monkeypatch.setattr(project, "test", lambda *a, **k: seen.append(a[3]) or 0)
    for flag in (["--asan"], ["--type", "asan"]):
        assert CliRunner().invoke(cli.app, ["test", *flag]).exit_code == 0
    assert seen == [BuildType.asan, BuildType.asan]


def test_a_build_configures_and_compiles_in_the_tree_of_its_type(build):
    """The configure and the compile both name build/default-release for a Release build."""
    assert project.build(BuildType.release) == 0
    assert build.driver.compiles[0][0] == build.root / "build" / "default-release"
    assert str(build.root / "build" / "default-release") in build.driver.configures[0]


def test_the_packages_get_the_compiler_the_build_path_finds(build, monkeypatch, tmp_path):
    """A compiler only setvars put on the build PATH reaches sushicore as its full path."""
    bin_dir = tmp_path / "oneapi" / "bin"
    bin_dir.mkdir(parents=True)
    compiler = bin_dir / ("clang++.exe" if os.name == "nt" else "clang++")
    compiler.write_text("", encoding="utf-8")
    compiler.chmod(0o755)
    monkeypatch.setattr(project, "load_build_env", lambda cfg, tree: {"PATH": str(bin_dir)})
    seen = []
    packages = SimpleNamespace(definitions=lambda: [], run_dirs=lambda platform: [])
    monkeypatch.setattr(project.library_packages, "resolve",
                        lambda cfg, root, build_type, cxx, *a: seen.append(cxx) or packages)
    assert project.build(BuildType.release) == 0
    assert Path(seen[0]).parent == bin_dir


def test_a_tree_of_another_build_type_is_refused(build):
    """Configuring Release into a tree cached as Debug stops before cmake runs."""
    tree = build.root / "build" / "x"
    tree.mkdir(parents=True)
    (tree / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Debug\n", encoding="utf-8")
    assert project.build(BuildType.release, build_dir="build/x") == 1
    assert build.driver.configures == [] and build.driver.compiles == []
    assert len(build.console.errors) == 1 and "Debug tree" in build.console.errors[0]


def test_a_stopped_gate_refuses_the_build(build):
    """While the gate's stop file exists, no configure and no compile starts."""
    stop = build.root / "build" / "build_gate" / "stop"
    stop.parent.mkdir(parents=True)
    stop.write_text("", encoding="utf-8")
    assert project.build(BuildType.debug) == 1
    assert build.driver.configures == [] and build.driver.compiles == []
    assert "memory gate" in build.console.errors[0]


def test_the_configure_carries_both_launcher_entries(build):
    """The compile and the link launcher reach the cache through the gate script."""
    assert project.build(BuildType.debug) == 0
    arguments = build.driver.configures[0]
    compile_entries = [a for a in arguments if a.startswith("-DCMAKE_CXX_COMPILER_LAUNCHER=")]
    link_entries = [a for a in arguments if a.startswith("-DCMAKE_CXX_LINKER_LAUNCHER=")]
    assert len(compile_entries) == 1 and len(link_entries) == 1
    assert "build_gate.py" in compile_entries[0]
    assert compile_entries[0].split("=", 1)[1] == link_entries[0].split("=", 1)[1]


@pytest.mark.parametrize("build_type", [BuildType.debug, BuildType.relwithdebinfo,
                                         BuildType.release, BuildType.asan])
def test_no_build_type_but_tsan_asks_for_thread_sanitizer(build, build_type):
    """ThreadSanitizer is never implied by Debug or RelWithDebInfo."""
    assert project.build(build_type) == 0
    assert project.K_THREAD_SANITIZER_OPTION not in build.driver.configures[0]


def test_the_tsan_type_asks_for_thread_sanitizer_in_its_own_tree(build):
    """`--type tsan` configures RelWithDebInfo with the option, in build/tsan-relwithdebinfo."""
    assert project.build(BuildType.tsan) == 0
    arguments = build.driver.configures[0]
    assert project.K_THREAD_SANITIZER_OPTION in arguments
    assert "-DCMAKE_BUILD_TYPE=RelWithDebInfo" in arguments
    assert build.driver.compiles[0][0] == build.root / "build" / "tsan-relwithdebinfo"


def test_the_compile_gets_the_hardware_threads_without_jobs(build, monkeypatch):
    """No --jobs means the compile is told the hardware thread count."""
    monkeypatch.setattr(project.build_admission, "hardware_threads", lambda: 7)
    assert project.build(BuildType.debug) == 0
    assert build.driver.compiles[0][1]["jobs"] == 7


def test_the_report_is_printed_and_written_after_the_compile(build, monkeypatch):
    """--report reads the log from the offset taken before the compile and writes the report."""
    calls: list[tuple] = []
    report = SimpleNamespace(lines=lambda: ["wall time 1 s"],
                             write=lambda tree: calls.append(("write", tree)) or tree / "r.json")
    monkeypatch.setattr(project.build_report, "ninja_log_offset", lambda tree: 42)
    monkeypatch.setattr(project.build_report, "of_build",
                        lambda tree, offset, state: calls.append(("of_build", offset)) or report)
    assert project.build(BuildType.debug, report=True) == 0
    tree = build.root / "build" / "default-debug"
    assert calls == [("of_build", 42), ("write", tree)]


def test_no_report_is_read_without_the_flag(build, monkeypatch):
    """Without --report the log is not summarised."""
    monkeypatch.setattr(project.build_report, "of_build",
                        lambda *a: pytest.fail("the report was read"))
    assert project.build(BuildType.debug) == 0
