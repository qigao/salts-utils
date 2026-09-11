"""Enforce direct Salts parser ownership; run alongside the C runtime tests."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[3]
BIND = ROOT / 'tbe' / 'data_bind'


class NativeParserContract(unittest.TestCase):
    def test_compatibility_directory_is_removed(self):
        self.assertFalse((BIND / 'parser_compat').exists())

    def test_no_legacy_parser_surface(self):
        legacy = re.compile(r'\b(?:turbo_(?:parse|free|json|xml|yaml|csv|dsv|datetime|query|parser)|TURBO_(?:JSON|XML|YAML|CSV|QUERY|PARSER))')
        offenders = []
        for path in (ROOT / 'tbe').rglob('*'):
            if path.suffix in {'.c', '.h', '.cpp', '.mustache'}:
                if legacy.search(path.read_text(encoding='utf-8')):
                    offenders.append(str(path.relative_to(ROOT)))
        self.assertEqual(offenders, [], 'Legacy parser surface remains')

    def test_databind_links_native_parsers(self):
        cmake = (BIND / 'CMakeLists.txt').read_text(encoding='utf-8')
        self.assertNotIn('parser_compat', cmake)
        for target in ('JsonParser', 'CsvParser', 'CYaml', 'CYamlJsonAdapter',
                       'DateTimeParser', 'QueryVM', 'XmlParser'):
            self.assertIn('Salts::' + target, cmake)


if __name__ == '__main__':
    unittest.main(verbosity=2)
