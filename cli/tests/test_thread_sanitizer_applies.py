# test_thread_sanitizer_applies.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""`sr build`'s TSan message used to claim Windows RelWithDebInfo gets ThreadSanitizer.

cmake/Sanitizers.cmake never enables TSan on Windows -- its guard excludes MSVC and the
Windows Clang (icx-cl) driver alike, so every Windows toolchain this CLI configures is
covered. `thread_sanitizer_applies` is the single place that states the rule the CLI's
build message reads from, and these pin it against that guard directly.
"""

from __future__ import annotations

import pytest

from sushiruntime.services.project import BuildType, thread_sanitizer_applies

pytestmark = pytest.mark.small


def test_windows_relwithdebinfo_never_gets_tsan():
    assert thread_sanitizer_applies(True, BuildType.relwithdebinfo) is False


def test_linux_debug_gets_tsan():
    assert thread_sanitizer_applies(False, BuildType.debug) is True


def test_linux_release_does_not_get_tsan():
    assert thread_sanitizer_applies(False, BuildType.release) is False
