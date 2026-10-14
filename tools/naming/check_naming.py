# check_naming.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Checks C++ names against the Sushi naming rules.

Usage: python tools/naming/check_naming.py [paths...] [--report] [--rule NAME] [--short-names]
"""

from __future__ import annotations

import bisect
import enum
import re
import sys
from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass, replace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from checking.checker import Checker, Issue, run  # noqa: E402

K_C_SUFFIXES = frozenset({".h", ".hpp", ".cpp"})
K_SKIPPED_FOLDERS = frozenset(
    {
        ".git",
        "third_party",
        "build",
        "build-asan",
        "build-tsan",
        "venv",
        "__pycache__",
        "node_modules",
    }
)
K_DEFAULT_PATH = "modules"
K_SHORT_NAMES_FLAG = "--short-names"
K_INCLUDE_FOLDER = "include"
K_SHORT_NAMES = ("ctx", "bg", "fn", "meta", "q", "deps", "err")
K_CONSTANT_PREFIX = "K_"
K_SKIPPED_ENUM = "Errc"
K_ACRONYM_LIMIT = 5
K_TAIL_WINDOW = 16

K_MASKED_SPAN = re.compile(
    r"""
    //(?:[^\n\\]|\\[\s\S])*
    |/\*[\s\S]*?(?:\*/|\Z)
    |R"(?P<delimiter>[^()\\\s]{0,16})\([\s\S]*?\)(?P=delimiter)"
    |"(?:\\[\s\S]|[^"\\\n])*"
    |(?<![0-9A-Fa-f_])'(?:\\[\s\S]|[^'\\\n])*'
    """,
    re.VERBOSE,
)
K_DIRECTIVE = re.compile(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", re.MULTILINE)
K_NOT_NEWLINE = re.compile(r"[^\n]")
K_NEWLINE = re.compile(r"\n")
K_BOUNDARY = re.compile(r"[{};]")
K_DECLARATOR_END = re.compile(r"[={(\[]")
K_ATTRIBUTE = re.compile(r"\[\[[^\]]*\]\]")
K_IDENTIFIER = re.compile(r"[A-Za-z_]\w*")
K_TRAILING_WORD = re.compile(r"[A-Za-z_]\w*$")
K_UPPER_SNAKE = re.compile(r"[A-Z][A-Z0-9]*(?:_[A-Z0-9]+)*")
K_CONSTEXPR = re.compile(r"\bconstexpr\b")
K_STATIC = re.compile(r"\bstatic\b")
K_CONST = re.compile(r"\bconst\b")
K_NAMESPACE_DETAIL = re.compile(r"\bnamespace\s+(?:\w+\s*::\s*)*detail\b")
K_USING_TAIL = re.compile(r"\busing\s+$")
K_INTERFACE = re.compile(r"\b(?:class|struct)\s+(I[A-Z][a-z]\w*)")
K_ELABORATED_TAIL = re.compile(r"\b(?:enum|friend)\s+$")
K_GET_CALL = re.compile(r"\bget_\w+\s*\(")
K_FUNCTION_TAIL = re.compile(
    r"\s*(?:[{;=]|->|:(?!:)|&&?|(?:const|volatile|noexcept|override|final|requires|throw)\b)"
)
K_EXPRESSION_WORDS = frozenset(
    {
        "return",
        "co_return",
        "co_await",
        "co_yield",
        "throw",
        "else",
        "case",
        "new",
        "delete",
        "sizeof",
        "goto",
        "do",
        "not",
        "and",
        "or",
        "typeid",
    }
)
K_ENUM_CLASS = re.compile(r"\benum\s+(?:class|struct)\s+(\w+)[^{;]*\{")
K_ENUMERATOR = re.compile(r"\s*([A-Za-z_]\w*)")
K_ALPHANUMERIC = re.compile(r"[A-Za-z0-9]+")
K_CAPITAL_RUN = re.compile(r"[A-Z]+")
K_SHORT_NAME = re.compile(r"\b(" + "|".join(K_SHORT_NAMES) + r")\b")
K_NAMESPACE_WORD = re.compile(r"\b(?:namespace|extern)\b")
K_CLASS_WORD = re.compile(r"\b(?:class|struct|union)\b")
K_ENUM_WORD = re.compile(r"\benum\b")


@dataclass(frozen=True, slots=True)
class SourceFile:
    """Holds one C++ file with comments, literals and directives blanked out of its code."""

    path: Path
    code: str
    line_starts: tuple[int, ...]

    def line_at(self, offset: int) -> int:
        """Returns the 1-based line number of a character offset in the code."""
        return bisect.bisect_right(self.line_starts, offset)


Rule = Callable[[SourceFile], Iterator[Issue]]


class _Scope(enum.Enum):
    """Names the kinds of brace scope that the constant rule tells apart."""

    NAMESPACE = enum.auto()
    CLASS = enum.auto()
    BLOCK = enum.auto()
    ENUM = enum.auto()
    INITIALIZER = enum.auto()
    NESTED = enum.auto()


K_CHECKED_SCOPES = frozenset({_Scope.NAMESPACE, _Scope.CLASS})
K_STATEMENT_ENDS = frozenset({_Scope.NAMESPACE, _Scope.CLASS, _Scope.BLOCK, _Scope.ENUM})


def _blank(text: str) -> str:
    """Returns the text with every character but a newline replaced by a space."""
    return K_NOT_NEWLINE.sub(" ", text)


def _blank_comments_and_literals(text: str) -> str:
    """Returns the text with comments and string or character literals blanked."""
    return K_MASKED_SPAN.sub(lambda match: _blank(match.group(0)), text)


def _blank_directives(code: str) -> str:
    """Returns the code with every preprocessor directive, continuation lines included, blanked."""
    return K_DIRECTIVE.sub(lambda match: _blank(match.group(0)), code)


def _line_starts(code: str) -> tuple[int, ...]:
    """Returns the offset at which each line of the code starts."""
    return (0, *(match.end() for match in K_NEWLINE.finditer(code)))


def _read_source(path: Path) -> SourceFile:
    """Returns a source file with its comments, literals and directives blanked."""
    text = path.read_text(encoding="utf-8-sig", errors="replace")
    code = _blank_directives(_blank_comments_and_literals(text))
    return SourceFile(path, code, _line_starts(code))


def _walk(paths: list[Path]) -> Iterator[Path]:
    """Yields every checked source file below the given paths."""
    for path in paths:
        if path.is_file():
            if path.suffix in K_C_SUFFIXES:
                yield path
            continue
        for candidate in sorted(path.rglob("*")):
            if candidate.suffix in K_C_SUFFIXES and not K_SKIPPED_FOLDERS.intersection(
                candidate.parts
            ):
                if candidate.is_file():
                    yield candidate


def collect(paths: list[Path]) -> Iterable[SourceFile]:
    """Returns every C++ source file below the given paths."""
    return (_read_source(path) for path in _walk(paths))


def _tail(code: str, start: int) -> str:
    """Returns the code just before an offset, long enough to test the word that precedes it."""
    return code[max(0, start - K_TAIL_WINDOW) : start]


def _matching_close(code: str, open_index: int, opener: str, closer: str) -> int | None:
    """Returns the index of the bracket that closes the one at open_index, or None."""
    depth = 0
    for index in range(open_index, len(code)):
        char = code[index]
        if char == opener:
            depth += 1
        elif char == closer:
            depth -= 1
            if depth == 0:
                return index
    return None


def _matching_angle(text: str) -> int | None:
    """Returns the index of the angle bracket that opens the template-id ending the text."""
    depth = 0
    for index in range(len(text) - 1, -1, -1):
        char = text[index]
        if char in ";{}":
            return None
        if char == ">":
            depth += 1
        elif char == "<":
            depth -= 1
            if depth == 0:
                return index if K_TRAILING_WORD.search(text[:index].rstrip()) else None
    return None


def _precedes_declarator(code: str, start: int) -> bool:
    """Returns whether the code before a name ends a type, which makes the name a declaration."""
    before = code[:start].rstrip()
    while before.endswith("::"):
        before = before[:-2].rstrip()
        if before.endswith(">"):
            opened = _matching_angle(before)
            if opened is None:
                return False
            before = before[:opened].rstrip()
        word = K_TRAILING_WORD.search(before)
        if word is None:
            return False
        before = before[: word.start()].rstrip()
    if not before:
        return False
    last = before[-1]
    if last == ">":
        return _matching_angle(before) is not None
    if last in "*&":
        return len(before) > 1 and (before[-2].isalnum() or before[-2] in "_>*&")
    word = K_TRAILING_WORD.search(before)
    return word is not None and word.group(0) not in K_EXPRESSION_WORDS


def _is_pascal_case(name: str) -> bool:
    """Returns whether a name is PascalCase, where a capital run is at most five letters."""
    if not K_ALPHANUMERIC.fullmatch(name) or not name[0].isupper():
        return False
    return all(len(run) <= K_ACRONYM_LIMIT for run in K_CAPITAL_RUN.findall(name))


def _top_level_pieces(body: str) -> Iterator[tuple[int, str]]:
    """Yields the offset and text of each comma-separated piece outside any bracket."""
    depth = 0
    start = 0
    for index, char in enumerate(body):
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif char == "," and depth == 0:
            yield start, body[start:index]
            start = index + 1
    yield start, body[start:]


def _classify_brace(head: str) -> _Scope:
    """Returns the scope a brace opens, judged from the statement text before it."""
    if K_ENUM_WORD.search(head):
        return _Scope.ENUM
    declares = "=" not in head and "(" not in head
    if declares and K_NAMESPACE_WORD.search(head):
        return _Scope.NAMESPACE
    if declares and K_CLASS_WORD.search(head):
        return _Scope.CLASS
    if ")" in head and "=" not in head:
        return _Scope.BLOCK
    return _Scope.INITIALIZER


def _statements_in_checked_scopes(code: str) -> Iterator[tuple[int, str]]:
    """Yields the offset and text of each statement that ends at namespace or class scope.

    A brace that opens an initializer stays inside its statement. A brace that opens a block,
    a class, a namespace or an enum closes the statement that came before it.
    """
    stack = [_Scope.NAMESPACE]
    start = 0
    for match in K_BOUNDARY.finditer(code):
        mark = match.group(0)
        checked = stack[-1] in K_CHECKED_SCOPES
        if mark == ";":
            if checked:
                yield start, code[start : match.start()]
                start = match.end()
        elif mark == "{":
            kind = _classify_brace(code[start : match.start()]) if checked else _Scope.NESTED
            stack.append(kind)
            if kind in K_CHECKED_SCOPES:
                start = match.end()
        elif len(stack) > 1 and stack.pop() in K_STATEMENT_ENDS:
            start = match.end()


def _is_constant_head(head: str) -> bool:
    """Returns whether a declaration head marks a constant: constexpr, or static const."""
    if K_CONSTEXPR.search(head):
        return True
    return bool(K_STATIC.search(head) and K_CONST.search(head))


def _constant_name(statement: str) -> tuple[str, int] | None:
    """Returns the declared name and its offset in the statement, or None for a non-constant.

    A name followed by an opening parenthesis is a function, so it returns None.
    """
    statement = K_ATTRIBUTE.sub(lambda match: " " * len(match.group(0)), statement)
    end = K_DECLARATOR_END.search(statement)
    head = statement[: end.start()] if end else statement
    if not _is_constant_head(head):
        return None
    names = list(K_IDENTIFIER.finditer(head))
    if not names or (end is not None and statement[end.start()] == "("):
        return None
    return names[-1].group(0), names[-1].start()


def rule_namespace_detail(source: SourceFile) -> Iterator[Issue]:
    """Yields an issue for each namespace declaration named detail instead of Detail."""
    for match in K_NAMESPACE_DETAIL.finditer(source.code):
        if K_USING_TAIL.search(_tail(source.code, match.start())):
            continue
        yield Issue(
            source.path,
            source.line_at(match.start()),
            "namespace detail must be named Detail",
        )


def rule_interface_prefix(source: SourceFile) -> Iterator[Issue]:
    """Yields an issue for each class or struct named with an I prefix."""
    for match in K_INTERFACE.finditer(source.code):
        if K_ELABORATED_TAIL.search(_tail(source.code, match.start())):
            continue
        name = match.group(1)
        yield Issue(
            source.path,
            source.line_at(match.start()),
            f"'{name}' carries an I prefix; an interface is named without it",
        )


def rule_get_prefix(source: SourceFile) -> Iterator[Issue]:
    """Yields an issue for each function or method declared with a get_ prefix."""
    code = source.code
    for match in K_GET_CALL.finditer(code):
        open_index = match.end() - 1
        close = _matching_close(code, open_index, "(", ")")
        if close is None or not K_FUNCTION_TAIL.match(code, close + 1):
            continue
        if not _precedes_declarator(code, match.start()):
            continue
        name = code[match.start() : open_index].rstrip()
        yield Issue(
            source.path,
            source.line_at(match.start()),
            f"'{name}' must not start with get_",
        )


def rule_constant_prefix(source: SourceFile) -> Iterator[Issue]:
    """Yields an issue for each namespace or class constant whose name lacks the K_ prefix."""
    for start, statement in _statements_in_checked_scopes(source.code):
        found = _constant_name(statement)
        if found is None:
            continue
        name, offset = found
        if len(name) > 1 and K_UPPER_SNAKE.fullmatch(name) and not name.startswith(K_CONSTANT_PREFIX):
            yield Issue(
                source.path,
                source.line_at(start + offset),
                f"'{name}' must start with {K_CONSTANT_PREFIX}",
            )


def rule_enumerator_case(source: SourceFile) -> Iterator[Issue]:
    """Yields an issue for each enumerator of an enum class that is not PascalCase."""
    code = source.code
    for match in K_ENUM_CLASS.finditer(code):
        enum_name = match.group(1)
        if enum_name == K_SKIPPED_ENUM:
            continue
        close = _matching_close(code, match.end() - 1, "{", "}")
        if close is None:
            continue
        body = K_ATTRIBUTE.sub(
            lambda attribute: " " * len(attribute.group(0)),
            code[match.end() : close],
        )
        for offset, piece in _top_level_pieces(body):
            found = K_ENUMERATOR.match(piece)
            if found is None:
                continue
            name = found.group(1)
            if not _is_pascal_case(name):
                yield Issue(
                    source.path,
                    source.line_at(match.end() + offset + found.start(1)),
                    f"'{name}' in {enum_name} must be PascalCase",
                )


def rule_short_name(source: SourceFile) -> Iterator[Issue]:
    """Yields an issue for each short name used in a header under an include folder."""
    if K_INCLUDE_FOLDER not in source.path.parts:
        return
    for match in K_SHORT_NAME.finditer(source.code):
        name = match.group(1)
        yield Issue(
            source.path,
            source.line_at(match.start()),
            f"'{name}' is a short name; spell it out",
        )


K_RULES: dict[str, Rule] = {
    "namespace-detail": rule_namespace_detail,
    "interface-prefix": rule_interface_prefix,
    "get-prefix": rule_get_prefix,
    "constant-prefix": rule_constant_prefix,
    "enumerator-case": rule_enumerator_case,
}
K_FLAG_RULES: dict[str, Rule] = {
    "short-name": rule_short_name,
}

K_CHECKER = Checker(
    description="Checks C++ names against the Sushi naming rules.",
    collect=collect,
    rules=K_RULES,
)


def _has_path_argument(arguments: list[str]) -> bool:
    """Returns whether the command line names a path, skipping the value that follows --rule."""
    skip_next = False
    for argument in arguments:
        if skip_next:
            skip_next = False
        elif argument == "--rule":
            skip_next = True
        elif not argument.startswith("-"):
            return True
    return False


def main(argv: list[str] | None = None) -> int:
    """Runs the naming checker from the command line and returns its exit code."""
    arguments = list(sys.argv[1:] if argv is None else argv)
    checker = K_CHECKER
    if K_SHORT_NAMES_FLAG in arguments:
        arguments = [argument for argument in arguments if argument != K_SHORT_NAMES_FLAG]
        checker = replace(K_CHECKER, rules={**K_RULES, **K_FLAG_RULES})
    if not _has_path_argument(arguments):
        arguments.append(K_DEFAULT_PATH)
    return run(checker, arguments)


if __name__ == "__main__":
    sys.exit(main())
