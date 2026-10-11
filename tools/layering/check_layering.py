# check_layering.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Checks that modules under modules/<tier>/<module>/ depend only down the tier order.

Usage: python tools/layering/check_layering.py [repository root] [--report] [--rule NAME]
"""

from __future__ import annotations

import re
import sys
from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common.checker import Checker, Issue, run  # noqa: E402

K_TIER_ORDER: tuple[str, ...] = ("foundation", "platform", "engine", "surface", "extension")
K_SAME_TIER_EDGES: frozenset[tuple[str, str]] = frozenset({
    ("report", "core"),
    ("log", "core"),
    ("logging", "core"),
    ("logging", "log"),
    ("scheduler", "task"),
    ("graph", "task"),
    ("execution", "task"),
    ("execution", "scheduler"),
    ("execution", "graph"),
    ("memory_exchange", "interop"),
    ("api", "interop"),
    ("api", "memory_exchange"),
})
K_SOURCE_SUFFIXES = frozenset({".h", ".hpp", ".cpp", ".glsl", ".comp", ".vert", ".frag"})
K_ANGLE_INCLUDE = re.compile(r'^\s*#\s*include\s*<[^/>]+/([^/>]+)/[^>]+>')
K_QUOTED_INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')
K_CMAKE_SOURCE_REACH = re.compile(r"(?:^|[/\s\"'])([A-Za-z0-9_]+)/source\b")
K_CMAKE_COMMENT = re.compile(r"#.*")
K_CMAKE_MODULE_CALL = re.compile(r"\bsushiruntime_add_module\s*\(([^)]*)\)")
K_CMAKE_KEYWORD = re.compile(r"[A-Z][A-Z_]*")
K_PUBLIC_DEPENDS = "DEPENDS"
K_PRIVATE_DEPENDS = "PRIVATE_DEPENDS"
K_INTERNAL_MARK = "INTERNAL"
K_PUBLIC_FOLDER = "include"
K_BUILT_FOLDERS = frozenset({"include", "source"})


@dataclass(frozen=True, slots=True)
class Module:
    """Holds one module folder and its tier."""

    tier: str
    name: str
    folder: Path


@dataclass(frozen=True, slots=True)
class ModuleTree:
    """Holds every module of one repository and the declared tier order."""

    root: Path
    modules: dict[str, Module]
    tier_order: tuple[str, ...]

    def rank(self, tier: str) -> int | None:
        """Returns a tier's position in the order, lowest first, or None when undeclared."""
        return self.tier_order.index(tier) if tier in self.tier_order else None


def _discover(root: Path) -> dict[str, Module]:
    """Returns every module below root/modules, keyed by module name."""
    modules: dict[str, Module] = {}
    base = root / "modules"
    for tier in sorted(entry for entry in base.iterdir() if entry.is_dir()):
        for folder in sorted(entry for entry in tier.iterdir() if entry.is_dir()):
            modules[folder.name] = Module(tier.name, folder.name, folder)
    return modules


def collect(paths: list[Path]) -> Iterable[ModuleTree]:
    """Returns the module tree of each given repository that has one."""
    return [
        ModuleTree(path, _discover(path), K_TIER_ORDER)
        for path in paths if (path / "modules").is_dir()
    ]


def _source_lines(module: Module) -> Iterator[tuple[Path, int, str]]:
    """Yields every line of every C-family source file in a module."""
    for path in sorted(module.folder.rglob("*")):
        if path.is_file() and path.suffix in K_SOURCE_SUFFIXES:
            text = path.read_text(encoding="utf-8", errors="replace")
            for number, line in enumerate(text.splitlines(), start=1):
                yield path, number, line


def _module_includes(tree: ModuleTree, module: Module) -> Iterator[tuple[Path, int, Module]]:
    """Yields each include, in a module's include/ or source/, of another module of the tree."""
    for path, number, line in _source_lines(module):
        if path.relative_to(module.folder).parts[0] not in K_BUILT_FOLDERS:
            continue
        match = K_ANGLE_INCLUDE.match(line)
        target = tree.modules.get(match.group(1)) if match else None
        if target is not None and target.name != module.name:
            yield path, number, target


def _module_call_tokens(module: Module) -> list[str]:
    """Returns the arguments of the sushiruntime_add_module() call in a module's build file."""
    build_file = module.folder / "CMakeLists.txt"
    text = build_file.read_text(encoding="utf-8") if build_file.is_file() else ""
    call = K_CMAKE_MODULE_CALL.search(K_CMAKE_COMMENT.sub("", text))
    return call.group(1).split() if call else []


def _declared_dependencies(module: Module) -> dict[str, frozenset[str]]:
    """Returns the modules a module's build file names, keyed by DEPENDS and PRIVATE_DEPENDS."""
    declared: dict[str, set[str]] = {K_PUBLIC_DEPENDS: set(), K_PRIVATE_DEPENDS: set()}
    keyword = ""
    for token in _module_call_tokens(module):
        if K_CMAKE_KEYWORD.fullmatch(token):
            keyword = token
        elif keyword in declared:
            declared[keyword].add(token)
    return {keyword: frozenset(names) for keyword, names in declared.items()}


def _is_internal(module: Module) -> bool:
    """Returns whether a module's build file marks it INTERNAL, so its headers are not installed."""
    return K_INTERNAL_MARK in _module_call_tokens(module)


def rule_declared_tiers(tree: ModuleTree) -> Iterator[Issue]:
    """Yields an issue for each tier folder missing from the declared order."""
    if not tree.tier_order:
        yield Issue(Path(__file__), 1, "K_TIER_ORDER is empty; declare the repository's tiers")
        return
    for tier in sorted({module.tier for module in tree.modules.values()}):
        if tree.rank(tier) is None:
            yield Issue(tree.root / "modules" / tier, 1, f"tier '{tier}' is not in K_TIER_ORDER")


def rule_upward_includes(tree: ModuleTree) -> Iterator[Issue]:
    """Yields an issue for each include of a module that sits in a higher tier."""
    for module in tree.modules.values():
        own = tree.rank(module.tier)
        for path, number, line in _source_lines(module):
            match = K_ANGLE_INCLUDE.match(line)
            target = tree.modules.get(match.group(1)) if match else None
            if own is None or target is None:
                continue
            other = tree.rank(target.tier)
            if other is not None and other > own:
                yield Issue(path, number, f"{module.tier}/{module.name} includes up into {target.tier}/{target.name}")


def rule_private_reach(tree: ModuleTree) -> Iterator[Issue]:
    """Yields an issue for each include or build line that reaches another module's source/."""
    for module in tree.modules.values():
        for path, number, line in _source_lines(module):
            match = K_QUOTED_INCLUDE.match(line)
            if match and ("../" in match.group(1) or "source/" in match.group(1)):
                yield Issue(path, number, "quoted include leaves the module or names a source/ folder")
        build_file = module.folder / "CMakeLists.txt"
        if not build_file.is_file():
            continue
        for number, line in enumerate(build_file.read_text(encoding="utf-8").splitlines(), start=1):
            for name in K_CMAKE_SOURCE_REACH.findall(line):
                if name != module.name and name in tree.modules:
                    yield Issue(build_file, number, f"reaches into {name}/source")


def rule_same_tier_includes(tree: ModuleTree) -> Iterator[Issue]:
    """Yields an issue for each include of a same-tier module whose edge is not listed."""
    for module in tree.modules.values():
        for path, number, target in _module_includes(tree, module):
            if target.tier == module.tier and (module.name, target.name) not in K_SAME_TIER_EDGES:
                yield Issue(path, number, f"{module.tier}/{module.name} includes {target.name}, "
                                          "an edge K_SAME_TIER_EDGES does not list")


def rule_declared_dependencies(tree: ModuleTree) -> Iterator[Issue]:
    """Yields an issue for each include of a module the including module's build file omits."""
    for module in tree.modules.values():
        declared = _declared_dependencies(module)
        for path, number, target in _module_includes(tree, module):
            public = path.relative_to(module.folder).parts[0] == K_PUBLIC_FOLDER
            keywords = (K_PUBLIC_DEPENDS,) if public else (K_PUBLIC_DEPENDS, K_PRIVATE_DEPENDS)
            if not any(target.name in declared[keyword] for keyword in keywords):
                yield Issue(path, number, f"{module.name} includes {target.name}, which its "
                                          f"CMakeLists.txt does not name in {' or '.join(keywords)}")


def rule_public_headers(tree: ModuleTree) -> Iterator[Issue]:
    """Yields an issue for each include of an internal module in a header a public module installs."""
    internal = {module.name for module in tree.modules.values() if _is_internal(module)}
    for module in tree.modules.values():
        if module.name in internal:
            continue
        for path, number, target in _module_includes(tree, module):
            installed = path.relative_to(module.folder).parts[0] == K_PUBLIC_FOLDER
            if installed and target.name in internal:
                yield Issue(path, number, f"{module.name} is installed and includes {target.name}, "
                                          "an INTERNAL module whose headers are not")


K_CHECKER = Checker(
    description="Checks module dependencies against the declared tier order.",
    collect=collect,
    rules={
        "rule_declared_tiers": rule_declared_tiers,
        "rule_upward_includes": rule_upward_includes,
        "rule_private_reach": rule_private_reach,
        "rule_same_tier_includes": rule_same_tier_includes,
        "rule_declared_dependencies": rule_declared_dependencies,
        "rule_public_headers": rule_public_headers,
    },
)

if __name__ == "__main__":
    sys.exit(run(K_CHECKER))
