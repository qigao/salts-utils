"""Prevent DataBind from reintroducing the removed parser facade."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
BIND = ROOT / "tbe" / "data_bind"


class DirectParserBoundary(unittest.TestCase):
    def test_compat_directory_is_removed(self):
        self.assertFalse((BIND / "parser_compat").exists(),
                         "DataBind must consume Salts parsers, not a local facade")

    def test_active_sources_do_not_use_facade_apis(self):
        legacy = re.compile(
            r'#\s*include\s*[<"]turbo_parser[^>"\n]*[>"]|'
            r'\b(?:turbo_(?:json|csv|xml|yaml|query|dsv|datetime|parse|free)_\w*|'
            r'TURBO_(?:JSON|CSV|XML|YAML|QUERY)_\w*)\b')
        violations = []
        for source in sorted((ROOT / "tbe").rglob("*")):
            if source.suffix not in {".c", ".h", ".cpp", ".mustache"}:
                continue
            for number, line in enumerate(source.read_text().splitlines(), 1):
                if legacy.search(line):
                    violations.append(f"{source.relative_to(ROOT)}:{number}")
        self.assertEqual(violations, [], "Legacy parser API references: " +
                         ", ".join(violations[:20]))

    def test_build_and_install_do_not_export_compatibility(self):
        cmake = (BIND / "CMakeLists.txt").read_text()
        self.assertNotIn("parser_compat", cmake)
        self.assertNotIn("DATA_BIND_PARSER_COMPAT_TARGET", cmake)
        for target in ("JsonParser", "CsvParser", "XmlParser", "CYaml",
                       "CYamlJsonAdapter", "DateTimeParser", "QueryVM"):
            self.assertIn(f"Salts::{target}", cmake)

    def test_datetime_is_owned_by_databind_public_abi(self):
        header = (BIND / "data_bind.h").read_text()
        self.assertNotIn("#include <datetime_parser.h>", header)
        self.assertIsNone(re.search(r"\bdatetime_t\b", header))
        self.assertNotIn("turbo_datetime_t", header)
        self.assertRegex(
            header,
            r"typedef\s+struct\s+DataBindDateTime\s*\{[\s\S]*?\}\s*DataBindDateTime\s*;",
        )


if __name__ == "__main__":
    unittest.main()
