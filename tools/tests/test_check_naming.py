# test_check_naming.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.
"""Tests each rule of the naming checker on one snippet it reports and one it accepts."""

from __future__ import annotations

import sys
import unittest
from collections.abc import Callable, Iterator
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from checking.checker import Issue  # noqa: E402
from naming.check_naming import (  # noqa: E402
    K_CHECKER,
    K_FLAG_RULES,
    K_RULES,
    SourceFile,
    parse_source_text,
    rule_constant_prefix,
    rule_enumerator_case,
    rule_get_prefix,
    rule_interface_prefix,
    rule_namespace_detail,
    rule_short_name,
)

K_TEST_SIZE = "small"
Rule = Callable[[SourceFile], Iterator[Issue]]


def _lines(
    rule: Rule,
    text: str,
    name: str = "sample.hpp",
    folder: tuple[str, ...] = (),
) -> list[int]:
    """Returns the line numbers the rule reports for a file with the given name and content."""
    source = parse_source_text(Path(*folder, name), text)
    return [issue.line for issue in rule(source)]


class NamespaceDetailTest(unittest.TestCase):
    """Checks which namespace declarations the namespace-detail rule reports."""

    def test_namespace_detail_reports_only_the_lowercase_declaration(self) -> None:
        """Reports a namespace declared as detail, and not Detail or a using-directive naming detail."""
        text = "namespace detail\n{\n}\nnamespace Detail\n{\n}\nusing namespace detail;\n"
        self.assertEqual(_lines(rule_namespace_detail, text), [1])


class InterfacePrefixTest(unittest.TestCase):
    """Checks which class and struct names the interface-prefix rule reports."""

    def test_interface_prefix_reports_only_the_i_prefixed_class(self) -> None:
        """Reports an I prefix followed by a capital and a lowercase, and not an acronym such as ID."""
        text = "class IRenderer\n{\n};\nclass ID {};\nstruct IOStream {};\n"
        self.assertEqual(_lines(rule_interface_prefix, text), [1])


class GetPrefixTest(unittest.TestCase):
    """Checks which function names the get-prefix rule reports."""

    def test_get_prefix_reports_only_the_declared_getter(self) -> None:
        """Reports a get_ method declaration, and not a call of a get_ function that is assigned."""
        text = "int get_count(int limit) const;\nint value = get_info(limit);\n"
        self.assertEqual(_lines(rule_get_prefix, text), [1])


class ConstantPrefixTest(unittest.TestCase):
    """Checks which constants the constant-prefix rule reports."""

    def test_constant_prefix_reports_only_the_unprefixed_namespace_constant(self) -> None:
        """Reports a namespace-scope constexpr without K_, and not a K_ constant, a macro or a local static."""
        text = (
            "namespace sushi\n{\nconstexpr int MAX_SIZE = 4;\nconstexpr int K_MAX_SIZE = 4;\n"
            "#define MAX_SIZE 4\nvoid run()\n{\n    static const int LIMIT = 2;\n}\n}\n"
        )
        self.assertEqual(_lines(rule_constant_prefix, text), [3])


class EnumeratorCaseTest(unittest.TestCase):
    """Checks which enumerators the enumerator-case rule reports."""

    def test_enumerator_case_reports_only_the_upper_snake_enumerator(self) -> None:
        """Reports an UPPER_SNAKE enumerator, and not PascalCase, an acronym of up to five letters or Errc."""
        text = (
            "enum class Mode\n{\n    FAST_PATH,\n    Slow,\n};\n"
            "enum class Backend { Sycl, CUDA, ID };\nenum class Errc { BAD_ALLOC };\n"
        )
        self.assertEqual(_lines(rule_enumerator_case, text), [3])


class ShortNameTest(unittest.TestCase):
    """Checks which identifiers the short-name rule reports."""

    def test_short_name_reports_only_the_short_identifier_in_an_include_folder(self) -> None:
        """Reports ctx in an include header, and not context there nor ctx outside include."""
        reported = [
            _lines(rule_short_name, "int ctx = 0;\n", "graph.hpp", ("include",)),
            _lines(rule_short_name, "int context = 0;\n", "graph.hpp", ("include",)),
            _lines(rule_short_name, "int ctx = 0;\n", "graph.cpp", ("src",)),
        ]
        self.assertEqual(reported, [[1], [], []])


class MaskingTest(unittest.TestCase):
    """Checks that comments and string literals never reach the rules."""

    def test_comment_and_string_text_is_not_reported(self) -> None:
        """Reports nothing for a namespace name that appears in a comment or a string."""
        text = '// namespace detail {\nconst char* text = "namespace detail";\n'
        self.assertEqual(_lines(rule_namespace_detail, text), [])

    def test_digit_separator_does_not_open_a_literal(self) -> None:
        """Reports the declaration after a digit separator, which is not a character literal."""
        text = "constexpr int K_BIG = 1'000'000;\nconstexpr int MAX_LIMIT = 2;\n"
        self.assertEqual(_lines(rule_constant_prefix, text), [2])


class RegistrationTest(unittest.TestCase):
    """Checks the rule table that the checker runs."""

    K_TEST_SIZE = "small"

    def test_every_default_rule_is_registered(self) -> None:
        """Registers the five rules that run without a flag."""
        self.assertEqual(
            set(K_RULES),
            {
                "namespace-detail",
                "interface-prefix",
                "get-prefix",
                "constant-prefix",
                "enumerator-case",
            },
        )
        self.assertIs(K_CHECKER.rules["get-prefix"], rule_get_prefix)

    def test_short_name_is_registered_only_behind_the_flag(self) -> None:
        """Keeps short-name out of the default table and in the flag table."""
        self.assertNotIn("short-name", K_RULES)
        self.assertIs(K_FLAG_RULES["short-name"], rule_short_name)


if __name__ == "__main__":
    unittest.main()
