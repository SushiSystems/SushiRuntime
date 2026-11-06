# test_library_packages.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests that `sr` consumes its libraries as packages and honours the package build contract."""

from __future__ import annotations

import os
from pathlib import Path

import pytest
from sushicore.provision.sushi_packages import configure_arguments
from typer.testing import CliRunner

from sushiruntime import cli, config
from sushiruntime.config import Config
from sushiruntime.services import library_packages, project
from sushiruntime.services.project import BuildType

pytestmark = pytest.mark.medium

K_LOG_DIR = "/store/sushilog/abc/prefix/lib/cmake/SushiLog"
K_LOG_LIB = "/store/sushilog/abc/prefix/lib"


def _compiler_name(asked: dict) -> str:
    """Returns the file name of the compiler a package request carried, without its extension."""
    return Path(asked["compiler"]).stem


def test_build_asks_for_the_closure_with_its_own_compiler(rig):
    """The configure provisions the packages of the closure with the compiler it builds with."""
    built = rig()
    assert project.build(BuildType.debug, surroundings=built.surroundings) == 0
    asked = built.packages.asked[0]
    assert (asked["root"], asked["build_type"], asked["options"]) == (built.root, "release", {})
    assert _compiler_name(asked) == "clang++"


def test_an_asan_build_links_release_packages(rig):
    """A library CLI takes no asan type, so the sanitizer build links release packages."""
    built = rig()
    assert project.build(BuildType.asan, surroundings=built.surroundings) == 0
    assert built.packages.asked[0]["build_type"] == "release"


def test_a_tsan_build_links_thread_sanitized_log_and_profiler_packages(rig):
    """TSan cannot follow a synchronisation edge through an uninstrumented library."""
    built = rig()
    assert project.build(BuildType.tsan, surroundings=built.surroundings) == 0
    asked = built.packages.asked[0]
    assert asked["build_type"] == "relwithdebinfo"
    assert asked["options"] == {"sushilog": ("--sanitize", "thread"),
                                "sushiprofiler": ("--sanitize", "thread")}


def test_the_definitions_skip_a_packaged_pin(rig):
    """A pinned library that is a package gets no checkout directory definition."""
    built = rig()
    (built.root / "cli").mkdir()
    fragment = "\n".join((
        "[module]",
        'depends_on = ["sushilog"]',
        "[sushisystems.sushilog]",
        'version = "v0.2.2"',
        f'commit = "{"a" * 40}"',
        "",
    ))
    (built.root / "cli" / "sushistack.deps.toml").write_text(fragment, encoding="utf-8")
    packages = built.packages(built.root, "release")
    assert configure_arguments(Config(platform="linux"), built.root, packages) == [
        f"-DSushiLog_DIR={K_LOG_DIR}"]


def test_the_run_dirs_lead_the_search_path_of_the_build(rig):
    """The build puts the package folders first on the path and records them for test and run."""
    built = rig()
    assert project.build(BuildType.release, surroundings=built.surroundings) == 0
    tree = built.root / "build" / "default-release"
    compile_env = built.runner.commands[-1].env
    assert compile_env["LD_LIBRARY_PATH"].split(os.pathsep)[0] == K_LOG_LIB
    assert library_packages.read_run_dirs(tree) == [K_LOG_LIB]


def test_the_recorded_run_dirs_lead_the_search_path_of_a_later_run(rig, monkeypatch):
    """A later run finds the package folders ahead of what the shell already had."""
    built = rig()
    tree = built.root / "build" / "default-release"
    library_packages.record_run_dirs(tree, [K_LOG_LIB])
    monkeypatch.setenv("LD_LIBRARY_PATH", "/old")
    env = project._run_environment(built.surroundings.cfg, tree)
    path = env["LD_LIBRARY_PATH"].split(os.pathsep)
    assert path[0] == K_LOG_LIB and "/old" in path


def test_lean_turns_off_the_tests_the_examples_and_the_benchmarks(rig):
    """--lean configures with no tests, no examples and no benchmarks."""
    built = rig()
    assert project.build(BuildType.release, lean=True, surroundings=built.surroundings) == 0
    arguments = built.argv(0)
    assert "-DSR_BUILD_TESTS=OFF" in arguments
    assert "-DSR_BUILD_EXAMPLES=OFF" in arguments and "-DSR_BUILD_BENCHMARKS=OFF" in arguments


def test_the_build_dir_names_the_tree_of_the_configure_and_the_compile(rig):
    """--build-dir is the tree the configure and the compile name."""
    built = rig()
    assert project.build(BuildType.release, build_dir="build/pkg-x",
                         surroundings=built.surroundings) == 0
    tree = str(built.root / "build" / "pkg-x")
    assert tree in built.argv(0) and tree in built.argv(1)


def test_the_build_command_forwards_build_dir_and_lean(rig):
    """`sr build --build-dir X --lean` configures the tree X with no tests."""
    built = rig()
    result = CliRunner().invoke(cli.app, ["build", "--build-dir", "build/pkg-x", "--lean"],
                                obj=built.surroundings)
    assert result.exit_code == 0, result.output
    assert str(built.root / "build" / "pkg-x") in built.argv(0)
    assert "-DSR_BUILD_TESTS=OFF" in built.argv(0)


def test_the_package_command_takes_the_tree_and_no_archive(rig, tmp_path):
    """`sr package` installs the tree --build-dir names into --prefix and builds no archive."""
    built = rig()
    tree = built.root / "build" / "pkg-x"
    tree.mkdir(parents=True)
    (tree / "CMakeCache.txt").write_text("", encoding="utf-8")
    prefix = tmp_path / "staging"
    result = CliRunner().invoke(
        cli.app, ["package", "--build-dir", "build/pkg-x", "--prefix", str(prefix),
                  "--no-archive", "--force"], obj=built.surroundings)
    assert result.exit_code == 0, result.output
    assert built.captured[0][1:] == ["--install", str(tree), "--prefix", str(prefix)]
    assert built.runner.commands == []


def test_load_config_reads_the_stack_compiler_from_the_environment(monkeypatch):
    """The variable SUSHISTACK_CXX becomes `stack_cxx`."""
    monkeypatch.setenv(config.K_STACK_CXX_ENV, "/env/c++")
    assert config.load_config(lambda cls: cls()).stack_cxx == "/env/c++"
