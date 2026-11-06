# surroundings.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Holds what a project command prints through, runs commands with and reads its checkout from.

See cli/README.md.
"""

from __future__ import annotations

from collections.abc import Callable, Mapping
from pathlib import Path
from typing import Any

from sushicore.cmake_driver import CMakeDriver
from sushicore.proc import CommandRunner, Runner
from sushicore.provision.package_install import CapturedRun, capture_command
from sushicore.provision.sushi_packages import PackageSet

from .. import console as live_console
from ..config import PROFILE, Config, find_project_root, load_config

#: Provisions the library packages of a configure; the signature of ``Config.sushi_packages``.
PackageProvisioner = Callable[..., PackageSet]

#: Runs a command with its output held back; the signature of ``capture_command``.
Capture = Callable[[list[str], Path, Mapping[str, str] | None], CapturedRun]


class Surroundings:
    """What one run of a service command reaches outside itself.

    Every argument is optional. An absent one is what the command used before the argument
    existed: the CLI's own console, a runner that spawns processes, the checkout the working
    folder sits in, the layered configuration, the configuration's own package provisioning and
    the real capture.
    The checkout and the configuration are read on first use, as the commands read them.

    Args:
        console: Receives every line the command prints.
        runner: Runs the external commands.
        root: The checkout the command works in.
        cfg: The resolved configuration.
        provision_packages: Provisions the library packages of a configure; the
            configuration's own ``sushi_packages`` when None.
        capture: Runs a command whose output is held back.
    """

    def __init__(self, *, console: Any = None, runner: CommandRunner | None = None,
                 root: Path | None = None, cfg: Config | None = None,
                 provision_packages: PackageProvisioner | None = None,
                 capture: Capture | None = None) -> None:
        """Keep the given parts; build nothing yet."""
        self._console = console if console is not None else live_console
        self._runner = runner if runner is not None else Runner(self._console, PROFILE.program)
        self._driver = CMakeDriver(self._console, self._runner)
        self._root = root
        self._cfg = cfg
        self._provision_packages = provision_packages
        self._capture = capture or capture_command

    @property
    def console(self) -> Any:
        """Return the console the command prints through."""
        return self._console

    @property
    def runner(self) -> CommandRunner:
        """Return the runner of external commands."""
        return self._runner

    @property
    def driver(self) -> CMakeDriver:
        """Return the cmake and ctest driver over this console and runner."""
        return self._driver

    @property
    def root(self) -> Path:
        """Return the checkout, finding it from the working folder on first use."""
        if self._root is None:
            self._root = find_project_root()
        return self._root

    @property
    def cfg(self) -> Config:
        """Return the configuration, loading it on first use."""
        if self._cfg is None:
            self._cfg = load_config()
        return self._cfg

    @property
    def provision_packages(self) -> PackageProvisioner | None:
        """Return the provisioner of library packages, or None for the configuration's own."""
        return self._provision_packages

    @property
    def capture(self) -> Capture:
        """Return the function that runs a command with its output held back."""
        return self._capture
