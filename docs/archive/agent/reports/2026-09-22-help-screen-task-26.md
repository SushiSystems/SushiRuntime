# Task 26: the grouped help screen and one set of colours for `sr`

Branch `feature/sushicore_help_screen`, cut from a clean `main`. Nothing pushed, merged or tagged.

## Files changed

- `cli/sushiruntime/console.py`: `current()` returns `_lazy.get()`. `_lazy` is never rebound in this module, so no rebinding case arose.
- `cli/sushiruntime/cli.py`: `_help_group = help_group(console.current)`, `cls=_help_group` on `app` and `docker_app`, `rich_help_panel` on `docker` and on every top-level command, an `epilog` on every command and sub-command, four panel constants.
- `cli/sushiruntime/services/select.py`, `cli/sushiruntime/services/project.py`: raw colour names mapped to the theme's names.
- `cli/pyproject.toml`: `sushicore>=0.1.0` to `sushicore>=0.4.0`.
- `cli/tests/test_help_screen.py`: new, 22 test cases.
- `docs/CLI_GUIDE.md`: one paragraph after the command overview table.
- `docs/reference/CHANGELOG.md`: three one-line entries under Unreleased.

## Panels

| Panel | Commands | Why |
| --- | --- | --- |
| Toolchain | `toolchain` | `docs/CLI_GUIDE.md` gives `sr toolchain` its own row in the command overview. |
| Project | `build`, `test`, `run`, `package`, `clean`, `doxygen` | The guide groups `build / test / run / clean / doxygen` on one row and `package` acts on the same build tree. |
| Diagnostics | `config`, `env` | `cli.py` already comments them as "config / env (diagnostics)". |
| Container | `docker` | The sub-app. Named "Container" and not "Docker" so the heading does not repeat the row under it. |

## Baseline and results

Before any change, from `D:/Projects/sushiruntime/cli`:

```
$ PYTHONPATH=D:/Projects/sushicore python -m pytest tests -q -p no:cacheprovider
...............................................                          [100%]
47 passed in 7.96s
```

New test file before the implementation (first version, a line regex; all 21 cases failed: Typer's own screen, no groups, no examples, 11 raw colour lines):

```
21 failed in 0.34s
```

After the coordinator's note on `tokenize`, the colour scan was rewritten to walk the `ast` for string constants (an f-string's literal parts are constants under `JoinedStr`) and for `style=` and `header_style=` keyword arguments. A self-test feeds it an f-string, a `style="dim"` argument and a bare `[bold]`, and expects only the first two lines. The scan run on the unfixed files from `HEAD`, then on the working tree:

```
select.py on HEAD (unfixed): [60, 61, 66, 69, 77, 80, 81, 96, 103, 104, 107, 110]
select.py on working tree: []
project.py on HEAD (unfixed): [594]
project.py on working tree: []
```

After:

```
$ PYTHONPATH=D:/Projects/sushicore python -m pytest tests -q -p no:cacheprovider
....................................................................     [100%]
69 passed in 5.91s
```

47 unchanged, 22 added.

```
$ python -m py_compile sushiruntime/cli.py sushiruntime/console.py sushiruntime/services/select.py sushiruntime/services/project.py tests/test_help_screen.py; echo "exit $?"
exit 0
```

## Help screens

Rendered in-process through `CliRunner` on a fake 90-column truecolour terminal (a stand-in console as in `sushicore/tests/test_typer_help.py`, installed as `_help_group.console_provider`; script in the session scratchpad, `sr_help_render.py`). ANSI codes stripped. Every run exited 0 and produced ANSI codes.

```
$ sr --help   (exit 0, ansi codes: True)

                  ▄█▀▀▀▀ ██  ██ ▄█▀▀▀▀ ██  ██ ██                    
   ▄▄▀▀▀▀▀▀██▄    ▀█▄▄▄  ██  ██ ▀█▄▄▄  ██▄▄██ ██                    
  ▄██▀▀▀▀▀██▀██       ██ ██  ██     ██ ██  ██ ██                    
  █████████▀████  ▀▀▀▀▀   ▀▀▀▀  ▀▀▀▀▀  ▀▀  ▀▀ ▀▀                    
  ██████████████   ▄▄▄▄▄ ▄▄  ▄▄  ▄▄▄▄▄ ▄▄▄▄▄▄ ▄▄▄▄▄▄ ▄▄    ▄▄  ▄▄▄▄▄
  ███▀▀██▀▀████▀  ██     ▀█▄▄█▀ ██       ██   ██     ███▄▄███ ██    
   ▀▀▀▀▀▀▀▀▀▀▀     ▀▀▀█▄   ██    ▀▀▀█▄   ██   ██▀▀   ██ ██ ██  ▀▀▀█▄
                  ▄▄▄▄█▀   ██   ▄▄▄▄█▀   ██   ██▄▄▄▄ ██    ██ ▄▄▄▄█▀


sr
SushiRuntime developer CLI — build, test, run, and Docker management.

Usage: sr [OPTIONS] COMMAND [ARGS]...

Toolchain
  toolchain  Choose the SYCL toolchain used for builds (persisted to config.local.toml).

Project
  build    Configure and build the project.                                     
  test     Run the test suite via CTest labels.                                 
  run      Run a built executable.                                              
  package  Install the built tree to a prefix and archive it for redistribution.
  clean    Remove the build/ and build-asan/ trees.                             
  doxygen  Generate Doxygen documentation.                                      

Diagnostics
  config  Print the resolved config and where each value came from.        
  env     Print the environment cmake/ctest/run subprocesses execute under.

Container
  docker  Docker environment management.

Options
  --help  Show this message and exit.

$ sr build --help   (exit 0, ansi codes: True)
sr build
Configure and build the project.

Usage: sr build [OPTIONS]

Options
  -t, --type [release|debug|relwithdebinfo|asan]  Build type. 'asan' uses an isolated     
                                                  build-asan tree (CUDA off).  [default:  
                                                  release]                                
  --distributed                                   Enable the master-worker distributed    
                                                  layer.                                  
  --no-cuda                                       Build the portable CPU/OpenCL path      
                                                  instead of detecting accelerators.      
  --clean                                         Delete the build tree before            
                                                  configuring.                            
  --force                                         If the selected toolchain's compiler is 
                                                  missing, fall back to clang++/clang     
                                                  instead of stopping.                    
  --no-test                                       Skip compiling the test suite           
                                                  (SR_BUILD_TESTS=OFF). Tests are built by
                                                  default.                                
  --help                                          Show this message and exit.             

Examples
  sr build                                                    
  sr build --type debug --clean     Rebuild from an empty tree
  sr build --no-cuda                Portable CPU/OpenCL path  
  sr build --distributed --no-test                            

$ sr docker --help   (exit 0, ansi codes: True)
sr docker
Docker environment management.

Usage: sr docker [OPTIONS] COMMAND [ARGS]...

Commands
  build  Build the sushiruntime Docker image.                
  run    Start the developer container with the repo mounted.

Options
  --help  Show this message and exit.

$ sr docker run --help   (exit 0, ansi codes: True)
sr docker run
Start the developer container with the repo mounted.

Usage: sr docker run [OPTIONS]

Options
  --admin   Run with --privileged --cap-add=SYS_ADMIN (for profiling).            
  --no-gpu  Omit --gpus all (useful when CUDA is disabled or no GPU is available).
  --help    Show this message and exit.                                           

Examples
  sr docker run                                    
  sr docker run --no-gpu  Headless or CI machines  
  sr docker run --admin   Privileged, for profiling
```

`sr run --help` was rendered before the `run` docstring edit below and showed the same example twice, once in the description and once under Examples:

```
$ sr run --help   (exit 0, ansi codes: True)
sr run
Run a built executable. Args after `--` are forwarded to it.

Example: sr run sr_benchmark_tests -- --gtest_filter='Bench.*'
```

## Colour mapping

Every change, from `git diff`:

```
-    console.console.print(f"[dim]{exe}[/dim]")                                        project.py
+    console.console.print(f"[muted]{exe}[/muted]")
-    table.add_column("Description", style="dim")                                     select.py
-    table.add_column("Install with", style="cyan")
+    table.add_column("Description", style="muted")
+    table.add_column("Install with", style="cmd")
-        status = "[green]installed[/green]" if installed else "[yellow]not installed[/yellow]"
+        status = "[success]installed[/success]" if installed else "[warn]not installed[/warn]"
-            "[bold green]→[/bold green]" if active else "",
+            "[success]→[/success]" if active else "",
-                 f"(switch with [cyan]sr toolchain <name>[/cyan])")
+                 f"(switch with [cmd]sr toolchain <name>[/cmd])")
-                     f"run [cyan]{_install_hint(cfg.toolchain)}[/cyan] to install it.")
+                     f"run [cmd]{_install_hint(cfg.toolchain)}[/cmd] to install it.")
-    console.console.print(f"[dim]{path}[/dim]")
+    console.console.print(f"[muted]{path}[/muted]")
-            f"    [bold cyan]{hint}[/bold cyan]"
+            f"    [cmd]{hint}[/cmd]"
-            console.info("oneAPI is several GB; use [cyan]hub install --customize[/cyan] "
+            console.info("oneAPI is several GB; use [cmd]hub install --customize[/cmd] "
-        console.info("Run [cyan]sr build --clean[/cyan] to reconfigure with the new toolchain.")
+        console.info("Run [cmd]sr build --clean[/cmd] to reconfigure with the new toolchain.")
```

Rich resolved `[muted]`, `[cmd]`, `[success]` and `[warn]` inside a `Table` column style and cell markup with `Theme().as_rich_styles()` registered (checked in a throwaway script; output showed codes `2`, `36`, `1;2;32`, `1;33`).

## Examples dropped

None dropped as wrong. Each was checked against the options in `cli.py`: `toolchain adaptivecpp` (argument values `intel-llvm | adaptivecpp | oneapi`); `build --type`, `--clean`, `--no-cuda`, `--distributed`, `--no-test`; `test --suite`, `--filter`, `--repeat` (`--suite all` is a `Suite` member); `run --sort` and a target with `--` forwarding; `package --prefix`, `--no-archive`; `docker build --no-cache`, `--oneapi`; `docker run --no-gpu`, `--admin`; `env --all`, `--asan`. `clean`, `doxygen` and `config` take no options and carry the bare command.

## What was not done, and things worth knowing

- Deviations from the brief's mapping: `[bold green]→[/bold green]` became `[success]` and `[bold cyan]` became `[cmd]`. A compound of `bold` and a theme name does not resolve in Rich, and the default theme's `success` and `cmd` styles already differ from plain colours. Two `style="dim"` and `style="cyan"` arguments to `Table.add_column` in `select.py` (lines 60 and 61) are raw colour names outside markup; I mapped them to `muted` and `cmd` because they break the shared palette the same way. The plan speaks only of markup.
- `[bold]` stays at `select.py:77` (bare bold, per the mapping) and `style="bold"` at `select.py:58`. No other colour or hex code was found in the package.
- I removed the `Example:` line from the `run` docstring because the Examples section now carries it (it appeared twice).
- The plan says `sr` has 10 raw markups; I count 10 markups over 9 source lines plus the 2 `style=` arguments. This is the same set.
- Changelog: two files exist. `docs/reference/CHANGELOG.md` is flat and dated (matches the house format and gets recent entries); `docs/CHANGELOG.md` is Keep a Changelog with sections, and `docs/CONTRIBUTING.md` line 227 says nothing goes there until v1.0.0. The three entries went into `docs/reference/CHANGELOG.md`. Which one is authoritative is the owner's call.
- The README paragraph went into `docs/CLI_GUIDE.md`, the only CLI document; the CLI has no `cli/README.md`. Its wording follows hub's paragraph and names `cli/config.local.toml`.
- `sr` has no callback and `no_args_is_help=True` on the root and on `docker_app`. The tests `test_the_root_without_arguments_draws_the_group_page` and `test_the_docker_sub_app_without_arguments_draws_the_same_page` show both still draw the page. `sr`'s exit code with no arguments was not asserted.
- `docker_app` had no `rich_markup_mode`; I set `"rich"` to match hub's sub-app.
- No app in `cli.py` is a one-command Typer without a callback.
- Not run: `sr build`, `sr test` or anything invoking cmake or ctest. The pipx-installed `sr` was not run on a real terminal; the screens above come from a stand-in console.
- `py_compile` and `pytest` were the syntax checks. No `clang` or shader compile applies to this repository's Python.
