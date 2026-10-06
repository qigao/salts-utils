#include "binary_layout_ir.h"
#include "binary_layout_lowering.h"
#include "compiler_core.h"
#include "binary_contract_overlay.h"
#include "data_bind_binary_reader.h"
#include "data_bind_binary_writer.h"
#include "data_bind_value_reader_internal.h"

#include <tinytest.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static const char ARRAY_SCALARS_TYPE[] = "ArrayScalars";
static const char ARRAY_RECORDS_TYPE[] = "ArrayRecords";
enum { ARRAY_SCALARS_WIRE_SIZE = 26u };

typedef struct LayoutGraphSink {
  unsigned char bytes[ARRAY_SCALARS_WIRE_SIZE];
  size_t length;
  size_t calls;
} LayoutGraphSink;

static int layout_graph_write(const void *bytes, size_t length, void *context) {
  LayoutGraphSink *sink = (LayoutGraphSink *)context;
  if (length > sizeof(sink->bytes)) return -1;
  memcpy(sink->bytes, bytes, length);
  sink->length = length;
  ++sink->calls;
  return 0;
}

#define LAYOUT_GRAPH_KEY(name_) \
  { .kind = CSERDE_STRING, \
    .value.slice = { (const unsigned char *)(name_), sizeof(name_) - 1u, CSERDE_VIEW_STABLE } }

spec("DataBind BinaryLayoutIR") {
  group("root projection resource admission") {
    static Node *root;
    static IdlContract *contract;
    static databind_binary_format_plan format;
    static tbe_error_t error;
    before_each() {
      static const char schema[] = "message Value { uint16 id; }";
      IdlDiagnostic diagnostic = {0};
      root = create_node_map(NULL);
      contract = NULL;
      format = (databind_binary_format_plan){0};
      error = (tbe_error_t){0};
      check_not_null(root);
      check_equal(databind_binary_contract_parse(schema, sizeof(schema) - 1u, root, &error), 0);
      check(idl_contract_parse(schema, sizeof(schema) - 1u, &contract, &diagnostic));
    }
    after_each() {
      databind_binary_format_plan_destroy(&format);
      idl_contract_destroy(contract);
      node_free(root);
    }
    it("rejects the type budget before reading or allocating an oversized metadata table") {
      IdlContract oversized = *contract;
      oversized.data_count = DATABIND_BINARY_FORMAT_MAX_TYPES + 1u;
      check_false(databind_binary_format_plan_build_root(&oversized, root, "Value", &format, &error));
      check_null(format.types);
      check_equal(format.type_count, 0u);
      check_contains(error.message, "type budget");
    }
    it("rejects the field budget before traversing unavailable field metadata") {
      IdlContract oversized = *contract;
      IdlDataDecl declaration = contract->data[0];
      declaration.field_count = DATABIND_BINARY_FORMAT_MAX_FIELDS + 1u;
      declaration.fields = NULL;
      oversized.data = &declaration;
      check_false(databind_binary_format_plan_build_root(&oversized, root, "Value", &format, &error));
      check_null(format.types);
      check_equal(format.type_count, 0u);
      check_contains(error.message, "field budget");
    }
  }

  group("shared execution graph") {
    static Node *root;
    static IdlContract *contract;
    static char *schema_data;
    static databind_binary_format_plan format;
    static databind_binary_execution_graph *graph;
    static cserde_writer *writer;
    static void *writer_owner;
    static cserde_reader *reader;
    static void *reader_owner;
    static LayoutGraphSink sink;
    static DataBind *codec;
    static DataBindValue *value;
    static cserde_reader *value_reader;

    before_each() {
      tbe_error_t error;
      root = NULL;
      contract = NULL;
      schema_data = NULL;
      format = (databind_binary_format_plan){0};
      graph = NULL;
      writer = NULL;
      writer_owner = NULL;
      reader = NULL;
      reader_owner = NULL;
      sink = (LayoutGraphSink){0};
      codec = NULL;
      value = NULL;
      value_reader = NULL;
      check_equal(databind_compiler_parse_contract_file(
                      BINARY_ARRAY_SCHEMA, &root, &contract, &schema_data), 0);
      tbe_error_init(&error);
      check(databind_binary_format_plan_build(contract, root, &format, &error));
    }

    after_each() {
      data_bind_value_reader_close(value_reader);
      data_bind_value_free(value);
      data_bind_free(codec);
      data_bind_binary_reader_close(reader, reader_owner);
      (void)data_bind_binary_writer_close(writer, writer_owner, NULL);
      databind_binary_execution_graph_destroy(graph);
      databind_binary_format_plan_destroy(&format);
      idl_contract_destroy(contract);
      node_free(root);
      free(schema_data);
    }

    it("retains canonical scalar array plans after all build inputs are released") {
      static const unsigned char expected[] = {
          0u, 0u, 1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u,
          0u, 0u, 0xc0u, 0x3fu, 0u, 0u, 0u, 0xbfu,
          0u, 0u, 0u, 0u, 0u, 0u, 3u, 0u};
      static const cserde_token tokens[] = {
          {.kind = CSERDE_MAP_BEGIN},
          LAYOUT_GRAPH_KEY("ready"), {.kind = CSERDE_ARRAY_BEGIN},
          {.kind = CSERDE_BOOL, .value.boolean = true},
          {.kind = CSERDE_BOOL, .value.boolean = false}, {.kind = CSERDE_ARRAY_END},
          LAYOUT_GRAPH_KEY("codes"), {.kind = CSERDE_ARRAY_BEGIN},
          {.kind = CSERDE_UINT, .value.uint = 0u},
          {.kind = CSERDE_UINT, .value.uint = 1u},
          {.kind = CSERDE_UINT, .value.uint = 0u}, {.kind = CSERDE_ARRAY_END},
          LAYOUT_GRAPH_KEY("ratios"), {.kind = CSERDE_ARRAY_BEGIN},
          {.kind = CSERDE_FLOAT, .value.floating = 1.5},
          {.kind = CSERDE_FLOAT, .value.floating = -0.5}, {.kind = CSERDE_ARRAY_END},
          LAYOUT_GRAPH_KEY("permissions"), {.kind = CSERDE_ARRAY_BEGIN},
          {.kind = CSERDE_UINT, .value.uint = 3u},
          {.kind = CSERDE_UINT, .value.uint = 0u}, {.kind = CSERDE_ARRAY_END},
          {.kind = CSERDE_MAP_END}};
      const DataBindBinaryLayoutPlan *plan;
      cserde_token token;
      cserde_status status;
      size_t i, signed_codes = 0u;
      check_equal(databind_binary_execution_graph_build(
                      contract, &format, ARRAY_SCALARS_TYPE, &graph, NULL),
                  DATABIND_BINARY_LAYOUT_OK);
      databind_binary_format_plan_destroy(&format);
      idl_contract_destroy(contract);
      contract = NULL;
      node_free(root);
      root = NULL;
      free(schema_data);
      schema_data = NULL;
      plan = databind_binary_execution_graph_root(graph);
      check_equal(plan->type_name, ARRAY_SCALARS_TYPE);
      check_equal(data_bind_binary_layout_plan_validate(plan, NULL), DATA_BIND_OK);
      check_equal(plan->array_plans[1]->element_flags, DATA_BIND_BINARY_FIELD_ENUM_BITS);
      check_equal(data_bind_binary_writer_open(
                      plan, layout_graph_write, &sink, 0u, &writer, &writer_owner, NULL),
                  DATA_BIND_OK);
      for (i = 0u; i < sizeof(tokens) / sizeof(tokens[0]); ++i)
        check_equal(cserde_writer_write(writer, &tokens[i]), CSERDE_OK);
      check_equal(cserde_writer_finish(writer), CSERDE_OK);
      check_equal(sink.calls, (size_t)1u);
      check_equal(sink.length, sizeof(expected));
      check_equal(sink.bytes, expected, sizeof(expected));
      check_equal(data_bind_binary_reader_open(
                      plan, sink.bytes, sink.length, 0u, &reader, &reader_owner, NULL),
                  DATA_BIND_OK);
      while ((status = cserde_reader_next(reader, &token)) == CSERDE_OK) {
        if (token.kind == CSERDE_SINT) {
          check_equal(token.value.sint, signed_codes == 1u ? INT64_C(1) : INT64_C(0));
          ++signed_codes;
        }
      }
      check_equal(status, CSERDE_DONE);
      check_equal(signed_codes, (size_t)3u);
    }

    it("encodes schema bound dynamic values through the same graph for all array states") {
      static const char *const json[] = {
          "{\"ready\":[true,false],\"codes\":[0,1,0],\"ratios\":[1.5,-0.5],\"permissions\":[3,0]}",
          "{\"ready\":[true,false],\"codes\":[0,1,0],\"ratios\":[1.5,-0.5],\"samples\":null,\"permissions\":[3,0]}",
          "{\"ready\":[true,false],\"codes\":[0,1,0],\"ratios\":[1.5,-0.5],\"samples\":[1,65535,2],\"permissions\":[3,0]}"};
      unsigned char expected[] = {
          0u, 0u, 1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u,
          0u, 0u, 0xc0u, 0x3fu, 0u, 0u, 0u, 0xbfu,
          0u, 0u, 0u, 0u, 0u, 0u, 3u, 0u};
      enum { SAMPLES_OFFSET = 18u };
      const DataBindBinaryLayoutPlan *plan;
      check_equal(data_bind_create_from_text(schema_data, strlen(schema_data), &codec, NULL), DATA_BIND_OK);
      check_equal(databind_binary_execution_graph_build(
                      contract, &format, ARRAY_SCALARS_TYPE, &graph, NULL), DATABIND_BINARY_LAYOUT_OK);
      plan = databind_binary_execution_graph_root(graph);
      for (size_t state = 0u; state < sizeof(json) / sizeof(json[0]); ++state) {
        cserde_token token;
        cserde_status status;
        expected[0] = state != 0u;
        expected[1] = state == 1u;
        if (state == 2u) {
          static const unsigned char samples[] = {1u, 0u, 0xffu, 0xffu, 2u, 0u};
          memcpy(expected + SAMPLES_OFFSET, samples, sizeof(samples));
        }
        sink = (LayoutGraphSink){0};
        check_equal(data_bind_parse_json(codec, ARRAY_SCALARS_TYPE, json[state], strlen(json[state]),
                                        &value, NULL), DATA_BIND_OK);
        check_equal(data_bind_internal_value_reader_open_typed(value, NULL, &value_reader, NULL), DATA_BIND_OK);
        check_equal(data_bind_binary_writer_open(
                        plan, layout_graph_write, &sink, 0u, &writer, &writer_owner, NULL), DATA_BIND_OK);
        while ((status = cserde_reader_next(value_reader, &token)) == CSERDE_OK)
          check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
        check_equal(status, CSERDE_DONE);
        check_equal(cserde_writer_finish(writer), CSERDE_OK);
        check_equal(sink.calls, (size_t)1u);
        check_equal(sink.length, sizeof(expected));
        check_equal(sink.bytes, expected, sizeof(expected));
        data_bind_value_reader_close(value_reader);
        value_reader = NULL;
        data_bind_value_free(value);
        value = NULL;
        check_equal(data_bind_binary_writer_close(writer, writer_owner, NULL), DATA_BIND_OK);
        writer = NULL;
        writer_owner = NULL;
      }
    }

    it("owns nested record array plans and preserves independent wire extents") {
      const DataBindBinaryLayoutPlan *plan;
      check_equal(databind_binary_execution_graph_build(
                      contract, &format, ARRAY_RECORDS_TYPE, &graph, NULL),
                  DATABIND_BINARY_LAYOUT_OK);
      databind_binary_format_plan_destroy(&format);
      idl_contract_destroy(contract);
      contract = NULL;
      plan = databind_binary_execution_graph_root(graph);
      check_equal(data_bind_binary_layout_plan_validate(plan, NULL), DATA_BIND_OK);
      check_equal(plan->child_plans[0]->type_name, "ArrayEntry");
      check_equal(plan->array_plans[0]->element_extent, (size_t)5u);
      check_equal(plan->child_plans[0]->fixed_block_size, (size_t)5u);
      check_equal(plan->child_plans[1]->type_name, "ArrayBlock");
      check_equal(plan->child_plans[1]->array_plans[0]->element_scalar_bits, 16u);
      check_equal(plan->fields[0].flags,
                  (unsigned)(DATA_BIND_BINARY_FIELD_OPTIONAL | DATA_BIND_BINARY_FIELD_NULLABLE));
    }

    it("rejects partial child graphs and allocation budgets before publishing a plan") {
      databind_binary_layout_diagnostic diagnostic = {0};
      databind_binary_format_type_plan excessive_type =
          *databind_binary_format_plan_find_type(&format, ARRAY_SCALARS_TYPE);
      databind_binary_format_plan excessive = {&excessive_type, 1u};
      check_equal(databind_binary_execution_graph_build(
                      contract, &format, "UnsupportedOverlayArray", &graph, &diagnostic),
                  DATABIND_BINARY_LAYOUT_INVALID_SCHEMA);
      check_null(graph);
      check_contains(diagnostic.text, "extent");
      excessive_type.field_count = DATABIND_BINARY_LOWERING_MAX_FIELDS + 1u;
      check_equal(databind_binary_execution_graph_build(
                      contract, &excessive, ARRAY_SCALARS_TYPE, &graph, &diagnostic),
                  DATABIND_BINARY_LAYOUT_INVALID_SCHEMA);
      check_null(graph);
      check_contains(diagnostic.text, "field budget");
      excessive = format;
      excessive.type_count = DATABIND_BINARY_LOWERING_MAX_TYPES + 1u;
      check_equal(databind_binary_execution_graph_build(
                      contract, &excessive, ARRAY_SCALARS_TYPE, &graph, &diagnostic),
                  DATABIND_BINARY_LAYOUT_INVALID_SCHEMA);
      check_null(graph);
      check_equal(databind_binary_execution_graph_build(
                      contract, &format, ARRAY_RECORDS_TYPE, &graph, &diagnostic),
                  DATABIND_BINARY_LAYOUT_OK);
      check_not_null(graph);
    }
  }

  group("counted collection wire layout") {
    static Node *root;
    static IdlContract *contract;
    static char *schema_data;
    static databind_binary_format_plan format;
    static databind_binary_type_layout layout;
    static databind_binary_execution_graph *graph;

    before_each() {
      tbe_error_t error;
      root = NULL;
      contract = NULL;
      schema_data = NULL;
      format = (databind_binary_format_plan){0};
      layout = (databind_binary_type_layout){0};
      graph = NULL;
      check_equal(databind_compiler_parse_contract_file(
                      BINARY_COUNTED_SCHEMA, &root, &contract, &schema_data), 0);
      tbe_error_init(&error);
      check(databind_binary_format_plan_build(contract, root, &format, &error), "%s", error.message);
    }
    after_each() {
      databind_binary_execution_graph_destroy(graph);
      databind_binary_layout_destroy(&layout);
      databind_binary_format_plan_destroy(&format);
      idl_contract_destroy(contract);
      node_free(root);
      free(schema_data);
    }

    it("keeps collection counts and following fixed values in declaration order") {
      check_equal(databind_binary_layout_build(contract, &format, "Counted", &layout, NULL),
                  DATABIND_BINARY_LAYOUT_OK);
      check_true(layout.wire_big_endian);
      check_equal(layout.fixed_block_size, sizeof(uint16_t));
      check_equal(layout.field_count, (size_t)8u);
      check_equal(layout.fields[0].kind, DATABIND_BINARY_FIELD_FIXED);
      check_equal(layout.fields[0].wire_offset, (size_t)0u);
      check_equal(layout.fields[1].kind, DATABIND_BINARY_FIELD_COUNTED);
      check_equal(layout.fields[1].tail_prefix_bytes, sizeof(uint32_t));
      check_equal(layout.fields[1].element_scalar_kind, DATABIND_BINARY_SCALAR_UINT);
      check_equal(layout.fields[1].element_scalar_bits, 32u);
      check_equal(layout.fields[1].element_extent, sizeof(uint32_t));
      check_equal(layout.fields[2].kind, DATABIND_BINARY_FIELD_CURSOR_FIXED);
      check_equal(layout.fields[2].wire_offset, (size_t)0u);
      check_equal(layout.fields[2].wire_extent, sizeof(int16_t));
      check_equal(layout.fields[3].element_scalar_kind, DATABIND_BINARY_SCALAR_SINT);
      check_equal(layout.fields[3].element_scalar_bits, 8u);
      check_equal(layout.fields[4].kind, DATABIND_BINARY_FIELD_CURSOR_FIXED);
      check_equal(layout.fields[4].array_count, (size_t)2u);
      check_equal(layout.fields[4].element_extent, sizeof(uint16_t));
      check_equal(layout.fields[5].key_scalar_kind, DATABIND_BINARY_SCALAR_STRING);
      check_equal(layout.fields[5].element_scalar_kind, DATABIND_BINARY_SCALAR_STRING);
      check_equal(layout.fields[5].element_extent, (size_t)0u);
      check_equal(layout.fields[6].kind, DATABIND_BINARY_FIELD_CURSOR_FIXED);
      check_equal(layout.fields[6].scalar_kind, DATABIND_BINARY_SCALAR_BOOL);
      check_equal(layout.fields[7].kind, DATABIND_BINARY_FIELD_VAR_DATA);
    }

    it("uses canonical enum and integer element widths independently of the u32 prefix") {
      check_equal(databind_binary_layout_build(contract, &format, "CountedDomains", &layout, NULL),
                  DATABIND_BINARY_LAYOUT_OK);
      check_equal(layout.fixed_block_size, (size_t)0u);
      check_equal(layout.fields[0].element_scalar_kind, DATABIND_BINARY_SCALAR_ENUM_SINT);
      check_equal(layout.fields[0].element_scalar_bits, 16u);
      check_equal(layout.fields[0].element_extent, sizeof(int16_t));
      check_equal(layout.fields[1].key_scalar_kind, DATABIND_BINARY_SCALAR_STRING);
      check_equal(layout.fields[1].element_scalar_kind, DATABIND_BINARY_SCALAR_UINT);
      check_equal(layout.fields[1].element_scalar_bits, 64u);
      check_equal(layout.fields[1].element_extent, sizeof(uint64_t));
      check_equal(layout.fields[2].key_scalar_kind, DATABIND_BINARY_SCALAR_NONE);
      check_equal(layout.fields[2].element_scalar_kind, DATABIND_BINARY_SCALAR_STRING);
      check_equal(layout.fields[2].element_scalar_bits, 0u);
      check_equal(layout.fields[2].element_extent, (size_t)0u);
    }

    it("retains independent state bitmaps before a positional collection") {
      check_equal(databind_binary_layout_build(contract, &format, "CountedState", &layout, NULL),
                  DATABIND_BINARY_LAYOUT_OK);
      check_equal(layout.fixed_block_size, layout.presence_size + layout.null_size);
      check_equal(layout.presence_size, (size_t)1u);
      check_equal(layout.null_offset, layout.presence_size);
      check_equal(layout.null_size, (size_t)1u);
      check_equal(layout.fields[0].flags,
                  (unsigned)(DATABIND_BINARY_FIELD_OPTIONAL | DATABIND_BINARY_FIELD_NULLABLE));
      check_equal(layout.fields[0].element_scalar_bits, 16u);
      check_equal(layout.fields[1].kind, DATABIND_BINARY_FIELD_CURSOR_FIXED);
      check_equal(layout.fields[1].scalar_bits, 32u);
    }

    it("executes counted fixed records and their trailing scalar with exact big-endian wire") {
      databind_binary_layout_diagnostic diagnostic = {0};
      static const unsigned char expected[] = {
          0, 0, 0, 2, 0x12, 0x34, 7, 0xab, 0xcd, 8, 0x43, 0x21};
      static const cserde_token tokens[] = {
          {.kind = CSERDE_MAP_BEGIN}, LAYOUT_GRAPH_KEY("entries"),
          {.kind = CSERDE_ARRAY_BEGIN}, {.kind = CSERDE_MAP_BEGIN},
          LAYOUT_GRAPH_KEY("id"), {.kind = CSERDE_UINT, .value.uint = 0x1234u},
          LAYOUT_GRAPH_KEY("tag"), {.kind = CSERDE_UINT, .value.uint = 7u}, {.kind = CSERDE_MAP_END},
          {.kind = CSERDE_MAP_BEGIN}, LAYOUT_GRAPH_KEY("id"),
          {.kind = CSERDE_UINT, .value.uint = 0xabcdu}, LAYOUT_GRAPH_KEY("tag"),
          {.kind = CSERDE_UINT, .value.uint = 8u}, {.kind = CSERDE_MAP_END},
          {.kind = CSERDE_ARRAY_END}, LAYOUT_GRAPH_KEY("last"),
          {.kind = CSERDE_UINT, .value.uint = 0x4321u}, {.kind = CSERDE_MAP_END}};
      LayoutGraphSink sink = {0};
      cserde_writer *writer = NULL;
      cserde_reader *reader = NULL;
      void *writer_owner = NULL, *reader_owner = NULL;
      const DataBindBinaryLayoutPlan *plan;
      cserde_token token;
      size_t i;
      check_equal(databind_binary_layout_build(contract, &format, "CountedRecords", &layout, NULL),
                  DATABIND_BINARY_LAYOUT_OK);
      check_equal(layout.fields[0].kind, DATABIND_BINARY_FIELD_COUNTED);
      check_equal(layout.fields[0].element_scalar_kind, DATABIND_BINARY_SCALAR_NONE);
      check_equal(layout.fields[0].element_extent, (size_t)0u);
      check_equal(databind_binary_execution_graph_build(
                      contract, &format, "CountedRecords", &graph, &diagnostic),
                  DATABIND_BINARY_LAYOUT_OK);
      check_not_null(graph);
      plan = databind_binary_execution_graph_root(graph);
      check_equal(data_bind_binary_writer_open(plan, layout_graph_write, &sink, 0u,
                                               &writer, &writer_owner, NULL), DATA_BIND_OK);
      for (i = 0u; i < sizeof(tokens) / sizeof(tokens[0]); ++i)
        check_equal(cserde_writer_write(writer, &tokens[i]), CSERDE_OK);
      check_equal(data_bind_binary_writer_close(writer, writer_owner, NULL), DATA_BIND_OK);
      check_equal(sink.calls, (size_t)1u);
      check_equal(sink.length, sizeof(expected));
      check_equal(sink.bytes, expected, sizeof(expected));
      check_equal(data_bind_binary_reader_open(plan, expected, sizeof(expected), 0u,
                                               &reader, &reader_owner, NULL), DATA_BIND_OK);
      for (i = 0u; i < sizeof(tokens) / sizeof(tokens[0]); ++i) {
        check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
        check_equal(token.kind, tokens[i].kind);
        if (token.kind == CSERDE_UINT) check_equal(token.value.uint, tokens[i].value.uint);
        if (token.kind == CSERDE_STRING) {
          check_equal(token.value.slice.size, tokens[i].value.slice.size);
          check_equal(token.value.slice.data, tokens[i].value.slice.data, token.value.slice.size);
        }
      }
      check_equal(cserde_reader_next(reader, &token), CSERDE_DONE);
      data_bind_binary_reader_close(reader, reader_owner);
    }

    it("rejects count width element width and absolute offset drift") {
      check_equal(databind_binary_layout_build(contract, &format, "Counted", &layout, NULL),
                  DATABIND_BINARY_LAYOUT_OK);
      const databind_binary_field_layout original = layout.fields[1];
      layout.fields[1].tail_prefix_bytes = sizeof(uint16_t);
      check_equal(databind_binary_layout_validate(&layout, NULL), DATABIND_BINARY_LAYOUT_INVALID_SCHEMA);
      layout.fields[1] = original;
      layout.fields[1].element_scalar_bits = 16u;
      check_equal(databind_binary_layout_validate(&layout, NULL), DATABIND_BINARY_LAYOUT_INVALID_SCHEMA);
      layout.fields[1] = original;
      layout.fields[2].kind = DATABIND_BINARY_FIELD_FIXED;
      check_equal(databind_binary_layout_validate(&layout, NULL), DATABIND_BINARY_LAYOUT_INVALID_SCHEMA);
      layout.fields[2].kind = DATABIND_BINARY_FIELD_CURSOR_FIXED;
      layout.fields[2].wire_offset = 1u;
      check_equal(databind_binary_layout_validate(&layout, NULL), DATABIND_BINARY_LAYOUT_INVALID_SCHEMA);
      layout.fields[2].wire_offset = 0u;
      check_equal(databind_binary_layout_validate(&layout, NULL), DATABIND_BINARY_LAYOUT_OK);
    }
  }

  it("keeps fixed array element semantics independent from the total wire extent") {
    Node *root = NULL;
    IdlContract *contract = NULL;
    char *schema_data = NULL;
    databind_binary_type_layout layout = {0};
    databind_binary_format_plan format_plan = {0};
    tbe_error_t format_error;
    const databind_binary_scalar_kind kinds[] = {
        DATABIND_BINARY_SCALAR_BOOL, DATABIND_BINARY_SCALAR_ENUM_SINT,
        DATABIND_BINARY_SCALAR_FLOAT, DATABIND_BINARY_SCALAR_UINT,
        DATABIND_BINARY_SCALAR_ENUM_UINT};
    const size_t counts[] = {2u, 3u, 2u, 3u, 2u};
    const unsigned widths[] = {8u, 16u, 32u, 16u, 8u};
    size_t i;
    check_equal(databind_compiler_parse_contract_file(
                    BINARY_ARRAY_SCHEMA, &root, &contract, &schema_data), 0);
    check_not_null(contract);
    if (contract != NULL) {
      tbe_error_init(&format_error);
      check(databind_binary_format_plan_build(contract, root, &format_plan, &format_error));
      check_equal(databind_binary_layout_build(contract, &format_plan, "ArrayScalars",
                                               &layout, NULL), DATABIND_BINARY_LAYOUT_OK);
      check_equal(layout.field_count, sizeof(counts) / sizeof(counts[0]));
      if (layout.field_count == sizeof(counts) / sizeof(counts[0])) {
        for (i = 0u; i < layout.field_count; ++i) {
          const databind_binary_field_layout *field = &layout.fields[i];
          check_equal(field->scalar_kind, DATABIND_BINARY_SCALAR_NONE);
          check_equal(field->scalar_bits, 0u);
          check_equal(field->array_count, counts[i]);
          check_equal(field->element_scalar_kind, kinds[i]);
          check_equal(field->element_scalar_bits, widths[i]);
          check_equal(field->element_extent, (size_t)(widths[i] / 8u));
          check_equal(field->wire_extent, counts[i] * (widths[i] / 8u));
        }
        check(layout.fields[3].flags & DATABIND_BINARY_FIELD_OPTIONAL);
        check(layout.fields[3].flags & DATABIND_BINARY_FIELD_NULLABLE);
      }
      databind_binary_layout_destroy(&layout);
      check_equal(databind_binary_layout_build(contract, &format_plan, "ArrayRecords",
                                               &layout, NULL), DATABIND_BINARY_LAYOUT_OK);
      check_equal(layout.field_count, (size_t)3u);
      if (layout.field_count == 3u) {
        check_equal(layout.fields[0].array_count, (size_t)2u);
        check_equal(layout.fields[0].element_extent, (size_t)5u);
        check_equal(layout.fields[0].element_scalar_kind, DATABIND_BINARY_SCALAR_NONE);
        check_equal(layout.fields[1].element_extent, (size_t)4u);
        check_equal(layout.fields[2].array_count, (size_t)0u);
      }
    }
    databind_binary_layout_destroy(&layout);
    databind_binary_format_plan_destroy(&format_plan);
    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }

  it("rejects inconsistent fixed array counts, widths and representation tails") {
    databind_binary_field_layout field = {
        .field_id = "values", .kind = DATABIND_BINARY_FIELD_FIXED,
        .wire_extent = 6u, .array_count = 3u, .element_extent = 2u,
        .element_scalar_kind = DATABIND_BINARY_SCALAR_UINT, .element_scalar_bits = 16u};
    databind_binary_type_layout layout = {
        .type_id = "Array", .fixed_block_size = 6u, .fields = &field, .field_count = 1u};
    const databind_binary_field_layout valid = field;
    size_t i;
    enum { INVALID_ARRAY_CASES = 8u };
    check_equal(databind_binary_layout_validate(&layout, NULL), DATABIND_BINARY_LAYOUT_OK);
    for (i = 0u; i < INVALID_ARRAY_CASES; ++i) {
      field = valid;
      switch (i) {
      case 0u: field.array_count = SIZE_MAX; break;
      case 1u: field.array_count = 0u; break;
      case 2u: field.element_extent = 0u; break;
      case 3u: field.element_scalar_bits = 32u; break;
      case 4u: field.element_scalar_kind = DATABIND_BINARY_SCALAR_STRING; break;
      case 5u: field.scalar_kind = DATABIND_BINARY_SCALAR_UINT; field.scalar_bits = 16u; break;
      case 6u: field.kind = DATABIND_BINARY_FIELD_GROUP; break;
      case 7u: field.element_scalar_kind = (databind_binary_scalar_kind)-1; break;
      }
      check_equal(databind_binary_layout_validate(&layout, NULL),
                  DATABIND_BINARY_LAYOUT_INVALID_SCHEMA);
    }
  }

  it("derives fixed scalar token representation from canonical CMeta semantics") {
    Node *root = NULL;
    IdlContract *contract = NULL;
    char *schema_data = NULL;
    databind_binary_type_layout layout = {0};
    databind_binary_layout_diagnostic diagnostic = {0};
    databind_binary_format_plan format_plan = {0};
    tbe_error_t format_error;

    check_equal(
        databind_compiler_parse_contract_file(
            BINARY_SCALAR_SCHEMA, &root, &contract, &schema_data), 0);
    check_not_null(root);
    check_not_null(schema_data);
    if (root == NULL || schema_data == NULL) {
      node_free(root);
      free(schema_data);
      return;
    }

    tbe_error_init(&format_error);
    check(databind_binary_format_plan_build(
        contract, root, &format_plan, &format_error));
    check_equal(
        databind_binary_layout_build(
            contract, &format_plan, "Scalars", &layout, &diagnostic),
        DATABIND_BINARY_LAYOUT_OK);
    check_equal(layout.field_count, (size_t)10u);

    check_equal(layout.fields[0].scalar_kind, DATABIND_BINARY_SCALAR_BOOL);
    check_equal(layout.fields[0].scalar_bits, 8u);

    check_equal(layout.fields[1].scalar_kind, DATABIND_BINARY_SCALAR_SINT);
    check_equal(layout.fields[1].scalar_bits, 8u);

    check_equal(layout.fields[2].scalar_kind, DATABIND_BINARY_SCALAR_UINT);
    check_equal(layout.fields[2].scalar_bits, 16u);

    check_equal(layout.fields[3].scalar_kind, DATABIND_BINARY_SCALAR_SINT);
    check_equal(layout.fields[3].scalar_bits, 32u);

    check_equal(layout.fields[4].scalar_kind, DATABIND_BINARY_SCALAR_UINT);
    check_equal(layout.fields[4].scalar_bits, 64u);

    check_equal(layout.fields[5].scalar_kind, DATABIND_BINARY_SCALAR_FLOAT);
    check_equal(layout.fields[5].scalar_bits, 32u);

    check_equal(layout.fields[6].scalar_kind, DATABIND_BINARY_SCALAR_FLOAT);
    check_equal(layout.fields[6].scalar_bits, 64u);

    check_equal(layout.fields[7].scalar_kind,
                DATABIND_BINARY_SCALAR_ENUM_SINT);
    check_equal(layout.fields[7].scalar_bits, 16u);

    check_equal(layout.fields[8].scalar_kind,
                DATABIND_BINARY_SCALAR_ENUM_UINT);
    check_equal(layout.fields[8].scalar_bits, 8u);

    check_equal(layout.fields[9].scalar_kind, DATABIND_BINARY_SCALAR_UINT);
    check_equal(layout.fields[9].scalar_bits, 32u);
    check((layout.fields[9].flags & DATABIND_BINARY_FIELD_OPTIONAL) != 0u);
    check((layout.fields[9].flags & DATABIND_BINARY_FIELD_NULLABLE) != 0u);

    databind_binary_layout_destroy(&layout);
    databind_binary_format_plan_destroy(&format_plan);
    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }


  it("builds LoginMessage wire layout from canonical compiler IR") {
    Node *root = NULL;
    IdlContract *contract = NULL;
    char *schema_data = NULL;
    databind_binary_type_layout layout = {0};
    databind_binary_layout_diagnostic diagnostic = {0};
    databind_binary_format_plan format_plan = {0};
    tbe_error_t format_error;

    check_equal(
        databind_compiler_parse_contract_file(
            SCHEMA_EXAMPLE_FILE, &root, &contract, &schema_data), 0);
    check_not_null(root);

    tbe_error_init(&format_error);
    check(databind_binary_format_plan_build(
        contract, root, &format_plan, &format_error));
    check_equal(
        databind_binary_layout_build(
            contract, &format_plan, "LoginMessage", &layout, &diagnostic),
        DATABIND_BINARY_LAYOUT_OK);
    check_equal(strcmp(layout.type_id, "LoginMessage"), 0);
    check_equal(layout.wire_big_endian, 0);
    check_equal(layout.fixed_block_size, (size_t)29u);
    check_equal(layout.presence_size, (size_t)0u);
    check_equal(layout.null_size, (size_t)0u);
    check_equal(layout.field_count, (size_t)3u);

    check_equal(strcmp(layout.fields[0].field_id, "header"), 0);
    check_equal(layout.fields[0].kind, DATABIND_BINARY_FIELD_FIXED);
    check_equal(layout.fields[0].wire_offset, (size_t)0u);
    check_equal(layout.fields[0].wire_extent, (size_t)13u);

    check_equal(strcmp(layout.fields[1].field_id, "pass_hash"), 0);
    check_equal(layout.fields[1].kind, DATABIND_BINARY_FIELD_FIXED);
    check_equal(layout.fields[1].wire_offset, (size_t)13u);
    check_equal(layout.fields[1].wire_extent, (size_t)16u);
    check_equal(layout.fields[1].scalar_kind, DATABIND_BINARY_SCALAR_BYTES);
    check_equal(layout.fields[1].scalar_bits, 0u);

    check_equal(strcmp(layout.fields[2].field_id, "username"), 0);
    check_equal(layout.fields[2].kind, DATABIND_BINARY_FIELD_VAR_DATA);
    check_equal(layout.fields[2].scalar_kind, DATABIND_BINARY_SCALAR_STRING);
    check_equal(layout.fields[2].scalar_bits, 0u);
    check_equal(layout.fields[2].tail_prefix_bytes, (size_t)4u);

    databind_binary_layout_destroy(&layout);
    databind_binary_format_plan_destroy(&format_plan);
    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }

  it("derives VAR_DATA string and bytes semantics from canonical CMeta") {
    Node *root = NULL;
    IdlContract *contract = NULL;
    char *schema_data = NULL;
    databind_binary_type_layout layout = {0};
    databind_binary_layout_diagnostic diagnostic = {0};
    databind_binary_format_plan format_plan = {0};
    tbe_error_t format_error;

    check_equal(
        databind_compiler_parse_contract_file(
            BINARY_VAR_DATA_SCHEMA, &root, &contract, &schema_data), 0);
    check_not_null(root);
    check_not_null(contract);
    if (root == NULL || contract == NULL) {
      node_free(root);
      idl_contract_destroy(contract);
      free(schema_data);
      return;
    }

    tbe_error_init(&format_error);
    check(databind_binary_format_plan_build(
        contract, root, &format_plan, &format_error));
    check_equal(
        databind_binary_layout_build(
            contract, &format_plan, "TelemetryEvent", &layout, &diagnostic),
        DATABIND_BINARY_LAYOUT_OK);
    check_equal(layout.field_count, (size_t)3u);

    check_equal(layout.fields[0].kind, DATABIND_BINARY_FIELD_FIXED);
    check_equal(layout.fields[0].scalar_kind, DATABIND_BINARY_SCALAR_UINT);
    check_equal(layout.fields[0].scalar_bits, 32u);

    check_equal(layout.fields[1].kind, DATABIND_BINARY_FIELD_VAR_DATA);
    check_equal(layout.fields[1].scalar_kind, DATABIND_BINARY_SCALAR_STRING);
    check_equal(layout.fields[1].scalar_bits, 0u);
    check_equal(layout.fields[1].tail_prefix_bytes, (size_t)4u);

    check_equal(layout.fields[2].kind, DATABIND_BINARY_FIELD_VAR_DATA);
    check_equal(layout.fields[2].scalar_kind, DATABIND_BINARY_SCALAR_BYTES);
    check_equal(layout.fields[2].scalar_bits, 0u);
    check_equal(layout.fields[2].tail_prefix_bytes, (size_t)4u);
    check(layout.fields[2].flags & DATABIND_BINARY_FIELD_OPTIONAL);
    check(layout.fields[2].flags & DATABIND_BINARY_FIELD_NULLABLE);

    databind_binary_layout_destroy(&layout);
    databind_binary_format_plan_destroy(&format_plan);
    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }

  it("rejects fixed wire overlap without consulting native layout") {
    databind_binary_field_layout fields[2] = {
        {.field_id = "a",
         .kind = DATABIND_BINARY_FIELD_FIXED,
         .wire_offset = 0u,
         .wire_extent = 4u},
        {.field_id = "b",
         .kind = DATABIND_BINARY_FIELD_FIXED,
         .wire_offset = 2u,
         .wire_extent = 4u}};
    databind_binary_type_layout layout = {
        .type_id = "Overlap",
        .fixed_block_size = 8u,
        .fields = fields,
        .field_count = 2u};
    databind_binary_layout_diagnostic diagnostic = {0};

    check_equal(
        databind_binary_layout_validate(&layout, &diagnostic),
        DATABIND_BINARY_LAYOUT_INVALID_SCHEMA);
    check_equal(strcmp(diagnostic.field, "b"), 0);
  }

  it("keeps Binary presence/null state independent from host offsets") {
    databind_binary_field_layout field = {
        .field_id = "value",
        .kind = DATABIND_BINARY_FIELD_FIXED,
        .wire_offset = 2u,
        .wire_extent = 2u,
        .optional_bit = 0u,
        .nullable_bit = 0u,
        .flags = DATABIND_BINARY_FIELD_OPTIONAL |
                 DATABIND_BINARY_FIELD_NULLABLE};
    databind_binary_type_layout layout = {
        .type_id = "State",
        .fixed_block_size = 4u,
        .presence_offset = 0u,
        .presence_size = 1u,
        .null_offset = 1u,
        .null_size = 1u,
        .fields = &field,
        .field_count = 1u};

    check_equal(
        databind_binary_layout_validate(&layout, NULL),
        DATABIND_BINARY_LAYOUT_OK);

    field.wire_offset = 1u;
    check_equal(
        databind_binary_layout_validate(&layout, NULL),
        DATABIND_BINARY_LAYOUT_INVALID_SCHEMA);
  }

  it("rejects scalar wire extents that disagree with canonical token width") {
    databind_binary_field_layout field = {
        .field_id = "value",
        .kind = DATABIND_BINARY_FIELD_FIXED,
        .wire_offset = 0u,
        .wire_extent = 2u,
        .scalar_kind = DATABIND_BINARY_SCALAR_UINT,
        .scalar_bits = 32u};
    databind_binary_type_layout layout = {
        .type_id = "ScalarMismatch",
        .fixed_block_size = 2u,
        .fields = &field,
        .field_count = 1u};

    check_equal(
        databind_binary_layout_validate(&layout, NULL),
        DATABIND_BINARY_LAYOUT_INVALID_SCHEMA);

    field.wire_extent = 4u;
    layout.fixed_block_size = 4u;
    check_equal(
        databind_binary_layout_validate(&layout, NULL),
        DATABIND_BINARY_LAYOUT_OK);
  }

  it("rejects fixed fields after Binary tail fields") {
    databind_binary_field_layout fields[2] = {
        {.field_id = "payload",
         .kind = DATABIND_BINARY_FIELD_VAR_DATA,
         .scalar_kind = DATABIND_BINARY_SCALAR_BYTES,
         .tail_prefix_bytes = 4u},
        {.field_id = "code",
         .kind = DATABIND_BINARY_FIELD_FIXED,
         .wire_offset = 0u,
         .wire_extent = 1u}};
    databind_binary_type_layout layout = {
        .type_id = "Ordering",
        .fixed_block_size = 1u,
        .fields = fields,
        .field_count = 2u};

    check_equal(
        databind_binary_layout_validate(&layout, NULL),
        DATABIND_BINARY_LAYOUT_INVALID_SCHEMA);
  }
}
