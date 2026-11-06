# check_device_sources.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Checks that exactly the units which instantiate a kernel are listed in DEVICE_SOURCES.

An unlisted kernel unit compiles without -fsycl; a listed unit with no kernel pays the device pass
for nothing. tools/README.md says what counts as a kernel.

Usage: python tools/layering/check_device_sources.py [repository root] [--report] [--rule NAME]
"""

from __future__ import annotations

import os
import re
import sys
from collections.abc import Callable, Iterable, Iterator, Mapping, Sequence
from dataclasses import dataclass
from functools import cached_property
from pathlib import Path
from typing import Protocol

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from checking.checker import Checker, Issue, run  # noqa: E402
from settings import K_MODULE_FUNCTION  # noqa: E402

# Declaring functions, each with the folder its source paths are relative to, below its build file.
K_DECLARING_CALLS = {
    K_MODULE_FUNCTION: "source",
    "sushiruntime_add_module_tests": "",
    "sushiruntime_add_test_program": "",
    "sushiruntime_add_example": "",
}
K_SOURCE_LISTS = ("SOURCES", "BENCHMARK_SOURCES")
K_DEVICE_LIST = "DEVICE_SOURCES"
K_BUILD_FOLDERS = ("modules", "tests", "applications")
# A consumer project built by the packaging test with its own -fsycl; no list of this tree names it.
K_SKIPPED_FOLDERS = ("tests/package",)
K_EXAMPLES = "applications/examples"
K_RECORDING_FOLDERS = ("tests", "applications")
K_INCLUDE_ROOTS_GLOB = "modules/*/*/include"
K_INCLUDE_ROOT_DEPTH = 4
K_BUILD_FILE_NAME = "CMakeLists.txt"
K_SOURCE_DIR_PREFIX = "${CMAKE_CURRENT_SOURCE_DIR}/"
K_PROJECT_PREFIX = "SushiRuntime/"
K_API_INCLUDE = re.compile(r"^SushiRuntime/(?:api/|sushiruntime\.hpp$)")
K_TEMPLATE_ARGUMENTS = r"\s*(?:<[^;(){}]*>)?\s*\("
# A launch the unit writes itself.
K_KERNEL_LAUNCH = re.compile(r"\b(?:parallel_for|parallel_for_work_group|single_task)" + K_TEMPLATE_ARGUMENTS)
# An API call that instantiates a kernel, read in tests and applications that reach the API only.
K_KERNEL_RECORD = re.compile(
    r"\.\s*(?:kernel|captured_kernel|dynamic_kernel|untracked_kernel|state_rule|coupled_rule|resolve"
    r"|add_rule|add_coupled|add_dynamic|add_untracked|add_captured)" + K_TEMPLATE_ARGUMENTS
    + r"|\.\s*add" + K_TEMPLATE_ARGUMENTS + r"(?!\s*(?:\[|std::move\())"
    + r"|\b(?:launch_node|range_node|device_sized_node|dynamic_node|captured_node|untracked_launch_node"
    r"|untracked_range_node|state_launch_node|dynamic_state_node|coupled_launch_node"
    r"|late_count_resolver_node|submit_launch|submit_late_window|submit_live_range)" + K_TEMPLATE_ARGUMENTS
)
K_INCLUDE = re.compile(r'^\s*#\s*include\s*(?:<([^>]+)>|"([^"]+)")')
K_BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
K_LINE_COMMENT = re.compile(r"//[^\n]*")
K_CMAKE_COMMENT = re.compile(r"#[^\n]*")
K_CMAKE_KEYWORD = re.compile(r"[A-Z][A-Z_]*")
K_CMAKE_TOKEN = re.compile(r"[^\s()]+")


@dataclass(frozen=True, slots=True)
class Reach:
    """Holds the first line of a unit that leads to a kernel and what it leads to."""

    line: int
    via: str


@dataclass(frozen=True, slots=True)
class Entry:
    """Holds one source a declaring call names, with its line in the build file."""

    path: Path
    build_file: Path
    line: int


@dataclass(frozen=True, slots=True)
class Declaration:
    """Holds the sources and the device sources of one declaring call."""

    sources: tuple[Entry, ...]
    device_sources: tuple[Entry, ...]


def _blank(text: str, pattern: re.Pattern[str]) -> str:
    """Returns text with each match of a comment pattern replaced by blanks, keeping its newlines."""
    return pattern.sub(lambda match: re.sub(r"[^\n]", " ", match.group(0)), text)


def _lexical(path: Path) -> Path:
    """Returns the path with its . and .. parts folded, without asking the disk."""
    return Path(os.path.normpath(path))


def _source_path(word: str, base: Path) -> Path | None:
    """Returns the source a list token names below base, None for a variable or a non-source token."""
    word = word.strip('"')
    if word.startswith(K_SOURCE_DIR_PREFIX):
        word = word[len(K_SOURCE_DIR_PREFIX):]
    if "$" in word or not word.endswith(".cpp"):
        return None
    return base / word


def parse_declarations_text(
    build_file: Path, text: str, canonical: Callable[[Path], Path]
) -> list[Declaration]:
    """Returns every declaring call of a build file's text, with the sources and device sources it lists.

    The canonical function gives each listed source the form the unit tree compares it by.
    """
    code = _blank(text, K_CMAKE_COMMENT)
    declarations: list[Declaration] = []
    for function, folder in K_DECLARING_CALLS.items():
        base = build_file.parent / folder if folder else build_file.parent
        for call in re.finditer(rf"\b{re.escape(function)}\s*\(([^)]*)\)", code):
            lists: dict[str, list[Entry]] = {name: [] for name in (*K_SOURCE_LISTS, K_DEVICE_LIST)}
            keyword = ""
            for token in K_CMAKE_TOKEN.finditer(call.group(1)):
                word = token.group(0)
                if K_CMAKE_KEYWORD.fullmatch(word):
                    keyword = word
                    continue
                path = _source_path(word, base) if keyword in lists else None
                if path is not None:
                    line = code.count("\n", 0, call.start(1) + token.start()) + 1
                    lists[keyword].append(Entry(canonical(path), build_file, line))
            sources = tuple(entry for name in K_SOURCE_LISTS for entry in lists[name])
            declarations.append(Declaration(sources, tuple(lists[K_DEVICE_LIST])))
    return declarations


def parse_declarations(build_file: Path) -> list[Declaration]:
    """Returns every declaring call of a build file read from disk, with the sources it lists."""
    text = build_file.read_text(encoding="utf-8")
    return parse_declarations_text(build_file, text, DiskFiles().canonical)


class TreeFiles(Protocol):
    """Names the reads a unit tree makes, so the tree can come from the disk or from text."""

    def canonical(self, path: Path) -> Path:
        """Returns the form in which two spellings of one file compare equal."""

    def text(self, path: Path) -> str | None:
        """Returns the text of a file, None when the file is absent or unreadable."""

    def build_files(self, folders: Sequence[Path]) -> list[Path]:
        """Returns each build file below the folders, folder by folder, sorted within each folder."""

    def include_roots(self, root: Path) -> list[Path]:
        """Returns every modules/<tier>/<module>/include folder below the root."""


class DiskFiles:
    """Reads a repository's files from disk; the files a unit tree reads when none is given."""

    def canonical(self, path: Path) -> Path:
        """Returns the path with its links followed."""
        return path.resolve()

    def text(self, path: Path) -> str | None:
        """Returns the UTF-8 text of a file, with bytes that do not decode replaced, None if unreadable."""
        try:
            return path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            return None

    def build_files(self, folders: Sequence[Path]) -> list[Path]:
        """Returns each build file below the folders, folder by folder, sorted within each folder."""
        return [path for folder in folders for path in sorted(folder.rglob(K_BUILD_FILE_NAME))]

    def include_roots(self, root: Path) -> list[Path]:
        """Returns every modules/<tier>/<module>/include folder below the root that is a folder."""
        return sorted(path for path in root.glob(K_INCLUDE_ROOTS_GLOB) if path.is_dir())


class TextFiles:
    """Holds a repository's files as text under their paths, so a unit tree reads no disk."""

    def __init__(self, texts: Mapping[Path, str]) -> None:
        """Keeps each text under its path, folded the way canonical folds a path."""
        self._texts = {_lexical(path): text for path, text in texts.items()}

    def canonical(self, path: Path) -> Path:
        """Returns the path with its . and .. parts folded."""
        return _lexical(path)

    def text(self, path: Path) -> str | None:
        """Returns the text kept under the path, None when no text is kept there."""
        return self._texts.get(_lexical(path))

    def build_files(self, folders: Sequence[Path]) -> list[Path]:
        """Returns each kept build file below the folders, folder by folder, sorted within each folder."""
        return [
            path
            for folder in map(_lexical, folders)
            for path in sorted(
                kept
                for kept in self._texts
                if kept.name == K_BUILD_FILE_NAME and kept.is_relative_to(folder)
            )
        ]

    def include_roots(self, root: Path) -> list[Path]:
        """Returns every modules/<tier>/<module>/include folder that holds a kept file below the root."""
        base = _lexical(root)
        found: set[Path] = set()
        for kept in self._texts:
            if not kept.is_relative_to(base):
                continue
            parts = kept.relative_to(base).parts
            if len(parts) > K_INCLUDE_ROOT_DEPTH and parts[0] == "modules" and parts[3] == "include":
                found.add(base.joinpath(*parts[:K_INCLUDE_ROOT_DEPTH]))
        return sorted(found)


class UnitTree:
    """Holds one repository's declaring calls and the include graph of its C++ files."""

    def __init__(self, root: Path, files: TreeFiles | None = None) -> None:
        """Remembers the root and the files it reads, the disk when none are given."""
        self.root = root
        self._files: TreeFiles = files if files is not None else DiskFiles()
        self._texts: dict[Path, str | None] = {}
        self._lines: dict[Path, list[str]] = {}
        self._api: dict[Path, bool] = {}

    @cached_property
    def declarations(self) -> list[Declaration]:
        """Returns the declaring calls of every build file below the build folders."""
        skipped = [self.root / folder for folder in K_SKIPPED_FOLDERS]
        folders = [self.root / folder for folder in K_BUILD_FOLDERS]
        found: list[Declaration] = []
        for build_file in self._files.build_files(folders):
            if any(build_file.is_relative_to(path) for path in skipped):
                continue
            text = self._text(build_file)
            if text is not None:
                found += parse_declarations_text(build_file, text, self._files.canonical)
        return found

    @cached_property
    def include_roots(self) -> list[Path]:
        """Returns every modules/<tier>/<module>/include folder."""
        return self._files.include_roots(self.root)

    def has_file(self, path: Path) -> bool:
        """Tells whether a file is present and readable."""
        return self._text(path) is not None

    def lines_of(self, path: Path) -> list[str]:
        """Returns the lines of a C++ file with its comments blanked, empty when it is unreadable."""
        if path not in self._lines:
            text = self._text(path) or ""
            self._lines[path] = _blank(_blank(text, K_BLOCK_COMMENT), K_LINE_COMMENT).splitlines()
        return self._lines[path]

    def _text(self, path: Path) -> str | None:
        """Returns the text of a file, read once; None when the file is absent or unreadable."""
        if path not in self._texts:
            self._texts[path] = self._files.text(path)
        return self._texts[path]

    def _quoted(self, target: str, includer: Path, unit: Path) -> Path | None:
        """Returns the file a quoted include names beside the includer, the unit or the examples."""
        candidates = (includer.parent / target, unit.parent / target, self.root / K_EXAMPLES / target)
        found = next((path for path in candidates if self.has_file(path)), None)
        return None if found is None else self._files.canonical(found)

    def _project(self, target: str) -> Path | None:
        """Returns the module header an angle include of this project names."""
        candidates = (root / target for root in self.include_roots)
        found = next((path for path in candidates if self.has_file(path)), None)
        return None if found is None else self._files.canonical(found)

    def reaches_api(self, path: Path, seen: set[Path] | None = None) -> bool:
        """Tells whether a file includes an API header, directly or through other files."""
        if path in self._api:
            return self._api[path]
        seen = seen if seen is not None else set()
        seen.add(path)
        found = False
        for line in self.lines_of(path):
            match = K_INCLUDE.match(line)
            if match is None:
                continue
            target = match.group(1) or match.group(2)
            if K_API_INCLUDE.match(target):
                found = True
                break
            header = self._project(target) if match.group(1) else self._quoted(target, path, path)
            if header is not None and header not in seen and self.reaches_api(header, seen):
                found = True
                break
        self._api[path] = found
        return found

    def reach(self, unit: Path) -> Reach | None:
        """Returns where a unit first instantiates a kernel, in itself or in a quoted header."""
        root = self._files.canonical(self.root)
        recording = any(unit.is_relative_to(root / folder) for folder in K_RECORDING_FOLDERS)
        return self._walk(unit, unit, recording and self.reaches_api(unit), {unit})

    def _walk(self, path: Path, unit: Path, api: bool, seen: set[Path]) -> Reach | None:
        """Searches one file and the quoted headers below it; `seen` keeps a cycle from looping."""
        for number, line in enumerate(self.lines_of(path), start=1):
            if K_KERNEL_LAUNCH.search(line):
                return Reach(number, "a kernel launch")
            if api and K_KERNEL_RECORD.search(line):
                return Reach(number, "a kernel recorded through the API")
            match = K_INCLUDE.match(line)
            if match is None or match.group(2) is None:
                continue
            header = self._quoted(match.group(2), path, unit)
            if header is None or header in seen:
                continue
            seen.add(header)
            inner = self._walk(header, unit, api, seen)
            if inner is not None:
                return Reach(number, f"{match.group(2)} -> {inner.via}")
        return None


def collect(paths: list[Path]) -> Iterable[UnitTree]:
    """Returns the unit tree of each given repository that has modules."""
    return [UnitTree(path) for path in paths if (path / "modules").is_dir()]


def rule_device_source_missing(tree: UnitTree) -> Iterator[Issue]:
    """Yields an issue for each listed source that instantiates a kernel and is not in DEVICE_SOURCES."""
    for declaration in tree.declarations:
        listed = {entry.path for entry in declaration.device_sources}
        for entry in declaration.sources:
            if entry.path in listed or not tree.has_file(entry.path):
                continue
            reach = tree.reach(entry.path)
            if reach is not None:
                yield Issue(entry.path, reach.line, f"{entry.path.name} instantiates {reach.via} and is "
                                                    f"not in DEVICE_SOURCES of {entry.build_file.parent.name}")


def rule_device_source_unneeded(tree: UnitTree) -> Iterator[Issue]:
    """Yields an issue for each DEVICE_SOURCES entry that instantiates no kernel."""
    for declaration in tree.declarations:
        for entry in declaration.device_sources:
            if tree.has_file(entry.path) and tree.reach(entry.path) is None:
                yield Issue(entry.build_file, entry.line, f"{entry.path.name} is in DEVICE_SOURCES and "
                                                          "instantiates no kernel; list it in SOURCES only")


K_CHECKER = Checker(
    description="Checks that the units instantiating a kernel are exactly the DEVICE_SOURCES.",
    collect=collect,
    rules={
        "rule_device_source_missing": rule_device_source_missing,
        "rule_device_source_unneeded": rule_device_source_unneeded,
    },
)

if __name__ == "__main__":
    sys.exit(run(K_CHECKER))
