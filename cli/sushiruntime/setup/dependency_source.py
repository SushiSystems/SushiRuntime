"""Dependency manifest reading.

The installer must not hard-code package names. Instead it asks an
``IDependencySource`` for the packages relevant to the current platform. The
production implementation reads ``cli/dependencies.toml`` (Section 3a of the
plan); tests can inject an in-memory source.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass
from pathlib import Path

try:
    import tomllib  # Python 3.11+
except ModuleNotFoundError:  # Python 3.10 fallback
    import tomli as tomllib

from ..config import config_dir

MANIFEST_NAME = "dependencies.toml"


@dataclass(frozen=True)
class Dependency:
    """One entry from the manifest, normalized."""

    name: str
    description: str
    required: bool
    gpu_only: bool
    linux_apt: list[str]
    windows_vcpkg: list[str]
    check_cmd: list[str]

    def packages_for(self, platform: str) -> list[str]:
        """Package names for the given platform ('windows' | other = linux)."""
        return self.windows_vcpkg if platform == "windows" else self.linux_apt


class IDependencySource(ABC):
    """Source of the dependency list. Abstraction the steps depend on."""

    @abstractmethod
    def all(self) -> list[Dependency]:
        """Every declared dependency, regardless of platform."""
        raise NotImplementedError

    def selected(self, platform: str, gpu: bool) -> list[Dependency]:
        """Dependencies relevant to this platform/GPU choice with packages.

        Filters out gpu-only entries when ``gpu`` is False and entries that
        declare no package for this platform.
        """
        out: list[Dependency] = []
        for dep in self.all():
            if dep.gpu_only and not gpu:
                continue
            if dep.packages_for(platform):
                out.append(dep)
        return out


def manifest_path() -> Path:
    """Locate ``dependencies.toml`` next to the CLI config."""
    return config_dir() / MANIFEST_NAME


class TomlDependencySource(IDependencySource):
    """Reads dependencies from a TOML manifest file."""

    def __init__(self, path: Path | None = None) -> None:
        self._path = path or manifest_path()

    def all(self) -> list[Dependency]:
        if not self._path.is_file():
            raise FileNotFoundError(
                f"Dependency manifest not found: {self._path}. "
                "Expected cli/dependencies.toml in the repo."
            )
        with self._path.open("rb") as fh:
            doc = tomllib.load(fh)

        deps: list[Dependency] = []
        for name, table in doc.items():
            if not isinstance(table, dict):
                continue
            deps.append(
                Dependency(
                    name=name,
                    description=str(table.get("description", "")),
                    required=bool(table.get("required", True)),
                    gpu_only=bool(table.get("gpu_only", False)),
                    linux_apt=list(table.get("linux_apt", [])),
                    windows_vcpkg=list(table.get("windows_vcpkg", [])),
                    check_cmd=list(table.get("check_cmd", [])),
                )
            )
        return deps
