# __main__.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026 Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Enable `python3 -m sushiruntime` as an entry point."""

from .cli import app

if __name__ == "__main__":
    app()
