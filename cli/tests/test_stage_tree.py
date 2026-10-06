# test_stage_tree.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests for the rule that decides which directory `sr package` may clear and stage into."""

from __future__ import annotations

import os
import shutil
from pathlib import Path

import pytest

from sushiruntime.services.stage_tree import (
    K_STAGE_MARKER,
    StageRules,
    StageTree,
    StageTreeError,
)


@pytest.fixture
def rules(tmp_path) -> StageRules:
    """Returns the rules of a checkout at tmp/work/repo whose owner's home is beside it."""
    root = tmp_path / "work" / "repo"
    (root / "build" / "dist").mkdir(parents=True)
    (tmp_path / "elsewhere_home").mkdir()
    return StageRules(
        project_root=root,
        home=tmp_path / "elsewhere_home",
        build_dir=root / "build",
        default_stage=root / "build" / "dist",
    )


def _filled(path: Path) -> Path:
    """Returns *path* after creating it with one file inside."""
    path.mkdir(parents=True, exist_ok=True)
    (path / "keep.txt").write_text("data", encoding="utf-8")
    return path


@pytest.mark.parametrize("force", [False, True])
@pytest.mark.parametrize(
    ("pick", "reason"),
    [
        (lambda rules: Path(rules.project_root.anchor), "a filesystem root"),
        (lambda rules: rules.home, "the home directory"),
        (lambda rules: rules.project_root, "the project root"),
        (lambda rules: rules.project_root.parent, "an ancestor of the project root"),
        (lambda rules: rules.build_dir, "the build tree"),
    ],
    ids=["filesystem-root", "home", "project-root", "ancestor", "build-tree"],
)
def test_a_protected_directory_is_refused_even_with_force(rules, pick, reason, force):
    """The filesystem root, home, the checkout, its ancestors and the build tree never stage."""
    with pytest.raises(StageTreeError) as caught:
        StageTree(pick(rules)).check(rules, force=force)
    assert reason in str(caught.value)


@pytest.mark.skipif(os.name != "nt", reason="only Windows has a second filesystem root")
def test_the_root_of_another_drive_is_protected(rules):
    """A drive root that is not above the checkout is still a filesystem root."""
    letter = next(c for c in "ZY" if not rules.project_root.anchor.upper().startswith(c))
    assert rules.protection(Path(f"{letter}:/")) == "a filesystem root"


def test_a_foreign_directory_with_content_is_refused_without_force(rules, tmp_path):
    """A non-empty directory the CLI did not stage is refused, and the message names --force."""
    foreign = _filled(tmp_path / "elsewhere")
    with pytest.raises(StageTreeError) as caught:
        StageTree(foreign).check(rules, force=False)
    assert "--force" in str(caught.value)
    assert str(foreign) in str(caught.value)


def test_a_foreign_directory_with_content_is_accepted_with_force(rules, tmp_path):
    """--force lets a non-empty directory outside the protected ones be staged into."""
    StageTree(_filled(tmp_path / "elsewhere")).check(rules, force=True)


def test_a_missing_or_empty_directory_is_accepted(rules, tmp_path):
    """A prefix that does not exist yet, or holds nothing, needs no --force."""
    (tmp_path / "empty").mkdir()
    StageTree(tmp_path / "new").check(rules, force=False)
    StageTree(tmp_path / "empty").check(rules, force=False)


def test_the_default_stage_is_accepted_without_a_marker(rules):
    """build/dist is the CLI's own directory and is cleared without --force."""
    StageTree(_filled(rules.default_stage)).check(rules, force=False)


def test_a_file_is_refused(rules, tmp_path):
    """A prefix that names a file is refused."""
    target = tmp_path / "file.txt"
    target.write_text("data", encoding="utf-8")
    with pytest.raises(StageTreeError):
        StageTree(target).check(rules, force=True)


def test_reset_empties_the_tree_and_writes_the_marker(rules, tmp_path):
    """reset() removes what was staged before and leaves only the marker."""
    stage = StageTree(_filled(tmp_path / "stage"))
    stage.reset()
    assert [entry.name for entry in stage.path.iterdir()] == [K_STAGE_MARKER]


def test_a_tree_the_cli_staged_is_accepted_again_without_force(rules, tmp_path):
    """A directory that carries the marker is the CLI's own and is cleared without --force."""
    stage = StageTree(tmp_path / "stage")
    stage.reset()
    (stage.path / "lib.so").write_text("data", encoding="utf-8")
    stage.check(rules, force=False)


def test_reset_reports_a_delete_that_fails(rules, tmp_path, monkeypatch):
    """A tree that cannot be removed raises instead of being staged over."""
    def refuse(path, ignore_errors=False, **kwargs):
        """Fails the way a locked file fails, and stays silent when told to ignore errors."""
        if not ignore_errors:
            raise PermissionError(13, "locked", str(path))

    stage = StageTree(_filled(tmp_path / "stage"))
    monkeypatch.setattr(shutil, "rmtree", refuse)
    with pytest.raises(StageTreeError) as caught:
        stage.reset()
    assert str(stage.path) in str(caught.value)
    assert "locked" in str(caught.value)
    assert (stage.path / "keep.txt").is_file()
