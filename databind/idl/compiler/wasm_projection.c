#include "wasm_projection.h"

#include "schema_cmeta.h"
#include "service_native.h"

#include <salts_fs.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct wasm_buffer {
  unsigned char *data;
  size_t size;
  size_t capacity;
} wasm_buffer;

typedef struct wasm_scalar_lowering {
  uint8_t component_code;
  uint8_t core_code;
  const char *host_kind;
  const char *host_member;
  const char *c_type;
} wasm_scalar_lowering;

typedef struct wasm_operation_view {
  const databind_compiler_service_native_operation *native;
  const IdlDataDecl *request;
  const IdlDataDecl *response;
} wasm_operation_view;

static int wasm_component_has_service(
    const IdlComponent *component, const char *service_name) {
  size_t i;
  if (component == NULL || service_name == NULL) return 0;
  for (i = 0u; i < component->capability_count; ++i) {
    const IdlComponentCapability *capability = &component->capabilities[i];
    if (capability->kind == IDL_CAPABILITY_SERVICE &&
        capability->name != NULL &&
        strcmp(capability->name, service_name) == 0)
      return 1;
  }
  return 0;
}

static int wasm_select_component_service(
    void *context, const char *service_name) {
  return wasm_component_has_service(
      (const IdlComponent *)context, service_name);
}

static const IdlComponent *wasm_component(
    const IdlContract *contract, const char *component_id) {
  size_t i;
  if (contract == NULL || component_id == NULL ||
      component_id[0] == '\0')
    return NULL;
  for (i = 0u; i < contract->component_count; ++i) {
    const IdlComponent *component = &contract->components[i];
    if ((component->qualified_name != NULL &&
         strcmp(component->qualified_name, component_id) == 0) ||
        (component->name != NULL &&
         strcmp(component->name, component_id) == 0))
      return component;
  }
  return NULL;
}

static int wasm_scalar_lower(
    const IdlContract *contract, const IdlField *field,
    wasm_scalar_lowering *out) {
  schema_cmeta_field_type resolved = {0};
  const cmeta_data_integer_shape *integer_shape;
  const cmeta_data_float_shape *float_shape;

  if (contract == NULL || field == NULL || out == NULL ||
      field->collection_kind != IDL_COLLECTION_NONE ||
      field->optional || field->nullable ||
      field->default_value != NULL ||
      !schema_cmeta_field_resolve(contract, field, &resolved) ||
      resolved.data == NULL || resolved.data->storage_type == NULL)
    return 0;

  memset(out, 0, sizeof(*out));
  switch (resolved.data->kind) {
    case CMETA_DATA_BOOL:
      *out = (wasm_scalar_lowering){
          0x7fu, 0x7fu,
          "TURBOWASM_COMPONENT_HOST_BOOL", "boolean", "bool"};
      return 1;

    case CMETA_DATA_SINT:
      integer_shape =
          (const cmeta_data_integer_shape *)resolved.data->shape;
      if (integer_shape == NULL) return 0;
      switch (integer_shape->bits) {
        case 8u:
          *out = (wasm_scalar_lowering){
              0x7eu, 0x7fu,
              "TURBOWASM_COMPONENT_HOST_S8", "s8", "int8_t"};
          return 1;
        case 16u:
          *out = (wasm_scalar_lowering){
              0x7cu, 0x7fu,
              "TURBOWASM_COMPONENT_HOST_S16", "s16", "int16_t"};
          return 1;
        case 32u:
          *out = (wasm_scalar_lowering){
              0x7au, 0x7fu,
              "TURBOWASM_COMPONENT_HOST_S32", "s32", "int32_t"};
          return 1;
        case 64u:
          *out = (wasm_scalar_lowering){
              0x78u, 0x7eu,
              "TURBOWASM_COMPONENT_HOST_S64", "s64", "int64_t"};
          return 1;
        default:
          return 0;
      }

    case CMETA_DATA_UINT:
      integer_shape =
          (const cmeta_data_integer_shape *)resolved.data->shape;
      if (integer_shape == NULL) return 0;
      switch (integer_shape->bits) {
        case 8u:
          *out = (wasm_scalar_lowering){
              0x7du, 0x7fu,
              "TURBOWASM_COMPONENT_HOST_U8", "u8", "uint8_t"};
          return 1;
        case 16u:
          *out = (wasm_scalar_lowering){
              0x7bu, 0x7fu,
              "TURBOWASM_COMPONENT_HOST_U16", "u16", "uint16_t"};
          return 1;
        case 32u:
          *out = (wasm_scalar_lowering){
              0x79u, 0x7fu,
              "TURBOWASM_COMPONENT_HOST_U32", "u32", "uint32_t"};
          return 1;
        case 64u:
          *out = (wasm_scalar_lowering){
              0x77u, 0x7eu,
              "TURBOWASM_COMPONENT_HOST_U64", "u64", "uint64_t"};
          return 1;
        default:
          return 0;
      }

    case CMETA_DATA_FLOAT:
      float_shape =
          (const cmeta_data_float_shape *)resolved.data->shape;
      if (float_shape == NULL) return 0;
      if (float_shape->bits == 32u) {
        *out = (wasm_scalar_lowering){
            0x76u, 0x7du,
            "TURBOWASM_COMPONENT_HOST_F32", "f32", "float"};
        return 1;
      }
      if (float_shape->bits == 64u) {
        *out = (wasm_scalar_lowering){
            0x75u, 0x7cu,
            "TURBOWASM_COMPONENT_HOST_F64", "f64", "double"};
        return 1;
      }
      return 0;

    default:
      return 0;
  }
}

static int wasm_operation_view_build(
    const IdlContract *contract,
    const databind_compiler_service_native_operation *operation,
    wasm_operation_view *out) {
  const IdlDataDecl *request;
  const IdlDataDecl *response;
  size_t i;
  wasm_scalar_lowering scalar;

  if (contract == NULL || operation == NULL || out == NULL ||
      operation->error_count != 0u ||
      operation->request_presence_count != 0u ||
      operation->request_null_count != 0u ||
      operation->response_presence_count != 0u ||
      operation->response_null_count != 0u)
    return 0;

  request = idl_contract_find_data(contract, operation->request_type);
  response = idl_contract_find_data(contract, operation->response_type);
  if (request == NULL || response == NULL ||
      request->kind != IDL_DATA_MESSAGE ||
      response->kind != IDL_DATA_MESSAGE ||
      response->field_count == 0u)
    return 0;

  for (i = 0u; i < request->field_count; ++i)
    if (!wasm_scalar_lower(contract, &request->fields[i], &scalar))
      return 0;
  for (i = 0u; i < response->field_count; ++i)
    if (!wasm_scalar_lower(contract, &response->fields[i], &scalar))
      return 0;

  out->native = operation;
  out->request = request;
  out->response = response;
  return 1;
}

static int wasm_operations_build(
    const IdlContract *contract,
    const IdlComponent *component,
    const databind_compiler_service_native_ir *ir,
    wasm_operation_view **out_views,
    size_t *out_count) {
  wasm_operation_view *views;
  size_t count = 0u;
  size_t i;

  if (out_views == NULL || out_count == NULL) return 0;
  *out_views = NULL;
  *out_count = 0u;
  if (contract == NULL || component == NULL || ir == NULL ||
      ir->operations == NULL || ir->operation_count == 0u)
    return 0;

  views = (wasm_operation_view *)calloc(
      ir->operation_count, sizeof(*views));
  if (views == NULL) return 0;

  for (i = 0u; i < ir->operation_count; ++i) {
    if (!wasm_component_has_service(
            component, ir->operations[i].service_name))
      continue;
    if (!wasm_operation_view_build(
            contract, &ir->operations[i], &views[count])) {
      free(views);
      return 0;
    }
    ++count;
  }

  if (count == 0u) {
    free(views);
    return 0;
  }

  *out_views = views;
  *out_count = count;
  return 1;
}

static int wasm_buffer_reserve(wasm_buffer *buffer, size_t extra) {
  size_t required;
  size_t capacity;
  unsigned char *data;

  if (buffer == NULL || extra > SIZE_MAX - buffer->size) return 0;
  required = buffer->size + extra;
  if (required <= buffer->capacity) return 1;

  capacity = buffer->capacity != 0u ? buffer->capacity : 256u;
  while (capacity < required) {
    if (capacity > SIZE_MAX / 2u) {
      capacity = required;
      break;
    }
    capacity *= 2u;
  }
  data = (unsigned char *)realloc(buffer->data, capacity);
  if (data == NULL) return 0;
  buffer->data = data;
  buffer->capacity = capacity;
  return 1;
}

static int wasm_buffer_bytes(
    wasm_buffer *buffer, const void *data, size_t size) {
  if (buffer == NULL || (size != 0u && data == NULL) ||
      !wasm_buffer_reserve(buffer, size))
    return 0;
  if (size != 0u) memcpy(buffer->data + buffer->size, data, size);
  buffer->size += size;
  return 1;
}

static int wasm_buffer_u8(wasm_buffer *buffer, uint8_t value) {
  return wasm_buffer_bytes(buffer, &value, 1u);
}

static int wasm_buffer_uleb(wasm_buffer *buffer, uint32_t value) {
  do {
    uint8_t byte = (uint8_t)(value & 0x7fu);
    value >>= 7u;
    if (value != 0u) byte |= 0x80u;
    if (!wasm_buffer_u8(buffer, byte)) return 0;
  } while (value != 0u);
  return 1;
}

static int wasm_buffer_name(wasm_buffer *buffer, const char *name) {
  size_t length;
  if (name == NULL) return 0;
  length = strlen(name);
  return length <= UINT32_MAX &&
         wasm_buffer_uleb(buffer, (uint32_t)length) &&
         wasm_buffer_bytes(buffer, name, length);
}

static int wasm_buffer_section(
    wasm_buffer *component, uint8_t id,
    const wasm_buffer *payload) {
  return component != NULL && payload != NULL &&
         payload->size <= UINT32_MAX &&
         wasm_buffer_u8(component, id) &&
         wasm_buffer_uleb(component, (uint32_t)payload->size) &&
         wasm_buffer_bytes(component, payload->data, payload->size);
}

static void wasm_buffer_destroy(wasm_buffer *buffer) {
  if (buffer == NULL) return;
  free(buffer->data);
  memset(buffer, 0, sizeof(*buffer));
}

static int wasm_read_file(
    const char *path, unsigned char **out, size_t *out_size) {
  FILE *file;
  long length;
  unsigned char *data;

  if (out == NULL || out_size == NULL) return 0;
  *out = NULL;
  *out_size = 0u;
  if (path == NULL || path[0] == '\0') return 0;

  file = fopen(path, "rb");
  if (file == NULL) return 0;
  if (fseek(file, 0, SEEK_END) != 0 ||
      (length = ftell(file)) < 0 ||
      fseek(file, 0, SEEK_SET) != 0 ||
      (unsigned long)length > (unsigned long)SIZE_MAX) {
    fclose(file);
    return 0;
  }
  data = (unsigned char *)malloc((size_t)length);
  if (data == NULL && length != 0) {
    fclose(file);
    return 0;
  }
  if (length != 0 &&
      fread(data, 1u, (size_t)length, file) != (size_t)length) {
    free(data);
    fclose(file);
    return 0;
  }
  if (fclose(file) != 0) {
    free(data);
    return 0;
  }

  *out = data;
  *out_size = (size_t)length;
  return 1;
}

static int wasm_core_header_valid(
    const unsigned char *data, size_t size) {
  static const unsigned char header[] = {
      0x00u, 0x61u, 0x73u, 0x6du, 0x01u, 0x00u, 0x00u, 0x00u};
  return data != NULL && size >= sizeof(header) &&
         memcmp(data, header, sizeof(header)) == 0;
}

static int wasm_build_component(
    const IdlContract *contract,
    const wasm_operation_view *views,
    size_t view_count,
    const unsigned char *core_module,
    size_t core_module_size,
    wasm_buffer *out) {
  static const unsigned char preamble[] = {
      0x00u, 0x61u, 0x73u, 0x6du, 0x0du, 0x00u, 0x01u, 0x00u};
  wasm_buffer section = {0};
  size_t i;
  (void)contract;

  if (views == NULL || view_count == 0u ||
      view_count > UINT32_MAX - 2u ||
      !wasm_core_header_valid(core_module, core_module_size) ||
      core_module_size > UINT32_MAX || out == NULL)
    return 0;

  if (!wasm_buffer_bytes(out, preamble, sizeof(preamble)))
    return 0;

  /* Embedded caller-supplied Core module. */
  if (!wasm_buffer_bytes(&section, core_module, core_module_size) ||
      !wasm_buffer_section(out, 0x01u, &section))
    goto fail;
  wasm_buffer_destroy(&section);

  /* Core instance 0 = instantiate module 0 with no imports. */
  if (!wasm_buffer_uleb(&section, 1u) ||
      !wasm_buffer_u8(&section, 0x00u) ||
      !wasm_buffer_uleb(&section, 0u) ||
      !wasm_buffer_uleb(&section, 0u) ||
      !wasm_buffer_section(out, 0x02u, &section))
    goto fail;
  wasm_buffer_destroy(&section);

  /*
   * Canonical aliases:
   *   core memory0 = instance0.memory
   *   core func0   = instance0.cabi_realloc
   *   core func1+  = Service operation exports
   */
  if (!wasm_buffer_uleb(&section, (uint32_t)view_count + 2u) ||
      !wasm_buffer_u8(&section, 0x00u) ||
      !wasm_buffer_u8(&section, 0x02u) ||
      !wasm_buffer_u8(&section, 0x01u) ||
      !wasm_buffer_uleb(&section, 0u) ||
      !wasm_buffer_name(&section, "memory") ||
      !wasm_buffer_u8(&section, 0x00u) ||
      !wasm_buffer_u8(&section, 0x00u) ||
      !wasm_buffer_u8(&section, 0x01u) ||
      !wasm_buffer_uleb(&section, 0u) ||
      !wasm_buffer_name(&section, "cabi_realloc"))
    goto fail;
  for (i = 0u; i < view_count; ++i) {
    if (!wasm_buffer_u8(&section, 0x00u) ||
        !wasm_buffer_u8(&section, 0x00u) ||
        !wasm_buffer_u8(&section, 0x01u) ||
        !wasm_buffer_uleb(&section, 0u) ||
        !wasm_buffer_name(&section, views[i].native->symbol))
      goto fail;
  }
  if (!wasm_buffer_section(out, 0x06u, &section))
    goto fail;
  wasm_buffer_destroy(&section);

  /*
   * type0 = list<u8>
   * type1 = func(request: type0) -> type0
   *
   * Request list bytes are exactly the generated canonical DataBind binary
   * wire record. Result list bytes are:
   *   i32 native_status (little endian)
   *   canonical DataBind response wire bytes when status == 0.
   */
  if (!wasm_buffer_uleb(&section, 2u) ||
      !wasm_buffer_u8(&section, 0x70u) ||
      !wasm_buffer_u8(&section, 0x7du) ||
      !wasm_buffer_u8(&section, 0x40u) ||
      !wasm_buffer_uleb(&section, 1u) ||
      !wasm_buffer_name(&section, "request") ||
      !wasm_buffer_u8(&section, 0x00u) ||
      !wasm_buffer_u8(&section, 0x00u) ||
      !wasm_buffer_u8(&section, 0x00u) ||
      !wasm_buffer_section(out, 0x07u, &section))
    goto fail;
  wasm_buffer_destroy(&section);

  /*
   * Each canon lift targets one operation Core function and uses the same
   * Core memory0 + realloc func0. The Core operation signature is therefore
   * canonical lift(list<u8> -> list<u8>) for memory32:
   *   (i32 request_ptr, i32 request_len) -> i32 result_pair_ptr
   */
  if (!wasm_buffer_uleb(&section, (uint32_t)view_count))
    goto fail;
  for (i = 0u; i < view_count; ++i) {
    if (!wasm_buffer_u8(&section, 0x00u) || /* canon lift */
        !wasm_buffer_u8(&section, 0x00u) || /* core func sort */
        !wasm_buffer_uleb(&section, (uint32_t)i + 1u) ||
        !wasm_buffer_uleb(&section, 2u) ||
        !wasm_buffer_u8(&section, 0x03u) || /* memory */
        !wasm_buffer_uleb(&section, 0u) ||
        !wasm_buffer_u8(&section, 0x04u) || /* realloc */
        !wasm_buffer_uleb(&section, 0u) ||
        !wasm_buffer_uleb(&section, 1u))    /* func type1 */
      goto fail;
  }
  if (!wasm_buffer_section(out, 0x08u, &section))
    goto fail;
  wasm_buffer_destroy(&section);

  if (!wasm_buffer_uleb(&section, (uint32_t)view_count))
    goto fail;
  for (i = 0u; i < view_count; ++i) {
    if (!wasm_buffer_u8(&section, 0x00u) ||
        !wasm_buffer_name(
            &section, views[i].native->qualified_operation) ||
        !wasm_buffer_u8(&section, 0x01u) ||
        !wasm_buffer_uleb(&section, (uint32_t)i) ||
        !wasm_buffer_u8(&section, 0x00u))
      goto fail;
  }
  if (!wasm_buffer_section(out, 0x0bu, &section))
    goto fail;

  wasm_buffer_destroy(&section);
  return 1;

fail:
  wasm_buffer_destroy(&section);
  return 0;
}

static const char *wasm_basename(const char *path) {
  const char *base = path;
  const char *p;
  if (path == NULL) return NULL;
  for (p = path; *p != '\0'; ++p)
    if (*p == '/' || *p == '\\') base = p + 1;
  return base;
}

static int wasm_header_guard(
    const char *prefix, const char *suffix,
    char *out, size_t out_size) {
  size_t used = 0u;
  size_t i;
  if (prefix == NULL || suffix == NULL || out == NULL ||
      out_size == 0u)
    return 0;
  for (i = 0u; prefix[i] != '\0'; ++i) {
    unsigned char ch = (unsigned char)prefix[i];
    if (!((ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z') ||
          (ch >= '0' && ch <= '9') ||
          ch == '_'))
      return 0;
    if (used + 1u >= out_size) return 0;
    out[used++] =
        ch >= 'a' && ch <= 'z'
            ? (char)(ch - 'a' + 'A')
            : (char)ch;
  }
  for (i = 0u; suffix[i] != '\0'; ++i) {
    if (used + 1u >= out_size) return 0;
    out[used++] = suffix[i];
  }
  out[used] = '\0';
  return used != 0u;
}

static int wasm_write_host_header(
    FILE *file, const databind_compiler_wasm_config *config,
    const wasm_operation_view *views, size_t view_count) {
  char guard[256];
  size_t i;
  if (file == NULL || config == NULL || views == NULL ||
      !wasm_header_guard(
          config->symbol_prefix, "_WASM_HOST_H",
          guard, sizeof(guard)))
    return 0;

  if (fprintf(
          file,
          "#ifndef %s\n#define %s\n\n"
          "#include <data_bind_native_binding.h>\n"
          "#include <stddef.h>\n#include <stdint.h>\n\n"
          "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n"
          "typedef struct %s_wasm_host {\n"
          "  const uint8_t *component_bytes;\n"
          "  size_t component_size;\n"
          "} %s_wasm_host;\n\n"
          "int %s_wasm_host_init(\n"
          "    %s_wasm_host *host,\n"
          "    const void *component_bytes, size_t component_size);\n"
          "void %s_wasm_host_destroy(%s_wasm_host *host);\n\n",
          guard, guard,
          config->symbol_prefix,
          config->symbol_prefix,
          config->symbol_prefix,
          config->symbol_prefix,
          config->symbol_prefix,
          config->symbol_prefix) < 0)
    return 0;

  for (i = 0u; i < view_count; ++i) {
    if (fprintf(
            file,
            "const cmeta_function_desc *%s__databind_function(void);\n"
            "const cmeta_function_abi_desc *%s__databind_function_abi(void);\n"
            "DataBindStatus %s__databind_native_binding(\n"
            "    DataBindNativeTypeBinding *request_out,\n"
            "    DataBindNativeTypeBinding *response_out,\n"
            "    DataBindServiceNativeBinding *service_out,\n"
            "    DataBindError *error);\n"
            "int %s__databind_wasm_execution(\n"
            "    %s_wasm_host *host,\n"
            "    DataBindNativeExecution *out);\n\n",
            views[i].native->symbol,
            views[i].native->symbol,
            views[i].native->symbol,
            views[i].native->symbol,
            config->symbol_prefix) < 0)
      return 0;
  }

  return fprintf(
             file,
             "#ifdef __cplusplus\n}\n#endif\n\n"
             "#endif /* %s */\n",
             guard) >= 0;
}

static int wasm_write_guest_header(
    FILE *file, const IdlContract *contract,
    const databind_compiler_wasm_config *config,
    const wasm_operation_view *views, size_t view_count) {
  char guard[256];
  size_t i;
  const char *native_header;
  (void)contract;

  if (file == NULL || config == NULL || views == NULL ||
      !wasm_header_guard(
          config->symbol_prefix, "_WASM_GUEST_H",
          guard, sizeof(guard)))
    return 0;
  native_header = wasm_basename(config->native_header);
  if (native_header == NULL) return 0;

  if (fprintf(
          file,
          "#ifndef %s\n#define %s\n\n"
          "#include \"%s\"\n"
          "#include <stddef.h>\n#include <stdint.h>\n\n"
          "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n"
          "enum { DATABIND_WASM_EXECUTION_STATUS_BYTES = 4u };\n\n"
          "/*\n"
          " * The Core module selected by the WASM projection must export:\n"
          " *   memory\n"
          " *   cabi_realloc(i32,i32,i32,i32)->i32\n"
          " * and every operation below.\n"
          " *\n"
          " * request_offset/request_length identify the canonical generated\n"
          " * DataBind binary request record in guest linear memory.\n"
          " * The operation returns a wasm32 pointer to two little-endian u32\n"
          " * values {envelope_offset,envelope_length}.\n"
          " * The envelope is little-endian i32 native status followed, only\n"
          " * on status 0, by the canonical generated response binary record.\n"
          " */\n"
          "uint32_t cabi_realloc(\n"
          "    uint32_t old_ptr, uint32_t old_size,\n"
          "    uint32_t align, uint32_t new_size);\n\n",
          guard, guard, native_header) < 0)
    return 0;

  for (i = 0u; i < view_count; ++i) {
    if (fprintf(
            file,
            "uint32_t %s(\n"
            "    uint32_t request_offset, uint32_t request_length);\n"
            "static inline int %s__wasm_request_bind(\n"
            "    uint32_t request_offset, uint32_t request_length,\n"
            "    %s_view_t *out) {\n"
            "  return out != NULL &&\n"
            "      %s_view_bind(\n"
            "          out, (const void *)(uintptr_t)request_offset,\n"
            "          (size_t)request_length);\n"
            "}\n\n",
            views[i].native->symbol,
            views[i].native->symbol,
            views[i].native->request_type,
            views[i].native->request_type) < 0)
      return 0;
  }

  return fprintf(
             file,
             "#ifdef __cplusplus\n}\n#endif\n\n"
             "#endif /* %s */\n",
             guard) >= 0;
}

static int wasm_write_host_source(
    FILE *file, const IdlContract *contract,
    const databind_compiler_wasm_config *config,
    const wasm_operation_view *views, size_t view_count) {
  size_t i;
  size_t j;
  const char *host_header;

  if (file == NULL || contract == NULL || config == NULL ||
      views == NULL)
    return 0;
  host_header = wasm_basename(config->host_header_output);
  if (host_header == NULL) return 0;

  if (fprintf(
          file,
          "#include \"%s\"\n"
          "#include \"%s\"\n"
          "#include <turbowasm/component.h>\n"
          "#include <string.h>\n\n"
          "_Static_assert(sizeof(int) == 4u, "
          "\"DataBind WASM status requires 32-bit int\");\n\n"
          "static int databind_wasm_u8_list(\n"
          "    const turbowasm_component_host_value *value,\n"
          "    uint8_t *out, size_t expected) {\n"
          "  size_t i;\n"
          "  if (value == NULL || value->kind != TURBOWASM_COMPONENT_HOST_LIST ||\n"
          "      value->as.list.count != expected ||\n"
          "      (expected != 0u && value->as.list.items == NULL))\n"
          "    return 0;\n"
          "  for (i = 0u; i < expected; ++i) {\n"
          "    if (value->as.list.items[i].kind != "
          "TURBOWASM_COMPONENT_HOST_U8)\n"
          "      return 0;\n"
          "    if (out != NULL) out[i] = value->as.list.items[i].as.u8;\n"
          "  }\n"
          "  return 1;\n"
          "}\n\n",
          host_header, config->native_header) < 0)
    return 0;

  for (i = 0u; i < view_count; ++i) {
    if (databind_compiler_service_native_emit_reflection(
            file, views[i].native, 1) != 0 ||
        databind_compiler_service_native_emit_binding(
            file, views[i].native) != 0)
      return 0;
  }

  if (fprintf(
          file,
          "int %s_wasm_host_init(\n"
          "    %s_wasm_host *host,\n"
          "    const void *component_bytes, size_t component_size) {\n"
          "  turbowasm_component component = {0};\n"
          "  if (host == NULL || component_bytes == NULL || component_size == 0u)\n"
          "    return 0;\n"
          "  *host = (%s_wasm_host){0};\n"
          "  if (turbowasm_component_load_borrowed(\n"
          "          &component, (const uint8_t *)component_bytes,\n"
          "          component_size) != TURBOWASM_OK)\n"
          "    return 0;\n"
          "  turbowasm_component_destroy(&component);\n"
          "  host->component_bytes = (const uint8_t *)component_bytes;\n"
          "  host->component_size = component_size;\n"
          "  return 1;\n"
          "}\n\n"
          "void %s_wasm_host_destroy(%s_wasm_host *host) {\n"
          "  if (host != NULL) *host = (%s_wasm_host){0};\n"
          "}\n\n",
          config->symbol_prefix,
          config->symbol_prefix,
          config->symbol_prefix,
          config->symbol_prefix,
          config->symbol_prefix,
          config->symbol_prefix) < 0)
    return 0;

  for (i = 0u; i < view_count; ++i) {
    const wasm_operation_view *view = &views[i];

    if (fprintf(
            file,
            "static bool DATA_BIND_NATIVE_CALL %s__wasm_invoke(\n"
            "    void *context, void *return_storage,\n"
            "    void *const *params, size_t param_count) {\n"
            "  %s_wasm_host *host = (%s_wasm_host *)context;\n"
            "  const %s_t *request;\n"
            "  %s_t *response;\n"
            "  uint8_t request_wire[%s_BLOCK_LENGTH != 0u ? "
            "%s_BLOCK_LENGTH : 1u] = {0};\n"
            "  uint8_t response_wire[%s_BLOCK_LENGTH != 0u ? "
            "%s_BLOCK_LENGTH : 1u] = {0};\n"
            "  %s_builder_t request_builder;\n"
            "  %s_view_t response_view;\n"
            "  turbowasm_component_host_value "
            "request_items[%s_BLOCK_LENGTH != 0u ? "
            "%s_BLOCK_LENGTH : 1u] = {{0}};\n"
            "  turbowasm_component_host_value argument = {0};\n"
            "  turbowasm_component_host_value result = {0};\n"
            "  turbowasm_component component = {0};\n"
            "  turbowasm_component_instance instance = {0};\n"
            "  size_t result_count = 0u;\n"
            "  size_t k;\n"
            "  uint32_t status_bits;\n"
            "  int native_status;\n"
            "  turbowasm_trap trap = TURBOWASM_TRAP_NONE;\n"
            "  int ok = 0;\n"
            "  if (host == NULL || host->component_bytes == NULL ||\n"
            "      return_storage == NULL || params == NULL ||\n"
            "      param_count != 2u || params[0] == NULL || params[1] == NULL)\n"
            "    return false;\n"
            "  request = (const %s_t *)params[0];\n"
            "  response = (%s_t *)params[1];\n"
            "  if (!%s_builder_bind(\n"
            "          &request_builder, request_wire, %s_BLOCK_LENGTH))\n"
            "    return false;\n",
            view->native->symbol,
            config->symbol_prefix,
            config->symbol_prefix,
            view->native->request_type,
            view->native->response_type,
            view->native->request_type,
            view->native->request_type,
            view->native->response_type,
            view->native->response_type,
            view->native->request_type,
            view->native->response_type,
            view->native->request_type,
            view->native->request_type,
            view->native->request_type,
            view->native->response_type,
            view->native->request_type,
            view->native->request_type) < 0)
      return 0;

    for (j = 0u; j < view->request->field_count; ++j) {
      if (fprintf(
              file,
              "  if (!%s_%s_set(&request_builder, request->%s))\n"
              "    return false;\n",
              view->native->request_type,
              view->request->fields[j].name,
              view->request->fields[j].name) < 0)
        return 0;
    }

    if (fprintf(
            file,
            "  for (k = 0u; k < %s_BLOCK_LENGTH; ++k) {\n"
            "    request_items[k].kind = TURBOWASM_COMPONENT_HOST_U8;\n"
            "    request_items[k].as.u8 = request_wire[k];\n"
            "  }\n"
            "  argument.kind = TURBOWASM_COMPONENT_HOST_LIST;\n"
            "  argument.as.list.items = request_items;\n"
            "  argument.as.list.count = %s_BLOCK_LENGTH;\n"
            "  if (turbowasm_component_load_borrowed(\n"
            "          &component, host->component_bytes,\n"
            "          host->component_size) != TURBOWASM_OK)\n"
            "    goto done;\n"
            "  if (turbowasm_component_instance_create(\n"
            "          &instance, &component) != TURBOWASM_OK)\n"
            "    goto done;\n"
            "  if (turbowasm_component_instance_invoke(\n"
            "          &instance,\n"
            "          (turbowasm_name){\n"
            "              (const uint8_t *)\"%s\", %zuu},\n"
            "          &argument, 1u, &result, 1u,\n"
            "          &result_count, &trap) != TURBOWASM_OK ||\n"
            "      trap != TURBOWASM_TRAP_NONE || result_count != 1u ||\n"
            "      result.kind != TURBOWASM_COMPONENT_HOST_LIST ||\n"
            "      result.as.list.count < 4u ||\n"
            "      result.as.list.items == NULL)\n"
            "    goto done;\n"
            "  for (k = 0u; k < result.as.list.count; ++k)\n"
            "    if (result.as.list.items[k].kind != "
            "TURBOWASM_COMPONENT_HOST_U8)\n"
            "      goto done;\n"
            "  status_bits =\n"
            "      (uint32_t)result.as.list.items[0].as.u8 |\n"
            "      ((uint32_t)result.as.list.items[1].as.u8 << 8u) |\n"
            "      ((uint32_t)result.as.list.items[2].as.u8 << 16u) |\n"
            "      ((uint32_t)result.as.list.items[3].as.u8 << 24u);\n"
            "  native_status = (int)(int32_t)status_bits;\n"
            "  *(int *)return_storage = native_status;\n"
            "  if (native_status != 0) {\n"
            "    if (result.as.list.count != 4u) goto done;\n"
            "    ok = 1;\n"
            "    goto done;\n"
            "  }\n"
            "  if (result.as.list.count != 4u + %s_BLOCK_LENGTH)\n"
            "    goto done;\n"
            "  for (k = 0u; k < %s_BLOCK_LENGTH; ++k)\n"
            "    response_wire[k] = result.as.list.items[4u + k].as.u8;\n"
            "  if (!%s_view_bind(\n"
            "          &response_view, response_wire, %s_BLOCK_LENGTH))\n"
            "    goto done;\n",
            view->native->request_type,
            view->native->request_type,
            view->native->qualified_operation,
            strlen(view->native->qualified_operation),
            view->native->response_type,
            view->native->response_type,
            view->native->response_type,
            view->native->response_type) < 0)
      return 0;

    for (j = 0u; j < view->response->field_count; ++j) {
      if (fprintf(
              file,
              "  response->%s = %s_%s_get(&response_view);\n",
              view->response->fields[j].name,
              view->native->response_type,
              view->response->fields[j].name) < 0)
        return 0;
    }

    if (fprintf(
            file,
            "  ok = 1;\n"
            "done:\n"
            "  if (result_count != 0u)\n"
            "    turbowasm_component_host_value_destroy(&result);\n"
            "  turbowasm_component_instance_destroy(&instance);\n"
            "  turbowasm_component_destroy(&component);\n"
            "  return ok != 0;\n"
            "}\n\n"
            "int %s__databind_wasm_execution(\n"
            "    %s_wasm_host *host, DataBindNativeExecution *out) {\n"
            "  DataBindNativeExecution execution =\n"
            "      (DataBindNativeExecution)DATA_BIND_NATIVE_EXECUTION_INIT;\n"
            "  if (out == NULL) return 0;\n"
            "  *out = execution;\n"
            "  if (host == NULL || host->component_bytes == NULL ||\n"
            "      host->component_size == 0u) return 0;\n"
            "  execution.function = &%s__function_meta;\n"
            "  execution.abi = &%s__function_abi_meta;\n"
            "  execution.context = host;\n"
            "  execution.invoke = %s__wasm_invoke;\n"
            "  if (!data_bind_native_execution_valid(&execution)) return 0;\n"
            "  *out = execution;\n"
            "  return 1;\n"
            "}\n\n",
            view->native->symbol,
            config->symbol_prefix,
            view->native->symbol,
            view->native->symbol,
            view->native->symbol) < 0)
      return 0;
  }

  return 1;
}

static int wasm_write_file(
    const char *path, const void *data, size_t size) {
  FILE *file;
  int ok;
  if (path == NULL || (size != 0u && data == NULL)) return 0;
  file = fopen(path, "wb");
  if (file == NULL) return 0;
  ok = size == 0u || fwrite(data, 1u, size, file) == size;
  if (fflush(file) != 0) ok = 0;
  if (fclose(file) != 0) ok = 0;
  return ok;
}

static int wasm_write_text_output(
    const char *path,
    int (*writer)(FILE *, const IdlContract *,
                  const databind_compiler_wasm_config *,
                  const wasm_operation_view *, size_t),
    const IdlContract *contract,
    const databind_compiler_wasm_config *config,
    const wasm_operation_view *views, size_t view_count) {
  FILE *file;
  int ok;
  if (path == NULL || writer == NULL) return 0;
  file = fopen(path, "wb");
  if (file == NULL) return 0;
  ok = writer(file, contract, config, views, view_count);
  if (fflush(file) != 0) ok = 0;
  if (fclose(file) != 0) ok = 0;
  return ok;
}

static int wasm_write_host_header_adapter(
    FILE *file, const IdlContract *contract,
    const databind_compiler_wasm_config *config,
    const wasm_operation_view *views, size_t view_count) {
  (void)contract;
  return wasm_write_host_header(file, config, views, view_count);
}

int databind_compiler_wasm_generate(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    void *context) {
  const databind_compiler_wasm_config *config =
      request != NULL
          ? (const databind_compiler_wasm_config *)request->config
          : NULL;
  (void)context;
  const IdlComponent *component;
  databind_compiler_service_native_ir ir = {0};
  wasm_operation_view *views = NULL;
  size_t view_count = 0u;
  unsigned char *core_module = NULL;
  size_t core_module_size = 0u;
  wasm_buffer component_binary = {0};
  int ok = 0;

  if (input == NULL || request == NULL || config == NULL ||
      input->contract == NULL ||
      request->id.axis != DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT ||
      request->id.kind != DATABIND_COMPILER_ARTIFACT_WASM ||
      request->output == NULL || request->output[0] == '\0' ||
      config->component_id == NULL ||
      config->native_header == NULL ||
      config->core_module_path == NULL ||
      config->host_header_output == NULL ||
      config->host_source_output == NULL ||
      config->guest_header_output == NULL ||
      config->symbol_prefix == NULL)
    return -1;

  component = wasm_component(input->contract, config->component_id);
  if (component == NULL)
    return -1;

  if (databind_compiler_service_native_build_selected(
          input->contract, wasm_select_component_service,
          (void *)component, &ir) != 0)
    goto cleanup;

  if (!wasm_operations_build(
          input->contract, component, &ir, &views, &view_count))
    goto cleanup;

  if (!wasm_read_file(
          config->core_module_path,
          &core_module, &core_module_size) ||
      !wasm_core_header_valid(core_module, core_module_size) ||
      !wasm_build_component(
          input->contract, views, view_count,
          core_module, core_module_size, &component_binary))
    goto cleanup;

  if (!wasm_write_file(
          request->output,
          component_binary.data, component_binary.size) ||
      !wasm_write_text_output(
          config->host_header_output,
          wasm_write_host_header_adapter,
          input->contract, config, views, view_count) ||
      !wasm_write_text_output(
          config->host_source_output,
          wasm_write_host_source,
          input->contract, config, views, view_count) ||
      !wasm_write_text_output(
          config->guest_header_output,
          wasm_write_guest_header,
          input->contract, config, views, view_count))
    goto cleanup;

  ok = 1;

cleanup:
  if (!ok) {
    if (request != NULL && request->output != NULL)
      (void)salts_fs_unlink(request->output);
    if (config != NULL) {
      if (config->host_header_output != NULL)
        (void)salts_fs_unlink(config->host_header_output);
      if (config->host_source_output != NULL)
        (void)salts_fs_unlink(config->host_source_output);
      if (config->guest_header_output != NULL)
        (void)salts_fs_unlink(config->guest_header_output);
    }
  }
  wasm_buffer_destroy(&component_binary);
  free(core_module);
  free(views);
  databind_compiler_service_native_destroy(&ir);
  return ok ? 0 : -1;
}

const databind_compiler_projection_backend
    DATABIND_COMPILER_WASM_BACKEND = {
        .id = {DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT,
               DATABIND_COMPILER_ARTIFACT_WASM},
        .name = "wasm",
        .generate = databind_compiler_wasm_generate,
        .context = NULL,
    };
