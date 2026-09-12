#!/usr/bin/env python3
"""Deterministic Jinja reference runner for compatibility cases."""

from __future__ import annotations

import argparse
import json
import sys
from importlib.metadata import version
from pathlib import Path
from typing import Any, Callable

from jinja2 import DictLoader, Environment, TemplateNotFound, TemplateRuntimeError
from jinja2 import TemplateSyntaxError, Undefined, UndefinedError


EXPECTED_JINJA_VERSION = "3.1.6"
EXPECTED_MARKUPSAFE_VERSION = "3.0.3"


def configure_output_encoding() -> None:
    """Keep Unicode oracle output deterministic across host console code pages."""
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is not None:
            reconfigure(encoding="utf-8", errors="strict")


def verify_versions() -> None:
    installed = {
        "Jinja2": version("Jinja2"),
        "MarkupSafe": version("MarkupSafe"),
    }
    expected = {
        "Jinja2": EXPECTED_JINJA_VERSION,
        "MarkupSafe": EXPECTED_MARKUPSAFE_VERSION,
    }
    if installed != expected:
        raise RuntimeError(f"oracle dependency mismatch: expected {expected}, got {installed}")


def build_environment(loader: DictLoader | None, extensions: tuple[str, ...] = ()) -> Environment:
    return Environment(
        block_start_string="{%",
        block_end_string="%}",
        variable_start_string="{{",
        variable_end_string="}}",
        comment_start_string="{#",
        comment_end_string="#}",
        line_statement_prefix=None,
        line_comment_prefix=None,
        trim_blocks=False,
        lstrip_blocks=False,
        newline_sequence="\n",
        keep_trailing_newline=False,
        extensions=extensions,
        optimized=True,
        undefined=Undefined,
        finalize=None,
        autoescape=False,
        loader=loader,
        cache_size=0,
        auto_reload=False,
        enable_async=False,
    )


def normalize_exception(error: Exception) -> dict[str, str]:
    # Some accepted Jinja ASTs fail when compiling the generated Python signature.
    if isinstance(error, (TemplateSyntaxError, SyntaxError)):
        category = "syntax"
    elif isinstance(error, UndefinedError):
        category = "undefined"
    elif isinstance(error, TemplateNotFound):
        category = "loader"
    elif isinstance(error, (TemplateRuntimeError, TypeError, ValueError, ZeroDivisionError)):
        category = "runtime"
    else:
        raise error
    return {"status": "error", "category": category, "type": type(error).__name__}


def evaluate_case(case: dict[str, Any]) -> dict[str, Any]:
    name = case.get("name")
    context = case.get("context", {})
    if not isinstance(name, str) or not name:
        raise ValueError("each oracle case requires a non-empty string name")
    if not isinstance(context, dict):
        raise ValueError(f"case {name!r} context must be an object")
    extensions = case.get("extensions", [])
    if not isinstance(extensions, list) or any(item != "jinja2.ext.do" for item in extensions):
        raise ValueError(f"case {name!r} extensions must be a list containing only jinja2.ext.do")

    templates = case.get("templates")
    render: Callable[[], str]
    if templates is not None:
        if not isinstance(templates, dict) or not all(
            isinstance(key, str) and isinstance(value, str) for key, value in templates.items()
        ):
            raise ValueError(f"case {name!r} templates must map strings to strings")
        entry = case.get("entry")
        if not isinstance(entry, str) or not entry:
            raise ValueError(f"case {name!r} requires a non-empty entry")
        environment = build_environment(DictLoader(templates), tuple(extensions))
        render = lambda: environment.get_template(entry).render(context)
    else:
        template_source = case.get("template")
        if not isinstance(template_source, str):
            raise ValueError(f"case {name!r} requires a string template")
        environment = build_environment(None, tuple(extensions))
        render = lambda: environment.from_string(template_source).render(context)

    try:
        return {"status": "ok", "output": render()}
    except Exception as error:
        return normalize_exception(error)


def load_case_file(path: Path) -> list[dict[str, Any]]:
    with path.open("r", encoding="utf-8") as stream:
        cases = json.load(stream)
    if not isinstance(cases, list):
        raise ValueError(f"oracle file {path} must contain a JSON array")
    names = [case.get("name") for case in cases if isinstance(case, dict)]
    if len(names) != len(cases) or len(set(names)) != len(names):
        raise ValueError(f"oracle file {path} must contain unique object cases with names")
    return cases


def load_cases(path: Path) -> list[dict[str, Any]]:
    if path.is_file():
        return load_case_file(path)
    if path.is_dir():
        files = sorted((entry for entry in path.iterdir() if entry.suffix.lower() == ".json"))
        cases: list[dict[str, Any]] = []
        for file in files:
            cases.extend(load_case_file(file))
        names = [case.get("name") for case in cases if isinstance(case, dict)]
        if len(names) != len(cases) or len(set(names)) != len(names):
            raise ValueError("oracle directory cases must contain unique names")
        return cases
    raise ValueError(f"{path} is not a file or directory")


def main() -> int:
    configure_output_encoding()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cases", type=Path)
    parser.add_argument("--verify", action="store_true", help="compare results with expected data")
    arguments = parser.parse_args()

    try:
        verify_versions()
        cases = load_cases(arguments.cases)
        results = []
        mismatches = []
        for case in cases:
            actual = evaluate_case(case)
            results.append({"name": case["name"], **actual})
            if arguments.verify and actual != case.get("expected"):
                mismatches.append(
                    {"name": case["name"], "expected": case.get("expected"), "actual": actual}
                )
        print(json.dumps(results, ensure_ascii=False, indent=2, sort_keys=True))
        if mismatches:
            print(json.dumps({"mismatches": mismatches}, ensure_ascii=False, indent=2), file=sys.stderr)
            return 1
        return 0
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"jinja oracle error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
