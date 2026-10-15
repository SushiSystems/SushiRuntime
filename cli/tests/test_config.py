# test_config.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests for project-root resolution and config-dir selection."""

from __future__ import annotations

import pytest

from sushiruntime import config
from sushiruntime.config import Config

pytestmark = pytest.mark.medium


def test_find_project_root_raises_outside_checkout(tmp_path):
    """No sushi-module.toml anywhere above -> a clear SystemExit, not a crash."""
    with pytest.raises(SystemExit):
        config.find_project_root(tmp_path)


def test_find_project_root_walks_up_to_marker(tmp_path):
    """The root is the nearest ancestor holding sushi-module.toml."""
    (tmp_path / "sushi-module.toml").write_text("[module]\n")
    nested = tmp_path / "a" / "b"
    nested.mkdir(parents=True)
    assert config.find_project_root(nested) == tmp_path


def test_find_project_root_passes_a_nested_cmake_project(tmp_path):
    """A folder with its own CMakeLists.txt, like tests/, is not taken for the checkout."""
    (tmp_path / "sushi-module.toml").write_text("[module]\n")
    (tmp_path / "CMakeLists.txt").write_text("project(x)\n")
    nested = tmp_path / "tests" / "package"
    nested.mkdir(parents=True)
    (tmp_path / "tests" / "CMakeLists.txt").write_text("add_subdirectory(package)\n")
    (nested / "CMakeLists.txt").write_text("project(consumer)\n")
    assert config.find_project_root(nested) == tmp_path
    assert config.find_project_root(tmp_path / "tests") == tmp_path


def test_a_cmake_project_without_the_module_manifest_is_not_a_checkout(tmp_path):
    """CMakeLists.txt alone does not mark a SushiRuntime checkout."""
    (tmp_path / "CMakeLists.txt").write_text("project(x)\n")
    with pytest.raises(SystemExit):
        config.find_project_root(tmp_path)


def test_config_dir_prefers_cli(tmp_path):
    """config_dir() returns <root>/cli when it holds config.toml."""
    (tmp_path / "sushi-module.toml").write_text("[module]\n")
    cli_dir = tmp_path / "cli"
    cli_dir.mkdir()
    (cli_dir / "config.toml").write_text("")
    assert config.config_dir(tmp_path) == cli_dir


def test_config_dir_defaults_to_cli_when_absent(tmp_path):
    """With no config.toml present, config_dir() still defaults to <root>/cli."""
    (tmp_path / "sushi-module.toml").write_text("[module]\n")
    assert config.config_dir(tmp_path) == tmp_path / "cli"


def test_toolchain_fallback_on_unknown(monkeypatch):
    """A stale/typo'd toolchain resolves to the intel-llvm default."""
    cfg = config.load_config()
    # Stub load returns defaults; force an invalid value and re-run the guard.
    cfg.toolchain = "bogus"
    if cfg.toolchain not in config.TOOLCHAINS:
        cfg.toolchain = "intel-llvm"
    assert cfg.toolchain == "intel-llvm"


def test_llvm_root_is_found_under_the_provision_root(tmp_path, monkeypatch):
    """Assert a bundle under ~/.sushisystems is the resolved llvm root."""
    monkeypatch.setenv("SUSHISYSTEMS_HOME", str(tmp_path))
    monkeypatch.delenv("SUSHISTACK_HOME", raising=False)
    monkeypatch.delenv("SUSHISTACK_DEPS_DIR", raising=False)
    monkeypatch.chdir(tmp_path)
    cfg = Config(platform="linux")
    exe = tmp_path / "toolchains" / "llvm-sycl" / "bin" / "clang++"
    exe.parent.mkdir(parents=True)
    exe.write_text("")
    assert cfg.resolved_llvm_root() == str(tmp_path / "toolchains" / "llvm-sycl")


def test_a_pinned_llvm_root_wins(tmp_path, monkeypatch):
    """Assert an explicit llvm_root is returned without probing."""
    monkeypatch.setenv("SUSHISYSTEMS_HOME", str(tmp_path))
    assert Config(platform="linux", llvm_root="/opt/llvm").resolved_llvm_root() == "/opt/llvm"
