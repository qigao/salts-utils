#include <cstdio>
#include <cstring>
#include <cstdint>
#include <iostream>

#include <turbo_parser.hpp>

static void print_json_compat(void) {
  using namespace turbo::json;

  const char *json_text =
      R"({"users":[{"name":"Alice","age":30},{"name":"Bob","age":40}],"active":true})";
  Json root = Json::parse(json_text);
  std::cout << "JSON pretty: " << root.dump(2) << '\n';

  auto *compiled = turbo_jpath_compile("$.users[*].name");
  auto *names = turbo_jpath_query_compiled(&root, compiled);
  if (compiled != nullptr) {
    std::cout << "C++ compat jpath size: " << turbo_jpath_result_size(names) << '\n';
    for (std::size_t i = 0; i < turbo_jpath_result_size(names); ++i) {
      const Value *value = turbo_jpath_result_get(names, i);
      std::cout << "  name[" << i << "]=" << (value && value->is_string() ? value->get<std::string>()
                                                                        : std::string("<not string>"))
                << '\n';
    }
    turbo_jpath_result_free(names);
    turbo_jpath_program_free(compiled);
  }

  auto *all_matches = turbo_jpath_query(&root, "$.users[*].age");
  if (all_matches != nullptr) {
    std::cout << "C++ compat jpath direct query size: " << turbo_jpath_result_size(all_matches)
              << '\n';
    turbo_jpath_result_free(all_matches);
  }
}

static void print_yaml_compat(void) {
  namespace yaml_compat = turbo::yaml::compat;
  const char *yaml_text =
      "users:\n"
      "  - name: Alice\n"
      "    score: 100\n"
      "  - name: Bob\n"
      "    score: 75\n";

  yaml_compat::turbo_yaml_doc_t *doc = nullptr;
  if (yaml_compat::turbo_parse_yaml((const std::uint8_t *)yaml_text, std::strlen(yaml_text), &doc) !=
          0 ||
      doc == nullptr) {
    std::cout << "failed to parse YAML (C++ compat)\n";
    return;
  }

  auto *names = yaml_compat::turbo_ypath_query(doc, nullptr, "/users[*]/name");
  std::cout << "C++ compat ypath size: " << yaml_compat::turbo_ypath_result_size(names) << '\n';
  for (std::size_t i = 0; i < yaml_compat::turbo_ypath_result_size(names); ++i) {
    const ::turbo_yaml_node_t *node = yaml_compat::turbo_ypath_result_get(names, i);
    char *name = yaml_compat::turbo_yaml_scalar_dup(doc, node);
    std::cout << "  name[" << i << "]=" << (name == nullptr ? "<null>" : name) << '\n';
    yaml_compat::turbo_yaml_string_free(name);
  }
  yaml_compat::turbo_ypath_result_free(names);
  yaml_compat::turbo_free_yaml(&doc);
}

static void print_xml_compat(void) {
  namespace xml_compat = turbo::xml::compat;
  const char *xml_text = "<root><item>alpha</item><item>beta</item></root>";

  xml_compat::turbo_xml_doc_t *doc = nullptr;
  if (xml_compat::turbo_parse_xml((const std::uint8_t *)xml_text, std::strlen(xml_text), &doc) != 0 ||
      doc == nullptr) {
    std::cout << "failed to parse XML (C++ compat)\n";
    return;
  }

  xml_compat::turbo_xpath_list_t nodes;
  xml_compat::turbo_xpath_list_init(&nodes);
  xml_compat::turbo_xpath_query(doc, "//item", &nodes);
  std::cout << "C++ compat xpath count: " << xml_compat::turbo_xpath_list_size(&nodes) << '\n';
  for (std::size_t i = 0; i < xml_compat::turbo_xpath_list_size(&nodes); ++i) {
    const xml_compat::turbo_xpath_node_t *node = xml_compat::turbo_xpath_list_node(&nodes, i);
    const char *text = xml_compat::turbo_xpath_node_text(node);
    const char *name = xml_compat::turbo_xpath_node_name(node);
    std::cout << "  node[" << i << "] name=" << (name == nullptr ? "<null>" : name)
              << " text=" << (text == nullptr ? "<null>" : text) << '\n';
  }
  xml_compat::turbo_xpath_list_free(&nodes);
  xml_compat::turbo_free_xml(&doc);
}

int main() {
  print_json_compat();
  print_yaml_compat();
  print_xml_compat();
  return 0;
}
