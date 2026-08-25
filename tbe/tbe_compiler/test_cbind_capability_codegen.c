#include "tbe_cbind_capability.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static int emit(FILE *file, const char *format, ...) {
  int result;
  va_list args;

  va_start(args, format);
  result = vfprintf(file, format, args);
  va_end(args);
  return result >= 0;
}

static int emit_schema(FILE *schema) {
  size_t spelling_count = tbe_cbind_capability_spelling_count();
  size_t index;

  if (!emit(schema, "schema CBindCapabilityMatrix [id(75), version(1)];\n\n")) return 0;
  for (index = 0u; index < spelling_count; ++index) {
    const char *spelling = tbe_cbind_capability_spelling_at(index);
    const tbe_cbind_capability *capability = tbe_cbind_capability_find(spelling);
    if (!spelling || !capability) return 0;
    if (capability->kind == TBE_CBIND_SCALAR_INTEGER &&
        !emit(schema, "enum MatrixEnum_%zu <%s> { Zero = 0; One = 1; }\n",
              index, spelling))
      return 0;
  }
  if (!emit(schema, "\nmessage CBindCapabilityMatrix {\n")) return 0;
  for (index = 0u; index < spelling_count; ++index) {
    const char *spelling = tbe_cbind_capability_spelling_at(index);
    if (!spelling || !emit(schema, "    %s scalar_%zu;\n", spelling, index)) return 0;
  }
  for (index = 0u; index < spelling_count; ++index) {
    const char *spelling = tbe_cbind_capability_spelling_at(index);
    const tbe_cbind_capability *capability = tbe_cbind_capability_find(spelling);
    if (!spelling || !capability) return 0;
    if (capability->kind == TBE_CBIND_SCALAR_INTEGER &&
        !emit(schema, "    MatrixEnum_%zu enum_%zu;\n", index, index))
      return 0;
  }
  return emit(schema, "}\n");
}

static int emit_c_consumer(FILE *consumer) {
  size_t spelling_count = tbe_cbind_capability_spelling_count();
  size_t field_index = 0u;
  size_t index;

  if (!emit(consumer,
            "#include \"cbind_capability_matrix.h\"\n"
            "#include <turbo_cmeta_data.h>\n"
            "#include <stdbool.h>\n"
            "#include <stdio.h>\n"
            "#include <stdlib.h>\n"
            "#include <string.h>\n\n"))
    return 0;
  for (index = 0u; index < spelling_count; ++index) {
    const char *spelling = tbe_cbind_capability_spelling_at(index);
    const tbe_cbind_capability *capability = tbe_cbind_capability_find(spelling);
    if (!spelling || !capability ||
        !emit(consumer,
              "_Static_assert(_Generic(((CBindCapabilityMatrix_t *)0)->scalar_%zu, "
              "%s: 1, default: 0), \"scalar spelling %s must use provider C storage\");\n",
              index, capability->c_storage, spelling))
      return 0;
    if (capability->kind == TBE_CBIND_SCALAR_INTEGER &&
        !emit(consumer,
              "_Static_assert(_Generic((MatrixEnum_%zu_t)0, %s: 1, default: 0), "
              "\"enum spelling %s must use provider C storage\");\n",
              index, capability->c_storage, spelling))
      return 0;
  }
  if (!emit(consumer,
            "#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "
            "\"matrix check failed: %%s\\n\", #expression); return EXIT_FAILURE; } } while "
            "(0)\n\n"
            "\nint main(void) {\n"
            "  const cmeta_data_desc *descriptor = CBindCapabilityMatrix_cbind_data();\n"
            "  const cmeta_data_struct_shape *shape;\n"
            "  CHECK(descriptor != NULL && cmeta_data_desc_valid(descriptor));\n"
            "  shape = (const cmeta_data_struct_shape *)descriptor->shape;\n"
            "  CHECK(shape != NULL);\n"))
    return 0;
  for (index = 0u; index < spelling_count; ++index, ++field_index) {
    const char *spelling = tbe_cbind_capability_spelling_at(index);
    const tbe_cbind_capability *capability = tbe_cbind_capability_find(spelling);
    if (!spelling || !capability ||
        !emit(consumer,
              "  CHECK(shape->layout->fields[%zu].type != NULL);\n"
              "  CHECK(cmeta_type_equal(shape->layout->fields[%zu].type, &%s));\n"
              "  CHECK(shape->layout->fields[%zu].type->size == sizeof(%s));\n"
              "  CHECK(shape->layout->fields[%zu].type->align == _Alignof(%s));\n",
              field_index, field_index, capability->cmeta_type_symbol, field_index,
              capability->c_storage, field_index, capability->c_storage))
      return 0;
    if (capability->is_uuid) {
      if (!emit(consumer,
                "  CHECK(shape->layout->fields[%zu].type == &%s);\n"
                "  CHECK(shape->fields[%zu].value == &%s);\n",
                field_index, capability->cmeta_type_symbol, field_index,
                capability->cmeta_data_symbol))
        return 0;
    } else if (capability->cmeta_data_symbol) {
      if (!emit(consumer,
                "  CHECK(shape->fields[%zu].value != NULL && "
                "cmeta_data_desc_valid(shape->fields[%zu].value));\n"
                "  CHECK(strcmp(shape->fields[%zu].value->stable_id, "
                "(&%s)->stable_id) == 0);\n"
                "  CHECK(shape->fields[%zu].value->storage_type == "
                "shape->layout->fields[%zu].type);\n",
                field_index, field_index, field_index, capability->cmeta_data_symbol,
                field_index, field_index))
        return 0;
    } else if (!emit(consumer,
                     "  CHECK(shape->fields[%zu].value != NULL && "
                     "shape->fields[%zu].value->kind == CMETA_DATA_STRING);\n",
                     field_index, field_index)) {
      return 0;
    }
  }
  for (index = 0u; index < spelling_count; ++index) {
    const char *spelling = tbe_cbind_capability_spelling_at(index);
    const tbe_cbind_capability *capability = tbe_cbind_capability_find(spelling);
    if (!spelling || !capability) return 0;
    if (capability->kind == TBE_CBIND_SCALAR_INTEGER) {
      if (!emit(consumer,
                "  CHECK(cmeta_data_desc_valid(MatrixEnum_%zu_cbind_data()));\n"
                "  CHECK(cmeta_type_equal(MatrixEnum_%zu_cbind_data()->storage_type, "
                "&%s));\n"
                "  CHECK(MatrixEnum_%zu_cbind_data()->storage_type->size == sizeof(%s));\n"
                "  CHECK(MatrixEnum_%zu_cbind_data()->storage_type->align == "
                "_Alignof(%s));\n"
                "  CHECK(shape->layout->fields[%zu].type == "
                "MatrixEnum_%zu_cbind_data()->storage_type);\n"
                "  CHECK(shape->fields[%zu].value == MatrixEnum_%zu_cbind_data());\n",
                index, index, capability->cmeta_type_symbol, index, capability->c_storage,
                index, capability->c_storage, field_index, index, field_index, index))
        return 0;
      ++field_index;
    }
  }
  return emit(consumer, "  return EXIT_SUCCESS;\n}\n");
}

static int emit_cpp_consumer(FILE *consumer) {
  size_t spelling_count = tbe_cbind_capability_spelling_count();
  size_t index;

  if (!emit(consumer,
            "#include \"cbind_capability_matrix.h\"\n"
            "#include <cstdlib>\n"
            "#include <type_traits>\n\n"))
    return 0;
  for (index = 0u; index < spelling_count; ++index) {
    const char *spelling = tbe_cbind_capability_spelling_at(index);
    const tbe_cbind_capability *capability = tbe_cbind_capability_find(spelling);
    if (!spelling || !capability ||
        !emit(consumer,
              "static_assert(std::is_same<decltype(CBindCapabilityMatrix_t::scalar_%zu), "
              "%s>::value, \"scalar spelling %s must use provider C storage\");\n",
              index, capability->c_storage, spelling))
      return 0;
    if (capability->kind == TBE_CBIND_SCALAR_INTEGER &&
        !emit(consumer,
              "static_assert(std::is_same<MatrixEnum_%zu_t, %s>::value, "
              "\"enum spelling %s must use provider C storage\");\n",
              index, capability->c_storage, spelling))
      return 0;
  }
  return emit(consumer,
              "\nint main() {\n"
              "  const cmeta_data_desc *descriptor = CBindCapabilityMatrix_cbind_data();\n"
              "  return descriptor != nullptr && cmeta_data_desc_valid(descriptor) "
              "? EXIT_SUCCESS : EXIT_FAILURE;\n"
              "}\n");
}

int main(int argc, char **argv) {
  FILE *schema;
  FILE *c_consumer;
  FILE *cpp_consumer;
  int success;

  if (argc != 4) return EXIT_FAILURE;
  schema = fopen(argv[1], "wb");
  c_consumer = fopen(argv[2], "wb");
  cpp_consumer = fopen(argv[3], "wb");
  if (!schema || !c_consumer || !cpp_consumer) {
    if (schema) fclose(schema);
    if (c_consumer) fclose(c_consumer);
    if (cpp_consumer) fclose(cpp_consumer);
    return EXIT_FAILURE;
  }
  success = emit_schema(schema) && emit_c_consumer(c_consumer) &&
            emit_cpp_consumer(cpp_consumer);
  if (fclose(schema) != 0 || fclose(c_consumer) != 0 ||
      fclose(cpp_consumer) != 0)
    success = 0;
  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
