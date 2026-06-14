"""Rich-based console output for the SushiRuntime CLI."""

from __future__ import annotations

from rich.console import Console
from rich.panel import Panel
from rich.theme import Theme

_theme = Theme(
    {
        "info": "bold blue",
        "success": "bold green",
        "warn": "bold yellow",
        "error": "bold red",
        "cmd": "cyan",
    }
)

console = Console(theme=_theme)


def info(msg: str) -> None:
    console.print(f"[info][INFO][/info] {msg}")


def success(msg: str) -> None:
    console.print(f"[success][SUCCESS][/success] {msg}")


def warn(msg: str) -> None:
    console.print(f"[warn][WARN][/warn] {msg}")


def error(msg: str) -> None:
    console.print(f"[error][ERROR][/error] {msg}")


def command(cmd: str) -> None:
    """Echo the command about to be executed."""
    console.print(f"[info][INFO][/info] Executing: [cmd]{cmd}[/cmd]")


def header(title: str) -> None:
    console.print()
    console.rule(f"[bold magenta]{title}")


def fail_panel(title: str, body: str) -> None:
    console.print(Panel(body, title=f"[error]{title}", border_style="red"))
