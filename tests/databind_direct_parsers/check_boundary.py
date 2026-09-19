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

    def test_core_runtime_has_no_concrete_saltsutils_dependencies(self):
        cmake = (BIND / "CMakeLists.txt").read_text()
        self.assertIn("set(DATA_BIND_CORE_TARGET data_bind_core)", cmake)
        match = re.search(
            r"target_link_libraries\(\s*\$\{DATA_BIND_CORE_TARGET\}([\s\S]*?)\)",
            cmake,
        )
        self.assertIsNotNone(match, "DataBind core link contract not found")
        links = match.group(1)
        forbidden = (
            "Salts::JsonParser",
            "Salts::CsvParser",
            "Salts::XmlParser",
            "Salts::CYaml",
            "Salts::CYamlJsonAdapter",
            "Salts::DateTimeParser",
            "Salts::QueryVM",
        )
        leaked = [target for target in forbidden if target in links]
        self.assertEqual(
            leaked,
            [],
            "DataBind core still depends on concrete SaltsUtils targets: "
            + ", ".join(leaked),
        )
        for target in ("Salts::Core", "Salts::CMeta", "Salts::CSTL", "Salts::CSerde"):
            self.assertIn(target, links)

        facade = re.search(
            r"target_link_libraries\(\s*\$\{DATA_BIND_TARGET\}([\s\S]*?)\)",
            cmake,
        )
        self.assertIsNotNone(facade, "DataBind facade link contract not found")
        self.assertIn("Salts::DataBindCore", facade.group(1))
        self.assertIn("Salts::DataBindJsonAdapter", facade.group(1))
        self.assertIn("Salts::DataBindYamlAdapter", facade.group(1))
        self.assertIn("Salts::DataBindCsvAdapter", facade.group(1))
        self.assertIn("Salts::DataBindXmlAdapter", facade.group(1))
        self.assertNotIn("Salts::JsonParser", facade.group(1))
        self.assertNotIn("Salts::CsvParser", facade.group(1))
        self.assertNotIn("Salts::XmlParser", facade.group(1))
        self.assertNotIn("Salts::JsonCSerdeAdapter", facade.group(1))
        self.assertNotIn("Salts::CYaml", facade.group(1))
        self.assertNotIn("Salts::CYamlJsonAdapter", facade.group(1))

        json_adapter = re.search(
            r"target_link_libraries\(\s*\$\{DATA_BIND_JSON_ADAPTER_TARGET\}([\s\S]*?)\)",
            cmake,
        )
        self.assertIsNotNone(json_adapter, "JSON adapter link contract not found")
        self.assertIn("Salts::DataBindCore", json_adapter.group(1))
        self.assertIn("Salts::JsonCSerdeAdapter", json_adapter.group(1))

        yaml_adapter = re.search(
            r"target_link_libraries\(\s*\$\{DATA_BIND_YAML_ADAPTER_TARGET\}([\s\S]*?)\)",
            cmake,
        )
        self.assertIsNotNone(yaml_adapter, "YAML adapter link contract not found")
        self.assertIn("Salts::DataBindCore", yaml_adapter.group(1))
        self.assertIn("Salts::CYamlJsonAdapter", yaml_adapter.group(1))
        self.assertIn("Salts::JsonCSerdeAdapter", yaml_adapter.group(1))

        csv_adapter = re.search(
            r"target_link_libraries\(\s*\$\{DATA_BIND_CSV_ADAPTER_TARGET\}([\s\S]*?)\)",
            cmake,
        )
        self.assertIsNotNone(csv_adapter, "CSV adapter link contract not found")
        self.assertIn("Salts::DataBindCore", csv_adapter.group(1))
        self.assertIn("Salts::CsvParser", csv_adapter.group(1))

        xml_adapter = re.search(
            r"target_link_libraries\(\s*\$\{DATA_BIND_XML_ADAPTER_TARGET\}([\s\S]*?)\)",
            cmake,
        )
        self.assertIsNotNone(xml_adapter, "XML adapter link contract not found")
        self.assertIn("Salts::DataBindCore", xml_adapter.group(1))
        self.assertIn("Salts::XmlParser", xml_adapter.group(1))

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
