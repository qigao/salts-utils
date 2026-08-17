#include <stdio.h>
#include <string.h>

#include <turbo_parser.h>

static void print_json_example(void) {
  const char *json_text =
      "{\"users\":[{\"name\":\"Alice\",\"age\":30},{\"name\":\"Bob\",\"age\":40}],\"active\":true}";
  turbo_json_doc_t *json_doc = NULL;

  if (turbo_parse_json((const uint8_t *)json_text, strlen(json_text), &json_doc) != 0 ||
      json_doc == NULL) {
    printf("failed to parse JSON\n");
    return;
  }

  turbo_json_path_result_t *names = turbo_json_path_query(json_doc, "$.users[*].name");
  if (names != NULL) {
    const size_t count = turbo_json_path_result_size(names);
    printf("JSON path names: ");
    for (size_t i = 0; i < count; ++i) {
      const json_value_t *name = turbo_json_path_result_get(names, i);
      const char *text = name == NULL ? NULL : turbo_json_string(name);
      printf("%s%s", text == NULL ? "null" : text, (i + 1 == count) ? "\n" : ", ");
    }
  }

  const char *item_age = turbo_json_string(turbo_json_path_get(json_doc, "$.users[1].name"));
  if (item_age != NULL) {
    printf("JSON path indexed name: %s\n", item_age);
  }

  turbo_json_path_result_free(names);
  turbo_free_json(&json_doc);
}

static void print_yaml_example(void) {
  const char *yaml_text =
      "users:\n"
      "  - name: Alice\n"
      "    role: admin\n"
      "  - name: Bob\n"
      "    role: guest\n";
  turbo_yaml_doc_t *yaml_doc = NULL;

  if (turbo_parse_yaml((const uint8_t *)yaml_text, strlen(yaml_text), &yaml_doc) != 0 ||
      yaml_doc == NULL) {
    printf("failed to parse YAML\n");
    return;
  }

  turbo_yaml_path_result_t *names = turbo_yaml_path_query(yaml_doc, NULL, "/users[*]/name");
  if (names != NULL) {
    size_t count = turbo_yaml_path_result_size(names);
    printf("YAML users: ");
    for (size_t i = 0; i < count; ++i) {
      const turbo_yaml_node_t *node = turbo_yaml_path_result_get(names, i);
      char *name = turbo_yaml_scalar_dup(yaml_doc, node);
      if (name != NULL) {
        printf("%s%s", name, (i + 1 == count) ? "\n" : ", ");
        turbo_yaml_string_free(name);
      }
    }
  }

  turbo_yaml_path_result_free(names);
  turbo_free_yaml(&yaml_doc);
}

static void print_xml_example(void) {
  const char *xml_text =
      "<root id=\"a\"><item>first</item><item>second</item><item>third</item></root>";
  turbo_xml_doc_t *xml_doc = NULL;

  if (turbo_parse_xml((const uint8_t *)xml_text, strlen(xml_text), &xml_doc) != 0 ||
      xml_doc == NULL) {
    printf("failed to parse XML\n");
    return;
  }

  const char *first_item = turbo_xml_xpath_text(xml_doc, "/root/item[1]");
  const size_t item_count = turbo_xml_xpath_count(xml_doc, "/root/item");
  printf("XML XPath first text: %s\n", first_item == NULL ? "null" : first_item);
  printf("XML XPath count: %zu\n", item_count);

  turbo_xml_list_t matched_items;
  turbo_xml_list_init(&matched_items);
  turbo_xml_xpath_query(xml_doc, "//item", &matched_items);
  turbo_xml_for(xpath_item, &matched_items) {
    const char *node_text = turbo_xml_xpath_node_text(xpath_item);
    const char *node_name = turbo_xml_xpath_node_name(xpath_item);
    printf("XML item node: %s=%s\n", node_name == NULL ? "item" : node_name,
           node_text == NULL ? "null" : node_text);
  }
  turbo_xml_list_free(&matched_items);
  turbo_free_xml(&xml_doc);
}

int main(void) {
  print_json_example();
  print_yaml_example();
  print_xml_example();
  return 0;
}
