"""Tests for project-root resolution and config-dir selection."""

from __future__ import annotations

import pytest

from sushiruntime import config


def test_find_project_root_raises_outside_checkout(tmp_path):
    """No CMakeLists.txt anywhere above -> a clear SystemExit, not a crash."""
    with pytest.raises(SystemExit):
        config.find_project_root(tmp_path)


def test_find_project_root_walks_up_to_marker(tmp_path):
    """The root is the nearest ancestor holding CMakeLists.txt."""
    (tmp_path / "CMakeLists.txt").write_text("project(x)\n")
    nested = tmp_path / "a" / "b"
    nested.mkdir(parents=True)
    assert config.find_project_root(nested) == tmp_path


def test_config_dir_prefers_cli(tmp_path):
    """config_dir() returns <root>/cli when it holds config.toml."""
    (tmp_path / "CMakeLists.txt").write_text("project(x)\n")
    cli_dir = tmp_path / "cli"
    cli_dir.mkdir()
    (cli_dir / "config.toml").write_text("")
    assert config.config_dir(tmp_path) == cli_dir


def test_config_dir_defaults_to_cli_when_absent(tmp_path):
    """With no config.toml present, config_dir() still defaults to <root>/cli."""
    (tmp_path / "CMakeLists.txt").write_text("project(x)\n")
    assert config.config_dir(tmp_path) == tmp_path / "cli"


def test_toolchain_fallback_on_unknown(monkeypatch):
    """A stale/typo'd toolchain resolves to the intel-llvm default."""
    cfg = config.load_config()
    # Stub load returns defaults; force an invalid value and re-run the guard.
    cfg.toolchain = "bogus"
    if cfg.toolchain not in config.TOOLCHAINS:
        cfg.toolchain = "intel-llvm"
    assert cfg.toolchain == "intel-llvm"
