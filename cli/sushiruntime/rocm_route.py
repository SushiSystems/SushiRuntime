# rocm_route.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Finds the Windows ROCm route under the SushiSystems home and states it to CMake and to a run.

See cli/README.md.
"""

from __future__ import annotations

import re
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from sushicore.provision import home

#: The environment variable that states the ISA, in place of probing for it.
K_ARCH_ENV = "SR_AMD_ARCH"

#: The variable the Unified Runtime loader reads to load adapters it does not find by itself.
K_FORCE_LOAD = "UR_ADAPTERS_FORCE_LOAD"

_RESOLVED = re.compile(r"^SR_GPU_BACKEND_RESOLVED:[A-Z]+=(.*)$", re.MULTILINE)


def _newest(candidates: Sequence[Path]) -> Path | None:
    """Return the last of *candidates* in path order, which is the newest version, or None."""
    return sorted(candidates, key=str)[-1] if candidates else None


@dataclass(frozen=True)
class RocmRoute:
    """The three pieces of the ROCm route, all of which a build and a run need.

    ``root`` is the unpacked toolkit, ``device_libraries`` the bitcode the compiler can read
    (the toolkit's own is written by a newer LLVM) and ``adapter`` the Unified Runtime HIP
    adapter, which the loader does not discover on Windows.
    """

    root: Path
    device_libraries: Path
    adapter: Path

    @classmethod
    def discover(cls, roots: Sequence[Path] | None = None) -> RocmRoute | None:
        """Return the route the first dependency root holding all three pieces gives, or None.

        Args:
            roots: The roots to look through, in order; the search roots when ``None``.
        """
        for base in (home.search_roots() if roots is None else roots):
            toolkit = _newest([path.parent.parent for path in
                               (base / "rocm").glob("*/bin/hipcc.exe")])
            bitcode = _newest([path.parent for path in (base / "rocm").glob(
                "devlibs-*/opt/rocm-*/lib/llvm/lib/clang/*/lib/amdgcn/bitcode/hip.bc")])
            adapter = _newest(list((base / "ur").glob("hip-*/bin/ur_adapter_hip.dll")))
            if toolkit and bitcode and adapter:
                return cls(toolkit, bitcode, adapter)
        return None

    def configure_arguments(self, environ: Mapping[str, str]) -> list[str]:
        """Return the ``-D`` arguments that hand the route to CMake, with the ISA when stated."""
        arguments = [f"-DSR_ROCM_PATH={self.root.as_posix()}",
                     f"-DSR_ROCM_DEVICE_LIBS={self.device_libraries.as_posix()}"]
        if environ.get(K_ARCH_ENV):
            arguments.append(f"-D{K_ARCH_ENV}={environ[K_ARCH_ENV]}")
        return arguments

    def fingerprint(self, environ: Mapping[str, str]) -> str:
        """Return a key that changes whenever :meth:`configure_arguments` would."""
        return "|".join(self.configure_arguments(environ))

    def force_load_value(self, adapter_directory: Path) -> str:
        """Return the quoted, comma separated adapter list the loader is told to load.

        Args:
            adapter_directory: The toolchain's ``bin``, holding the CUDA and OpenCL adapters.
        """
        adapters = [self.adapter,
                    adapter_directory / "ur_adapter_cuda.dll",
                    adapter_directory / "ur_adapter_opencl.dll"]
        return ",".join(f'"{adapter}"' for adapter in adapters if adapter.is_file())


def tree_offloads_to_rocm(tree: Path) -> bool:
    """Return whether the configured *tree* resolved ROCm as one of its GPU backends."""
    cache = tree / "CMakeCache.txt"
    if not cache.is_file():
        return False
    found = _RESOLVED.search(cache.read_text(encoding="utf-8", errors="replace"))
    return bool(found) and "rocm" in found.group(1).split(";")
