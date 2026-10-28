# test_help_screen.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""sr's help screen groups its commands under four headings and carries examples."""

from __future__ import annotations

import ast
import re
from pathlib import Path

import pytest
from typer.testing import CliRunner

import sushiruntime
from sushiruntime.cli import app

pytestmark = pytest.mark.medium

K_GROUPS = {
    "Toolchain": ["toolchain"],
    "Project": ["build", "test", "bench", "run", "package", "clean", "docs", "backlog", "design"],
    "Diagnostics": ["config", "env", "setup", "doctor", "link", "unlink"],
    "Container": ["container"],
}
K_CONTAINER_COMMANDS = ["build", "run"]
K_COLOURS = "green|red|yellow|cyan|blue|dim"
K_RAW_MARKUP = re.compile(rf"\[/?(?:bold |italic )?(?:{K_COLOURS})\]")
K_RAW_STYLE = re.compile(rf"(?:bold )?(?:{K_COLOURS})")
K_ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;]*m")


def _run(*args: str) -> str:
    """Return the output of one sr invocation, asserting it exited zero."""
    result = CliRunner().invoke(app, list(args))
    assert result.exit_code == 0, result.output
    return K_ANSI_ESCAPE.sub("", result.output)


def _commands_by_heading(page: str) -> dict[str, list[str]]:
    """Return the first word of each row, keyed by the heading above it.

    A row starts two columns in; a wrapped description sits deeper and is skipped.
    Colour codes are removed first, so a heading reads the same with or without colour.
    """
    grouped: dict[str, list[str]] = {}
    heading = ""
    for line in K_ANSI_ESCAPE.sub("", page).splitlines():
        if line and not line.startswith(" "):
            heading = line.rstrip()
            grouped[heading] = []
        elif line.startswith("  ") and not line.startswith("   "):
            grouped[heading].append(line.split()[0])
    return grouped


def test_root_help_lists_the_four_groups_with_the_sub_app_last():
    lines = [line.rstrip() for line in _run("--help").splitlines()]
    positions = [lines.index(heading) for heading in K_GROUPS]
    assert positions == sorted(positions)


@pytest.mark.parametrize("heading", list(K_GROUPS))
def test_every_command_sits_under_its_own_group(heading):
    listed = _commands_by_heading(_run("--help"))[heading]
    assert [name for name in listed if name in K_GROUPS[heading]] == K_GROUPS[heading]


def test_no_command_is_listed_under_a_group_it_does_not_belong_to():
    grouped = _commands_by_heading(_run("--help"))
    for heading, names in K_GROUPS.items():
        assert set(grouped[heading]) == set(names)


@pytest.mark.parametrize(
    "command",
    [name for names in K_GROUPS.values() for name in names
     if name not in ("container", "backlog", "design")],
)
def test_a_command_help_carries_examples(command):
    out = _run(command, "--help")
    assert "Examples" in out and f"sr {command}" in out


@pytest.mark.parametrize("command", K_CONTAINER_COMMANDS)
def test_a_container_command_help_carries_examples(command):
    out = _run("container", command, "--help")
    assert "Examples" in out and f"sr container {command}" in out


def test_the_container_sub_app_draws_a_page_listing_its_commands():
    listed = _commands_by_heading(_run("container", "--help"))["Commands"]
    assert listed == K_CONTAINER_COMMANDS


def test_the_container_sub_app_without_arguments_draws_the_same_page():
    result = CliRunner().invoke(app, ["container"])
    assert "Commands" in result.output and "Examples" not in result.output
    assert _commands_by_heading(result.output)["Commands"] == K_CONTAINER_COMMANDS


def test_the_root_help_lists_the_root_options():
    out = _run("--help")
    assert "--version" in out and "--describe" in out


def test_the_root_without_arguments_draws_the_group_page():
    result = CliRunner().invoke(app, [])
    assert _commands_by_heading(result.output)["Toolchain"] == ["toolchain"]


def _raw_colour_lines(source: str) -> list[int]:
    """Return the sorted lines of every string constant that names a raw colour.

    An f-string's literal parts count as constants.

    A constant inside an f-string is an ``ast.Constant`` below the ``ast.JoinedStr``, so the
    walk sees it whichever way the interpreter tokenises the literal.
    """
    lines: list[int] = []
    for node in ast.walk(ast.parse(source)):
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            if K_RAW_MARKUP.search(node.value):
                lines.append(node.lineno)
        elif isinstance(node, ast.keyword) and node.arg in ("style", "header_style"):
            value = node.value
            if isinstance(value, ast.Constant) and K_RAW_STYLE.fullmatch(str(value.value)):
                lines.append(value.lineno)
    return sorted(set(lines))


def test_the_colour_scan_sees_markup_inside_an_f_string_and_a_style_argument():
    source = chr(10).join(
        [
            'x = f"[green]{a}[/green]"',
            'y = table.add_column("n", style="dim")',
            'z = "[bold]{a}"',
        ]
    )
    assert _raw_colour_lines(source) == [1, 2]


def test_no_raw_colour_name_is_left_in_the_package_source():
    root = Path(sushiruntime.__file__).parent
    found = [
        f"{path.relative_to(root)}:{number}"
        for path in sorted(root.rglob("*.py"))
        for number in _raw_colour_lines(path.read_text(encoding="utf-8"))
    ]
    assert found == []
