"""Prevent DataBind from reintroducing the removed parser facade."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
BIND = ROOT / "databind" / "runtime"


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
        for source in sorted((ROOT / "databind").rglob("*")):
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
        self.assertIn("Salts::DataBindTemporalAdapter", facade.group(1))
        self.assertNotIn("Salts::JsonParser", facade.group(1))
        self.assertNotIn("Salts::CsvParser", facade.group(1))
        self.assertNotIn("Salts::XmlParser", facade.group(1))
        self.assertNotIn("Salts::JsonCSerdeAdapter", facade.group(1))
        self.assertNotIn("Salts::CYaml", facade.group(1))
        self.assertNotIn("Salts::CYamlJsonAdapter", facade.group(1))
        self.assertNotIn("Salts::DateTimeParser", facade.group(1))
        self.assertNotIn("Salts::QueryVM", facade.group(1))

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

        temporal_adapter = re.search(
            r"target_link_libraries\(\s*\$\{DATA_BIND_TEMPORAL_ADAPTER_TARGET\}([\s\S]*?)\)",
            cmake,
        )
        self.assertIsNotNone(
            temporal_adapter, "Temporal adapter link contract not found")
        self.assertIn("Salts::DataBindCore", temporal_adapter.group(1))
        self.assertIn("Salts::DateTimeParser", temporal_adapter.group(1))

    def test_incremental_stream_core_owns_no_concrete_parser_state(self):
        source = (BIND / "data_bind.c").read_text()
        struct_match = re.search(
            r"struct\s+data_bind_stream_t\s*\{([\s\S]*?)\n\};",
            source,
        )
        self.assertIsNotNone(struct_match, "data_bind_stream_t definition not found")
        stream_state = struct_match.group(1)
        forbidden_types = (
            "csv_doc_t",
            "dsv_filter_t",
            "json_sax_parser_t",
            "json_path_program_t",
            "json_path_stream_t",
            "json_value_t",
            "cyaml_sax_parser_t",
            "salts_xml_sax_parser_t",
        )
        leaked_types = [name for name in forbidden_types if name in stream_state]
        self.assertEqual(
            leaked_types,
            [],
            "provider-neutral stream state still owns concrete parser/query types: "
            + ", ".join(leaked_types),
        )

        destroy_match = re.search(
            r"void\s+data_bind_stream_destroy\s*\([^)]*\)\s*\{([\s\S]*?)\n\}",
            source,
        )
        self.assertIsNotNone(destroy_match, "data_bind_stream_destroy definition not found")
        destroy_body = destroy_match.group(1)
        forbidden_destroy = (
            "dsv_filter_destroy",
            "json_path_stream_destroy",
            "json_path_program_free",
            "json_sax_parser_destroy",
            "cyaml_sax_parser_destroy",
            "salts_xml_sax_parser_destroy",
            "csv_free",
            "json_free",
        )
        leaked_destroy = [name for name in forbidden_destroy if name in destroy_body]
        self.assertEqual(
            leaked_destroy,
            [],
            "DataBind stream destroy still owns concrete parser cleanup: "
            + ", ".join(leaked_destroy),
        )

    def test_incremental_stream_uses_versioned_provider_lease(self):
        source = (BIND / "data_bind.c").read_text()
        internal = (BIND / "data_bind_stream_format_state_internal.h").read_text()

        struct_match = re.search(
            r"struct\s+data_bind_stream_t\s*\{([\s\S]*?)\n\};",
            source,
        )
        self.assertIsNotNone(struct_match, "data_bind_stream_t definition not found")
        stream_state = struct_match.group(1)
        for loose_callback in ("feed_fn", "finish_fn", "bind_fn"):
            self.assertNotIn(
                loose_callback,
                stream_state,
                "stream shell still owns loose provider callback: " + loose_callback,
            )
        self.assertRegex(
            stream_state,
            r"data_bind_stream_provider_lease\s+provider",
        )

        self.assertIn("DATA_BIND_STREAM_PROVIDER_OPS_ABI_VERSION", internal)
        self.assertRegex(
            internal,
            r"typedef\s+struct\s+data_bind_stream_provider_ops\s*\{[\s\S]*?"
            r"size_t\s+size\s*;[\s\S]*?uint32_t\s+abi_version\s*;",
        )
        for operation in ("feed", "finish", "cancel", "destroy"):
            self.assertRegex(
                internal,
                rf"\(\*{operation}\)\s*\(",
                "provider ops missing lifecycle operation: " + operation,
            )

        destroy_match = re.search(
            r"void\s+data_bind_stream_destroy\s*\([^)]*\)\s*\{([\s\S]*?)\n\}",
            source,
        )
        self.assertIsNotNone(destroy_match, "data_bind_stream_destroy definition not found")
        self.assertNotIn(
            "data_bind_stream_format_state_cleanup",
            destroy_match.group(1),
            "stream shell still destroys concrete format state directly",
        )

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
