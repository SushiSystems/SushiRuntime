# test_build_trees.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests that Debug is the default tree of every command and that a build runs behind the memory
gate."""

from __future__ import annotations

import os
from pathlib import Path

import pytest
from typer.testing import CliRunner

from sushiruntime import cli
from sushiruntime.services import project
from sushiruntime.services.project import BuildType

pytestmark = pytest.mark.medium


def _words(rig) -> list[str]:
    """Returns every word of every command the rig recorded and captured, in order."""
    recorded = [word for command in rig.runner.commands for word in command.argv]
    return recorded + [word for command in rig.captured for word in command]


def _executable(folder: Path, name: str) -> None:
    """Writes an empty executable called *name* into *folder*."""
    folder.mkdir(parents=True, exist_ok=True)
    program = folder / (name + (".exe" if os.name == "nt" else ""))
    program.write_text("", encoding="utf-8")
    program.chmod(0o755)


def _configured_tree(rig, name: str) -> Path:
    """Makes a configured tree below the rig checkout and returns it."""
    tree = rig.root / "build" / name
    tree.mkdir(parents=True)
    (tree / "CMakeCache.txt").write_text("", encoding="utf-8")
    return tree


@pytest.mark.parametrize("command, folder", [
    (["build"], "default-debug"),
    (["build", "--type", "release"], "default-release"),
])
def test_the_build_command_defaults_to_debug(command, folder, rig):
    """`sr build` configures and compiles the Debug tree unless another type is named."""
    built = rig()
    assert CliRunner().invoke(cli.app, command, obj=built.surroundings).exit_code == 0
    assert str(built.root / "build" / folder) in built.argv(0)
    assert str(built.root / "build" / folder) in built.argv(1)


@pytest.mark.parametrize("command, folder", [
    (["test"], "default-debug"),
    (["run"], "default-debug"),
    (["bench"], "default-release"),
    (["package"], "default-debug"),
])
def test_the_tree_commands_default_to_debug_and_bench_to_release(command, folder, rig):
    """`sr test`, `sr run` and `sr package` find the Debug tree, and `sr bench` the Release one."""
    built = rig()
    tree = _configured_tree(built, folder)
    _executable(tree, "sr_surface_tests")
    _executable(tree / "tests", "sr_api_benchmarks")
    assert CliRunner().invoke(cli.app, command, obj=built.surroundings).exit_code == 0
    assert any(str(tree) in word for word in _words(built))


@pytest.mark.parametrize("flag", [["--asan"], ["--type", "asan"]])
def test_asan_selects_the_sanitizer_tree_in_the_tree_commands(flag, rig):
    """`--asan` and `--type asan` name the same tree."""
    built = rig()
    tree = _configured_tree(built, "asan-release")
    assert CliRunner().invoke(cli.app, ["test", *flag], obj=built.surroundings).exit_code == 0
    assert str(tree) in built.argv(0)


def test_a_build_configures_and_compiles_in_the_tree_of_its_type(rig):
    """The configure and the compile both name build/default-release for a Release build."""
    built = rig()
    assert project.build(BuildType.release, surroundings=built.surroundings) == 0
    tree = str(built.root / "build" / "default-release")
    assert tree in built.argv(0)
    assert built.argv(1)[1:3] == ["--build", tree]


def test_the_packages_get_the_compiler_the_build_path_finds(rig):
    """A compiler found on the build PATH reaches the package resolver as its full path."""
    built = rig()
    assert project.build(BuildType.release, surroundings=built.surroundings) == 0
    assert Path(built.packages.asked[0]["compiler"]).parent == Path(
        os.environ["PATH"].split(os.pathsep)[0])


def test_a_tree_of_another_build_type_is_refused(rig):
    """Configuring Release into a tree cached as Debug stops before cmake runs."""
    built = rig()
    tree = built.root / "build" / "x"
    tree.mkdir(parents=True)
    (tree / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Debug\n", encoding="utf-8")
    assert project.build(BuildType.release, build_dir="build/x",
                         surroundings=built.surroundings) == 1
    assert built.runner.commands == []
    assert len(built.console.texts("error")) == 1
    assert "Debug tree" in built.console.texts("error")[0]


def test_a_stopped_gate_refuses_the_build(rig):
    """While the gate stop file exists, no configure and no compile starts."""
    built = rig()
    stop = built.root / "build" / "build_gate" / "stop"
    stop.parent.mkdir(parents=True)
    stop.write_text("", encoding="utf-8")
    assert project.build(BuildType.debug, surroundings=built.surroundings) == 1
    assert built.runner.commands == []
    assert "memory gate" in built.console.texts("error")[0]


def test_the_configure_carries_both_launcher_entries(rig):
    """The compile and the link launcher reach the cache through the gate script."""
    built = rig()
    assert project.build(BuildType.debug, surroundings=built.surroundings) == 0
    arguments = built.argv(0)
    compile_entries = [a for a in arguments if a.startswith("-DCMAKE_CXX_COMPILER_LAUNCHER=")]
    link_entries = [a for a in arguments if a.startswith("-DCMAKE_CXX_LINKER_LAUNCHER=")]
    assert len(compile_entries) == 1 and len(link_entries) == 1
    assert "build_gate.py" in compile_entries[0]
    assert compile_entries[0].split("=", 1)[1] == link_entries[0].split("=", 1)[1]


def test_the_tsan_type_asks_for_thread_sanitizer_in_its_own_tree(rig):
    """`--type tsan` configures RelWithDebInfo with the option, in build/tsan-relwithdebinfo."""
    built = rig()
    assert project.build(BuildType.tsan, surroundings=built.surroundings) == 0
    arguments = built.argv(0)
    assert project.K_THREAD_SANITIZER_OPTION in arguments
    assert "-DCMAKE_BUILD_TYPE=RelWithDebInfo" in arguments
    assert str(built.root / "build" / "tsan-relwithdebinfo") in built.argv(1)


def test_the_report_is_written_after_the_compile(rig):
    """--report writes build_report.json into the tree and says where."""
    built = rig()
    assert project.build(BuildType.debug, report=True, surroundings=built.surroundings) == 0
    written = built.root / "build" / "default-debug" / "build_report.json"
    assert written.is_file()
    assert f"Report: {written}" in built.console.texts("info")


def test_no_report_is_written_without_the_flag(rig):
    """Without --report the log is not summarised and no report file exists."""
    built = rig()
    assert project.build(BuildType.debug, surroundings=built.surroundings) == 0
    assert not (built.root / "build" / "default-debug" / "build_report.json").exists()
