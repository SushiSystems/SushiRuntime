# test_check_naming.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Tests each rule of the naming checker on one snippet it reports and one it accepts."""

from __future__ import annotations

import sys
import tempfile
import unittest
from collections.abc import Callable, Iterator
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common.checker import Issue  # noqa: E402
from naming.check_naming import (  # noqa: E402
    K_CHECKER,
    K_FLAG_RULES,
    K_RULES,
    SourceFile,
    collect,
    rule_constant_prefix,
    rule_enumerator_case,
    rule_get_prefix,
    rule_interface_prefix,
    rule_namespace_detail,
    rule_short_name,
)

Rule = Callable[[SourceFile], Iterator[Issue]]


def _lines(
    rule: Rule,
    text: str,
    name: str = "sample.hpp",
    folder: tuple[str, ...] = (),
) -> list[int]:
    """Returns the line numbers the rule reports for a file with the given name and content."""
    with tempfile.TemporaryDirectory() as root:
        path = Path(root, *folder, name)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return [issue.line for source in collect([path]) for issue in rule(source)]


class NamespaceDetailTest(unittest.TestCase):
    """Checks which namespace declarations the namespace-detail rule reports."""

    def test_namespace_detail_reports_lowercase_name(self) -> None:
        """Reports a namespace declared as detail on the line of the declaration."""
        self.assertEqual(_lines(rule_namespace_detail, "namespace detail\n{\n}\n"), [1])

    def test_namespace_detail_accepts_capitalised_name(self) -> None:
        """Accepts a namespace declared as Detail."""
        self.assertEqual(_lines(rule_namespace_detail, "namespace Detail\n{\n}\n"), [])

    def test_namespace_detail_ignores_using_directive(self) -> None:
        """Accepts a using-directive that names the lowercase detail namespace."""
        self.assertEqual(_lines(rule_namespace_detail, "using namespace detail;\n"), [])


class InterfacePrefixTest(unittest.TestCase):
    """Checks which class and struct names the interface-prefix rule reports."""

    def test_interface_prefix_reports_i_prefixed_class(self) -> None:
        """Reports a class whose name carries an I prefix followed by a capital and a lowercase."""
        self.assertEqual(_lines(rule_interface_prefix, "class IRenderer\n{\n};\n"), [1])

    def test_interface_prefix_accepts_acronym_names(self) -> None:
        """Accepts class and struct names whose second letter starts an acronym."""
        self.assertEqual(_lines(rule_interface_prefix, "class ID {};\nstruct IOStream {};\n"), [])


class GetPrefixTest(unittest.TestCase):
    """Checks which function names the get-prefix rule reports."""

    def test_get_prefix_reports_declared_getter(self) -> None:
        """Reports a method declaration whose name starts with get_."""
        self.assertEqual(_lines(rule_get_prefix, "int get_count(int limit) const;\n"), [1])

    def test_get_prefix_accepts_call_of_third_party_function(self) -> None:
        """Accepts a call of a get_ function that is assigned, not declared."""
        self.assertEqual(_lines(rule_get_prefix, "int value = get_info(limit);\n"), [])


class ConstantPrefixTest(unittest.TestCase):
    """Checks which constants the constant-prefix rule reports."""

    def test_constant_prefix_reports_namespace_constant(self) -> None:
        """Reports a namespace-scope constexpr whose UPPER_SNAKE name lacks K_."""
        text = "namespace sushi\n{\nconstexpr int MAX_SIZE = 4;\n}\n"
        self.assertEqual(_lines(rule_constant_prefix, text), [3])

    def test_constant_prefix_accepts_prefixed_constant_and_macro(self) -> None:
        """Accepts a constant named with K_ and a #define macro, which is not checked."""
        text = "constexpr int K_MAX_SIZE = 4;\n#define MAX_SIZE 4\n"
        self.assertEqual(_lines(rule_constant_prefix, text), [])

    def test_constant_prefix_ignores_function_local_static(self) -> None:
        """Accepts a static const inside a function body, which is not namespace or class scope."""
        text = "void run()\n{\n    static const int LIMIT = 2;\n}\n"
        self.assertEqual(_lines(rule_constant_prefix, text), [])


class EnumeratorCaseTest(unittest.TestCase):
    """Checks which enumerators the enumerator-case rule reports."""

    def test_enumerator_case_reports_snake_case_enumerator(self) -> None:
        """Reports an enum class enumerator in UPPER_SNAKE on its own line."""
        text = "enum class Mode\n{\n    FAST_PATH,\n    Slow,\n};\n"
        self.assertEqual(_lines(rule_enumerator_case, text), [3])

    def test_enumerator_case_accepts_pascal_case_and_acronyms(self) -> None:
        """Accepts PascalCase enumerators and acronyms of up to five letters, and skips Errc."""
        text = "enum class Backend { Sycl, CUDA, ID };\nenum class Errc { BAD_ALLOC };\n"
        self.assertEqual(_lines(rule_enumerator_case, text), [])


class ShortNameTest(unittest.TestCase):
    """Checks which identifiers the short-name rule reports."""

    def test_short_name_reports_short_identifier_in_include_folder(self) -> None:
        """Reports the identifier ctx in a header under an include folder."""
        self.assertEqual(
            _lines(rule_short_name, "int ctx = 0;\n", "graph.hpp", ("include",)),
            [1],
        )

    def test_short_name_accepts_full_name_and_files_outside_include(self) -> None:
        """Accepts a full name in an include header, and a short name outside include."""
        self.assertEqual(
            _lines(rule_short_name, "int context = 0;\n", "graph.hpp", ("include",)),
            [],
        )
        self.assertEqual(_lines(rule_short_name, "int ctx = 0;\n", "graph.cpp", ("src",)), [])


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
