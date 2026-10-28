# stage_tree.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""The directory `sr package` installs into, and the rule for when it may be cleared.

A stage tree is emptied before every install. :class:`StageTree` refuses the directories
that must never be emptied and marks the ones it stages, so a later run knows them as its own.
"""

from __future__ import annotations

import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import final

from sushicore.errors import SushiCoreError

K_STAGE_MARKER = ".sr-stage"
K_MARKER_TEXT = "Staged by `sr package`. The next run deletes everything in this directory.\n"


class StageTreeError(SushiCoreError):
    """Reports a directory that `sr package` will not clear, or could not."""


@final
@dataclass(frozen=True, slots=True)
class StageRules:
    """Holds the directories that decide whether a stage tree may be cleared.

    Attributes:
        project_root: The checkout; it and every ancestor of it are never cleared.
        home: The user's home directory; never cleared.
        build_dir: The build tree the install reads from; never cleared.
        default_stage: The directory the CLI stages into when no prefix is given.
    """

    project_root: Path
    home: Path
    build_dir: Path
    default_stage: Path

    def protection(self, path: Path) -> str | None:
        """Returns what makes *path* a directory that is never cleared, or None.

        Args:
            path: A resolved path.
        """
        root = self.project_root.resolve()
        if path == Path(path.anchor):
            return "a filesystem root"
        if path == self.home.resolve():
            return "the home directory"
        if path == root:
            return "the project root"
        if root.is_relative_to(path):
            return "an ancestor of the project root"
        built = self.build_dir.resolve()
        if path == built or built.is_relative_to(path):
            return "the build tree being packaged or a folder that holds it"
        return None


@final
@dataclass(frozen=True, slots=True)
class StageTree:
    """Holds one directory an install is staged into."""

    path: Path

    def check(self, rules: StageRules, *, force: bool) -> None:
        """Verifies that this directory may be emptied and staged into.

        Args:
            rules: The directories that are protected, and the CLI's own default stage.
            force: Whether a directory with content the CLI did not stage may be cleared.

        Raises:
            StageTreeError: The directory is protected, is a file, or holds content the
                CLI did not stage while *force* is False.
        """
        path = self.path.resolve()
        reason = rules.protection(path)
        if reason is not None:
            raise StageTreeError(f"Refusing to stage into {path}: it is {reason}.")
        if not path.exists():
            return
        if not path.is_dir():
            raise StageTreeError(f"Refusing to stage into {path}: it is not a directory.")
        if force or path == rules.default_stage.resolve() or self._is_staged(path):
            return
        raise StageTreeError(
            f"Refusing to clear {path}: it holds files and `sr package` did not stage it. "
            "Pass --force to clear it anyway.")

    def reset(self) -> None:
        """Empties this directory, creates it and writes the stage marker.

        Raises:
            StageTreeError: The directory could not be removed, created or marked.
        """
        try:
            if self.path.is_dir():
                shutil.rmtree(self.path)
            self.path.mkdir(parents=True)
            (self.path / K_STAGE_MARKER).write_text(K_MARKER_TEXT, encoding="utf-8")
        except OSError as error:
            raise StageTreeError(f"Could not clear and stage {self.path}: {error}") from error

    @staticmethod
    def _is_staged(path: Path) -> bool:
        """Returns whether *path* is empty or carries the marker of an earlier stage."""
        return (path / K_STAGE_MARKER).is_file() or not any(path.iterdir())
