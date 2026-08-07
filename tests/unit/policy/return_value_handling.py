#!/usr/bin/env python3
"""Reject first-party function-call result suppression."""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]


SOURCE_DIRECTORIES = (
    "CoolStuff/Testbed",
    "core",
    "global",
    "impl",
    "loader",
    "logger",
    "resource_cooker",
    "tests",
    "utilities",
)
SOURCE_SUFFIXES = frozenset((".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ixx"))
STATIC_VOID_CAST = re.compile(r"static_cast\s*<\s*void\s*>\s*\(")
C_STYLE_VOID_CAST = re.compile(r"\(\s*void\s*\)")
MAYBE_UNUSED = re.compile(r"\[\[\s*maybe_unused\s*\]\]")
STD_IGNORE_ASSIGNMENT = re.compile(r"\bstd\s*::\s*ignore\s*=(?!=)")
CALL = re.compile(r"(?<!\w)([A-Za-z_]\w*)\s*\(")
CALL_AT_START = re.compile(
    r"(?:(?:::)?([A-Za-z_]\w*)(?:(?:::[A-Za-z_]\w*)|(?:->|\.)[A-Za-z_]\w*)*)\s*\("
)
TEMPLATE_CALL_AT_START = re.compile(
    r"(?:(?:::)?[A-Za-z_]\w*(?:(?:::\s*(?:template\s+)?[A-Za-z_]\w*)|(?:->|\.)\s*(?:template\s+)?[A-Za-z_]\w*)*)\s*<"
)
FUNCTION_POINTER_CALL = re.compile(r"\(\s*\*\s*(?:[A-Za-z_]\w*\s*)?\)\s*\(")
GROUPED_CALL = re.compile(r"[)}]\s*\(")
LOCAL_ASSIGNMENT = r"(?<![\w.>]){}\s*=(?!=)"
LOCAL_DIRECT_INITIALIZER = r"(?<![\w.>:]){}\s*(?=[({{])"
DIRECT_INITIALIZER = re.compile(r"(?<![\w.>:])([A-Za-z_]\w*)\s*(?=[({])")
ASSIGNMENT_OPERATOR = re.compile(r"(?<![=!<>])=(?!=)")
SINGLE_IDENTIFIER = re.compile(r"\s*([A-Za-z_]\w*)\s*")
IDENTIFIER = re.compile(r"(?<!\w)([A-Za-z_]\w*)")
UNEVALUATED_OPERATORS = frozenset(("alignof", "decltype", "noexcept", "sizeof", "typeid"))
CAST_IDENTIFIERS = frozenset(("const_cast", "dynamic_cast", "reinterpret_cast", "static_cast"))
NON_CALL_IDENTIFIERS = UNEVALUATED_OPERATORS | CAST_IDENTIFIERS
NON_DECLARATION_IDENTIFIERS = frozenset((
    "case", "catch", "co_return", "delete", "do", "else", "for", "if", "new", "return", "switch", "throw", "while",
))


def blank_non_code(source: str) -> str:
    """Replace comments and literals with spaces while preserving line locations."""
    result = list(source)
    index = 0
    length = len(source)

    def blank(begin: int, end: int) -> None:
        for position in range(begin, end):
            if result[position] != "\n":
                result[position] = " "

    while index < length:
        if source.startswith("//", index):
            end = source.find("\n", index)
            if end == -1:
                end = length
            blank(index, end)
            index = end
            continue
        if source.startswith("/*", index):
            end = source.find("*/", index + 2)
            end = length if end == -1 else end + 2
            blank(index, end)
            index = end
            continue
        if source.startswith('R"', index):
            delimiter_end = source.find("(", index + 2)
            if delimiter_end != -1:
                delimiter = source[index + 2 : delimiter_end]
                terminator = ")" + delimiter + '"'
                end = source.find(terminator, delimiter_end + 1)
                end = length if end == -1 else end + len(terminator)
                blank(index, end)
                index = end
                continue
        if source[index] in ("'", '"'):
            quote = source[index]
            end = index + 1
            while end < length:
                if source[end] == "\\":
                    end += 2
                    continue
                if source[end] == quote:
                    end += 1
                    break
                end += 1
            blank(index, min(end, length))
            index = end
            continue
        index += 1

    return "".join(result)


def matching_delimiter(source: str, opening: int, opening_char: str, closing_char: str) -> int | None:
    depth = 1
    for position in range(opening + 1, len(source)):
        if source[position] == opening_char:
            depth += 1
        elif source[position] == closing_char:
            depth -= 1
            if depth == 0:
                return position
    return None


def matching_parenthesis(source: str, opening: int) -> int | None:
    return matching_delimiter(source, opening, "(", ")")


def matching_template_opening(source: str, closing: int) -> int | None:
    depth = 1
    parenthesis_depth = 0
    bracket_depth = 0
    brace_depth = 0
    for position in range(closing - 1, -1, -1):
        character = source[position]
        if character == ")":
            parenthesis_depth += 1
            continue
        if character == "(" and parenthesis_depth:
            parenthesis_depth -= 1
            continue
        if character == "]":
            bracket_depth += 1
            continue
        if character == "[" and bracket_depth:
            bracket_depth -= 1
            continue
        if character == "}":
            brace_depth += 1
            continue
        if character == "{" and brace_depth:
            brace_depth -= 1
            continue
        if parenthesis_depth or bracket_depth or brace_depth:
            continue
        if character == ">":
            depth += 1
        elif character == "<":
            depth -= 1
            if depth == 0:
                return position
    return None


def matching_template_closing(source: str, opening: int) -> int | None:
    depth = 1
    parenthesis_depth = 0
    bracket_depth = 0
    brace_depth = 0
    for position in range(opening + 1, len(source)):
        character = source[position]
        if character == "(":
            parenthesis_depth += 1
            continue
        if character == ")" and parenthesis_depth:
            parenthesis_depth -= 1
            continue
        if character == "[":
            bracket_depth += 1
            continue
        if character == "]" and bracket_depth:
            bracket_depth -= 1
            continue
        if character == "{":
            brace_depth += 1
            continue
        if character == "}" and brace_depth:
            brace_depth -= 1
            continue
        if parenthesis_depth or bracket_depth or brace_depth:
            continue
        if character == "<":
            depth += 1
        elif character == ">":
            depth -= 1
            if depth == 0:
                return position
    return None


def template_call_before_parenthesis(expression: str, opening: int) -> bool:
    position = opening - 1
    while position >= 0 and expression[position].isspace():
        position -= 1
    if position < 0 or expression[position] != ">":
        return False

    template_opening = matching_template_opening(expression, position)
    if template_opening is None:
        return False
    # Keep this lexical check conservative: whitespace around angle brackets is
    # conventional for relational expressions and cannot be disambiguated here.
    if (
        template_opening == 0
        or expression[template_opening - 1].isspace()
        or template_opening + 1 == position
        or expression[template_opening + 1].isspace()
        or expression[position - 1].isspace()
    ):
        return False

    position = template_opening - 1
    while position >= 0 and expression[position].isspace():
        position -= 1
    if position < 0:
        return False
    if expression[position] in ")]}":
        return True
    if not (expression[position].isalnum() or expression[position] == "_"):
        return False

    identifier_end = position + 1
    while position >= 0 and (expression[position].isalnum() or expression[position] == "_"):
        position -= 1
    return expression[position + 1 : identifier_end] not in NON_CALL_IDENTIFIERS


def contains_template_call(expression: str) -> bool:
    return any(
        character == "(" and template_call_before_parenthesis(expression, position)
        for position, character in enumerate(expression)
    )


def is_unevaluated_expression(expression: str) -> bool:
    return re.match(r"(?:alignof|decltype|noexcept|sizeof)\b", ungroup_expression(expression)) is not None


def contains_call(expression: str) -> bool:
    if is_unevaluated_expression(expression):
        return False
    if FUNCTION_POINTER_CALL.search(expression):
        return True
    if any(match.group(1) not in NON_CALL_IDENTIFIERS for match in CALL.finditer(expression)):
        return True
    return GROUPED_CALL.search(expression) is not None or contains_template_call(expression)


def starts_with_lambda_call(expression: str, start: int) -> bool:
    if start >= len(expression) or expression[start] != "[":
        return False

    capture_closing = matching_delimiter(expression, start, "[", "]")
    if capture_closing is None:
        return False

    body_opening = expression.find("{", capture_closing + 1)
    if body_opening == -1 or ";" in expression[capture_closing + 1 : body_opening]:
        return False

    body_closing = matching_delimiter(expression, body_opening, "{", "}")
    if body_closing is None:
        return False

    after_body = body_closing + 1
    while after_body < len(expression) and expression[after_body].isspace():
        after_body += 1
    return after_body < len(expression) and expression[after_body] == "("


def starts_with_template_call(expression: str, start: int) -> bool:
    match = TEMPLATE_CALL_AT_START.match(expression, start)
    if match is None:
        return False

    template_opening = match.end() - 1
    template_closing = matching_template_closing(expression, template_opening)
    if template_closing is None:
        return False

    opening = template_closing + 1
    while opening < len(expression) and expression[opening].isspace():
        opening += 1
    return opening < len(expression) and expression[opening] == "(" and template_call_before_parenthesis(expression, opening)


def starts_with_call(expression: str) -> bool:
    start = len(expression) - len(expression.lstrip())
    call = CALL_AT_START.match(expression, start)
    if (call is not None and call.group(1) not in NON_CALL_IDENTIFIERS) or FUNCTION_POINTER_CALL.match(expression, start):
        return True
    if starts_with_lambda_call(expression, start):
        return True
    if starts_with_template_call(expression, start):
        return True
    if start >= len(expression) or expression[start] != "(":
        return False

    closing = matching_parenthesis(expression, start)
    if closing is None:
        return False
    if starts_with_call(expression[start + 1 : closing]):
        return True
    after_group = closing + 1
    while after_group < len(expression) and expression[after_group].isspace():
        after_group += 1
    if after_group < len(expression) and expression[after_group] == "<":
        template_closing = matching_template_closing(expression, after_group)
        if template_closing is not None:
            opening = template_closing + 1
            while opening < len(expression) and expression[opening].isspace():
                opening += 1
            return opening < len(expression) and expression[opening] == "("
    return after_group < len(expression) and expression[after_group] == "("


def line_number(source: str, position: int) -> int:
    return source.count("\n", 0, position) + 1


def ungroup_expression(expression: str) -> str:
    expression = expression.strip()
    while expression.startswith("("):
        closing = matching_parenthesis(expression, 0)
        if closing is None or closing != len(expression) - 1:
            break
        expression = expression[1:closing].strip()
    return expression


def open_scopes(source: str, position: int) -> list[int]:
    scopes: list[int] = []
    for index, character in enumerate(source[:position]):
        if character == "{":
            scopes.append(index)
        elif character == "}" and scopes:
            scopes.pop()
    return scopes


def statement_end(source: str, start: int, limit: int) -> int | None:
    parenthesis_depth = 0
    bracket_depth = 0
    brace_depth = 0
    for position in range(start, limit):
        character = source[position]
        if character == "(":
            parenthesis_depth += 1
        elif character == ")":
            if parenthesis_depth == 0:
                return None
            parenthesis_depth -= 1
        elif character == "[":
            bracket_depth += 1
        elif character == "]":
            if bracket_depth == 0:
                return None
            bracket_depth -= 1
        elif character == "{":
            brace_depth += 1
        elif character == "}":
            if brace_depth == 0:
                return None
            brace_depth -= 1
        elif character == ";" and parenthesis_depth == 0 and bracket_depth == 0 and brace_depth == 0:
            return position
    return None


def has_unmatched_grouping(expression: str) -> bool:
    matching_opening = {")": "(", "]": "[", "}": "{"}
    openings: list[str] = []
    for character in expression:
        if character in "([{":
            openings.append(character)
        elif character in matching_opening:
            if not openings or openings.pop() != matching_opening[character]:
                return True
    return bool(openings)


def direct_initializer_is_local(source: str, identifier_start: int, identifier: str) -> bool:
    if identifier in NON_CALL_IDENTIFIERS:
        return False

    statement_start = max(
        source.rfind(";", 0, identifier_start),
        source.rfind("{", 0, identifier_start),
        source.rfind("}", 0, identifier_start),
    ) + 1
    prefix = source[statement_start:identifier_start].rstrip()
    if ASSIGNMENT_OPERATOR.search(prefix) or has_unmatched_grouping(prefix):
        return False
    identifiers = tuple(IDENTIFIER.finditer(prefix))
    return bool(identifiers) and identifiers[-1].group(1) not in NON_DECLARATION_IDENTIFIERS


def local_initializer_contains_call(source: str, position: int, identifier: str, active_scopes: list[int]) -> bool:
    last_initializer: str | None = None
    initializer_matches = (
        *((match, False) for match in re.compile(LOCAL_ASSIGNMENT.format(re.escape(identifier))).finditer(source, 0, position)),
        *((match, True) for match in re.compile(LOCAL_DIRECT_INITIALIZER.format(re.escape(identifier))).finditer(source, 0, position)),
    )
    for match, is_direct_initializer in sorted(initializer_matches, key=lambda candidate: candidate[0].start()):
        if is_direct_initializer and not direct_initializer_is_local(source, match.start(), identifier):
            continue
        assignment_scopes = open_scopes(source, match.start())
        if active_scopes[: len(assignment_scopes)] != assignment_scopes:
            continue

        initializer_end = statement_end(source, match.end(), position)
        if initializer_end is None:
            continue
        last_initializer = source[match.end() : initializer_end]

    return last_initializer is not None and initializer_contains_call(last_initializer)


def local_call_result_cast(source: str, position: int, expression: str) -> bool:
    if is_unevaluated_expression(expression):
        return False

    expression = ungroup_expression(expression)
    while True:
        while expression and expression[0] in "!+-~":
            expression = ungroup_expression(expression[1:])

        unwrapped_cast = False
        for cast_identifier in CAST_IDENTIFIERS:
            if not expression.startswith(cast_identifier):
                continue
            template_opening = len(cast_identifier)
            while template_opening < len(expression) and expression[template_opening].isspace():
                template_opening += 1
            if template_opening >= len(expression) or expression[template_opening] != "<":
                continue
            template_closing = matching_template_closing(expression, template_opening)
            if template_closing is None:
                continue
            opening = template_closing + 1
            while opening < len(expression) and expression[opening].isspace():
                opening += 1
            if opening >= len(expression) or expression[opening] != "(":
                continue
            closing = matching_parenthesis(expression, opening)
            if closing != len(expression) - 1:
                continue
            expression = ungroup_expression(expression[opening + 1 : closing])
            unwrapped_cast = True
            break
        if not unwrapped_cast:
            break

    identifier_match = SINGLE_IDENTIFIER.fullmatch(expression)
    if identifier_match is None:
        return False
    return local_initializer_contains_call(source, position, identifier_match.group(1), open_scopes(source, position))


def assignment_initializer_start(source: str, start: int, end: int) -> int | None:
    parenthesis_depth = 0
    bracket_depth = 0
    brace_depth = 0
    for position in range(start, end):
        character = source[position]
        if character == "(":
            parenthesis_depth += 1
        elif character == ")":
            parenthesis_depth -= 1
        elif character == "[":
            bracket_depth += 1
        elif character == "]":
            bracket_depth -= 1
        elif character == "{":
            brace_depth += 1
        elif character == "}":
            brace_depth -= 1
        elif character == "=" and parenthesis_depth == 0 and bracket_depth == 0 and brace_depth == 0:
            if ASSIGNMENT_OPERATOR.match(source, position):
                return position + 1
    return None


def maybe_unused_initializer_start(source: str, start: int) -> int | None:
    end = statement_end(source, start, len(source))
    if end is None:
        return None

    assignment = assignment_initializer_start(source, start, end)
    if assignment is not None:
        return assignment

    for match in DIRECT_INITIALIZER.finditer(source, start, end):
        identifier = match.group(1)
        if direct_initializer_is_local(source, match.start(), identifier):
            return match.end()
    return None


def initializer_contains_call(expression: str) -> bool:
    expression = expression.replace("\\\n", "").lstrip()
    if is_unevaluated_expression(expression):
        return False
    if expression.startswith("[") and not starts_with_lambda_call(expression, 0):
        return False
    return contains_call(expression)


def find_discarded_calls(source: str) -> list[tuple[int, str]]:
    code = blank_non_code(source)
    violations: list[tuple[int, str]] = []

    for match in STD_IGNORE_ASSIGNMENT.finditer(code):
        expression_end = statement_end(code, match.end(), len(code))
        if expression_end is None:
            continue
        expression = code[match.end() : expression_end]
        if initializer_contains_call(expression) or local_call_result_cast(code, match.start(), expression):
            violations.append((line_number(code, match.start()), "std::ignore assignment"))

    for match in STATIC_VOID_CAST.finditer(code):
        opening = match.end() - 1
        closing = matching_parenthesis(code, opening)
        if closing is None:
            continue
        expression = code[opening + 1 : closing]
        if contains_call(expression):
            violations.append((line_number(code, match.start()), "static_cast<void>"))
        elif local_call_result_cast(code, match.start(), expression):
            violations.append((line_number(code, match.start()), "local call result"))

    for match in C_STYLE_VOID_CAST.finditer(code):
        expression_end = statement_end(code, match.end(), len(code))
        expression = code[match.end() : expression_end if expression_end is not None else len(code)]
        if starts_with_call(expression):
            violations.append((line_number(code, match.start()), "C-style void cast"))
            continue

        if local_call_result_cast(code, match.start(), expression):
            violations.append((line_number(code, match.start()), "local call result"))

    for match in MAYBE_UNUSED.finditer(code):
        initializer = maybe_unused_initializer_start(code, match.end())
        if initializer is None:
            continue
        end = statement_end(code, initializer, len(code))
        if end is None:
            continue
        if initializer_contains_call(code[initializer:end]):
            violations.append((line_number(code, match.start()), "maybe_unused local"))

    return violations


def source_files(source_root: Path) -> list[Path]:
    return sorted(
        path
        for directory in SOURCE_DIRECTORIES
        for path in (source_root / directory).rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    )


def run_self_test() -> int:
    cases = (
        ("direct call", "static_cast<void>(callback());", ((1, "static_cast<void>"),)),
        ("templated direct call", "static_cast<void>(callback<int>());", ((1, "static_cast<void>"),)),
        ("templated member call", "static_cast<void>(object.template callback<int>());", ((1, "static_cast<void>"),)),
        ("nested templated direct call", "static_cast<void>(callback<Wrapper<int>>());", ((1, "static_cast<void>"),)),
        ("relational expression", "static_cast<void>(first < second > (third));", ()),
        ("parenthesized callable", "static_cast<void>((callback)());", ((1, "static_cast<void>"),)),
        ("immediate lambda", "static_cast<void>([]{}());", ((1, "static_cast<void>"),)),
        ("c-style parenthesized callable", "(void)(callback)();", ((1, "C-style void cast"),)),
        ("c-style templated callable", "(void)callback<int>();", ((1, "C-style void cast"),)),
        ("c-style nested templated callable", "(void)callback<Wrapper<int>>();", ((1, "C-style void cast"),)),
        ("c-style qualified dependent template callable", "(void)T::template callback<int>();", ((1, "C-style void cast"),)),
        ("c-style unused parameter before template call", "(void)parameter;\ncallback<int>();", ()),
        ("c-style unevaluated templated call", "(void)sizeof(callback<int>());", ()),
        ("c-style immediate lambda", "(void)([]{}());", ((1, "C-style void cast"),)),
        ("c-style immediate lambda with statements", "(void)[]{ handler(); return 0; }();", ((1, "C-style void cast"),)),
        ("local static-void cast", "const bool result = callback();\nstatic_cast<void>(result);", ((2, "local call result"),)),
        ("parenthesized assignment local static-void cast", "bool result = false;\nresult = (callback());\nstatic_cast<void>(result);", ((3, "local call result"),)),
        ("grouped local static-void cast", "const bool result = callback();\nstatic_cast<void>((result));", ((2, "local call result"),)),
        ("brace-initialized local static-void cast", "const bool result{callback()};\nstatic_cast<void>(result);", ((2, "local call result"),)),
        ("direct-initialized local static-void cast", "const bool result(callback());\nstatic_cast<void>(result);", ((2, "local call result"),)),
        ("function call is not a direct-initialized local", "const bool result = false;\ntarget = result(callback());\nstatic_cast<void>(result);", ()),
        ("local static-void cast with lambda", "const bool result = callback([]{ return false; });\nstatic_cast<void>(result);", ((2, "local call result"),)),
        ("cast-wrapped local static-void cast", "const bool result = callback();\nstatic_cast<void>(static_cast<bool>(result));", ((2, "local call result"),)),
        ("logical local static-void cast", "const bool result = callback();\nstatic_cast<void>(!!result);", ((2, "local call result"),)),
        ("cast-wrapped logical local static-void cast", "const bool result = callback();\nstatic_cast<void>(static_cast<bool>(!!result));", ((2, "local call result"),)),
        ("local c-style void cast", "const bool result = callback();\n(void)result;", ((2, "local call result"),)),
        ("cast-wrapped local c-style void cast", "const bool result = callback();\n(void)static_cast<bool>(result);", ((2, "local call result"),)),
        ("local non-call cast", "const bool result = false;\nstatic_cast<void>(result);", ()),
        ("wrapped local non-call cast", "const bool result = false;\nstatic_cast<void>(static_cast<bool>(result));", ()),
        ("unevaluated local", "const bool result = callback();\nstatic_cast<void>(sizeof(result));", ()),
        ("maybe-unused call", "[[maybe_unused]] const bool result = callback();", ((1, "maybe_unused local"),)),
        ("maybe-unused templated call", "[[maybe_unused]] const bool result = callback<int>();", ((1, "maybe_unused local"),)),
        ("maybe-unused brace-initialized call", "[[maybe_unused]] const bool result{callback()};", ((1, "maybe_unused local"),)),
        ("maybe-unused direct-initialized call", "[[maybe_unused]] const bool result(callback());", ((1, "maybe_unused local"),)),
        ("maybe-unused function pointer declaration", "[[maybe_unused]] bool (*result)(int) = callback;", ()),
        ("maybe-unused unevaluated call", "[[maybe_unused]] constexpr auto typeSize = sizeof(callback());", ()),
        ("maybe-unused lambda object", "[[maybe_unused]] const auto callback = []{ handler(); };", ()),
        ("std-ignore call", "std::ignore = callback();", ((1, "std::ignore assignment"),)),
        ("std-ignore templated call", "std::ignore = callback<int>();", ((1, "std::ignore assignment"),)),
        ("std-ignore local call result", "const bool result = callback();\nstd::ignore = result;", ((2, "std::ignore assignment"),)),
        ("unused parameter", "static_cast<void>(parameter);", ()),
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_discarded_calls(source))
        if actual != expected:
            print(f"{name}: expected {expected}, got {actual}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return run_self_test()

    source_root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else REPOSITORY_ROOT
    violations: list[str] = []
    for path in source_files(source_root):
        source = path.read_text(encoding="utf-8", errors="replace")
        for line, cast_kind in find_discarded_calls(source):
            violations.append(f"{path.relative_to(source_root)}:{line}: {cast_kind} conceals a function-call return value")

    if violations:
        print("Function-call return values must not be discarded:", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
