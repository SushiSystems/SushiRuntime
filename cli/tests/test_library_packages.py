# test_library_packages.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests that `sr` consumes its libraries as packages and honours the package build contract."""

from __future__ import annotations

import os
from types import SimpleNamespace

import pytest
from typer.testing import CliRunner

from sushiruntime import cli, config
from sushiruntime.config import Config
from sushiruntime.services import library_packages, packaging, project
from sushiruntime.services.project import BuildType

pytestmark = pytest.mark.medium

K_LOG_DIR = "/store/sushilog/abc/prefix/lib/cmake/SushiLog"
K_LOG_LIB = "/store/sushilog/abc/prefix/lib"


class FakePackages:
    """Stands in for a resolved package set of one library, SushiLog."""

    def names(self) -> list[str]:
        """Returns the library names."""
        return ["sushilog"]

    def definitions(self) -> list[str]:
        """Returns the one package definition."""
        return [f"-DSushiLog_DIR={K_LOG_DIR}"]

    def run_dirs(self, platform: str) -> list[str]:
        """Returns the one run-time folder."""
        return [K_LOG_LIB]


class FakeDriver:
    """Records the configure and compile calls over a tree that has no stamp."""

    def __init__(self) -> None:
        """Starts with no call recorded."""
        self.configures: list[list[str]] = []
        self.compiles: list[tuple] = []

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
def consumer(monkeypatch, tmp_path):
    """Returns the fake driver and the calls to `sushi_packages`, with the build service stubbed."""
    driver, asked = FakeDriver(), []
    cfg = Config(platform="linux", generator="Ninja", cxx="clang++")

    def sushi_packages(root, build_type, options=None, report=print, warn=print, compiler=""):
        asked.append({"root": root, "build_type": build_type, "options": dict(options or {}),
                      "compiler": compiler})
        return FakePackages()

    monkeypatch.setattr(cfg, "sushi_packages", sushi_packages)
    quiet = SimpleNamespace(**{name: lambda *a, **k: None
                               for name in ("header", "info", "success", "error", "warn")})
    monkeypatch.setattr(project, "_DRIVER", driver)
    monkeypatch.setattr(project, "console", quiet)
    monkeypatch.setattr(project, "find_project_root", lambda: tmp_path)
    monkeypatch.setattr(project, "load_config", lambda: cfg)
    monkeypatch.setattr(project, "load_build_env", lambda cfg, tree: {})
    monkeypatch.setattr(project, "_resolve_compiler", lambda *a, **k: ("clang", "clang++"))
    return SimpleNamespace(driver=driver, asked=asked, root=tmp_path, cfg=cfg)


def test_build_asks_for_the_closure_with_its_own_compiler(consumer):
    """The configure resolves the packages of the closure with the compiler it builds with."""
    assert project.build(BuildType.debug) == 0
    assert consumer.asked == [{"root": consumer.root, "build_type": "debug", "options": {},
                               "compiler": "clang++"}]


def test_an_asan_build_links_release_packages(consumer):
    """A library CLI takes no asan type, so the sanitizer build links release packages."""
    assert project.build(BuildType.asan) == 0
    assert consumer.asked[0]["build_type"] == "release"


def test_the_configure_passes_the_package_and_no_checkout_directory(consumer):
    """Each package reaches CMake as -D<Package>_DIR and no -DSUSHILOG_DIR is passed."""
    assert project.build(BuildType.release) == 0
    arguments = consumer.driver.configures[0]
    assert f"-DSushiLog_DIR={K_LOG_DIR}" in arguments
    assert not [item for item in arguments if item.startswith("-DSUSHILOG_DIR")]


def test_the_definitions_skip_a_packaged_pin(tmp_path, monkeypatch):
    """A pinned library that is a package gets no checkout directory definition."""
    (tmp_path / "cli").mkdir()
    fragment = "\n".join((
        "[module]",
        'depends_on = ["sushilog"]',
        "[sushisystems.sushilog]",
        'version = "v0.2.2"',
        f'commit = "{"a" * 40}"',
        "",
    ))
    (tmp_path / "cli" / "sushistack.deps.toml").write_text(fragment, encoding="utf-8")
    cfg = Config(platform="linux")
    monkeypatch.setattr(cfg, "resolve_sushi", lambda root, name: SimpleNamespace(path=tmp_path))
    assert library_packages.configure_arguments(cfg, tmp_path, FakePackages()) == [
        f"-DSushiLog_DIR={K_LOG_DIR}"]


def test_the_run_dirs_lead_the_search_path_of_the_build_and_of_a_later_run(consumer, monkeypatch):
    """The build puts the package folders first on the path and records them for test and run."""
    assert project.build(BuildType.release) == 0
    tree, options = consumer.driver.compiles[0]
    assert options["env"]["LD_LIBRARY_PATH"].split(os.pathsep)[0] == K_LOG_LIB
    assert library_packages.read_run_dirs(tree) == [K_LOG_LIB]
    monkeypatch.setattr(project, "load_build_env", lambda cfg, tree: {"LD_LIBRARY_PATH": "/old"})
    env = project._run_environment(consumer.cfg, tree)
    assert env["LD_LIBRARY_PATH"].split(os.pathsep) == [K_LOG_LIB, "/old"]


def test_the_windows_run_dirs_lead_path():
    """On Windows the folders go on PATH."""
    cfg = Config(platform="windows")
    env = {"PATH": "C:/old"}
    library_packages.prepend_run_dirs(cfg, env, ["C:/pkg/bin"])
    assert env["PATH"].startswith("C:/pkg/bin")


def test_lean_and_the_build_dir_reach_the_tree_and_the_configure(consumer):
    """--lean turns off the tests, the examples and the benchmarks; the tree is the one named."""
    assert project.build(BuildType.release, lean=True, build_dir="build/pkg-x") == 0
    arguments = consumer.driver.configures[0]
    assert "-DSR_BUILD_TESTS=OFF" in arguments
    assert "-DSR_BUILD_EXAMPLES=OFF" in arguments and "-DSR_BUILD_BENCHMARKS=OFF" in arguments
    assert consumer.driver.compiles[0][0] == consumer.root / "build" / "pkg-x"


def test_the_build_command_forwards_build_dir_and_lean(monkeypatch):
    """`sr build --build-dir X --lean` hands both to the build service."""
    seen: list[dict] = []
    monkeypatch.setattr(cli.project_svc, "build", lambda *a, **k: seen.append(k) or 0)
    result = CliRunner().invoke(cli.app, ["build", "--build-dir", "build/pkg-x", "--lean"])
    assert result.exit_code == 0
    assert seen[0]["build_dir"] == "build/pkg-x" and seen[0]["lean"] is True


def test_the_package_command_takes_the_tree_and_no_archive(monkeypatch):
    """`sr package` is registered and forwards --build-dir, --prefix, --no-archive and --force."""
    seen: list[dict] = []
    monkeypatch.setattr(packaging, "package", lambda **k: seen.append(k) or 0)
    result = CliRunner().invoke(cli.app, ["package", "--build-dir", "build/pkg-x", "--prefix",
                                          "out", "--no-archive", "--force"])
    assert result.exit_code == 0
    assert seen == [{"prefix": "out", "archive": False, "force": True, "build_dir": "build/pkg-x"}]


def test_stack_cxx_outranks_the_configured_compiler():
    """The compiler SUSHISTACK_CXX names wins on both platforms' resolution."""
    cfg = Config(cxx="clang++", stack_cxx="/stack/c++", platform="windows")
    assert cfg.resolved_compilers()[1] == "/stack/c++"
    assert cfg.resolved_windows_compiler() == "/stack/c++"


def test_load_config_reads_the_stack_compiler_from_the_environment(monkeypatch):
    """The variable SUSHISTACK_CXX becomes `stack_cxx`."""
    monkeypatch.setattr(config._MODULE, "load", lambda cls: cls())
    monkeypatch.setenv(config.K_STACK_CXX_ENV, "/env/c++")
    assert config.load_config().stack_cxx == "/env/c++"
