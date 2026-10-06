#include "compiler_core.h"
#include "projection.h"
#include "wasm_projection.h"

#include "cmeta_fs.h"
#include "tinytest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WASM_PROJECTION_SCHEMA
#error "WASM_PROJECTION_SCHEMA is required"
#endif
#ifndef WASM_OPTIONAL_SCHEMA
#error "WASM_OPTIONAL_SCHEMA is required"
#endif
#ifndef WASM_ERROR_SCHEMA
#error "WASM_ERROR_SCHEMA is required"
#endif
#ifndef WASM_MULTI_RESPONSE_SCHEMA
#error "WASM_MULTI_RESPONSE_SCHEMA is required"
#endif

static int put_u8(unsigned char *out, size_t cap, size_t *used, uint8_t v) {
  if (out == NULL || used == NULL || *used >= cap) return 0;
  out[(*used)++] = v;
  return 1;
}

static int put_uleb(
    unsigned char *out, size_t cap, size_t *used, uint32_t value) {
  do {
    uint8_t byte = (uint8_t)(value & 0x7fu);
    value >>= 7u;
    if (value != 0u) byte |= 0x80u;
    if (!put_u8(out, cap, used, byte)) return 0;
  } while (value != 0u);
  return 1;
}

static int put_sleb32(
    unsigned char *out, size_t cap, size_t *used, int32_t value) {
  int more = 1;
  while (more) {
    uint8_t byte = (uint8_t)(value & 0x7f);
    int sign = (byte & 0x40u) != 0u;
    value >>= 7;
    more = !((value == 0 && !sign) || (value == -1 && sign));
    if (more) byte |= 0x80u;
    if (!put_u8(out, cap, used, byte)) return 0;
  }
  return 1;
}

static int put_bytes(
    unsigned char *out, size_t cap, size_t *used,
    const void *data, size_t size) {
  if (out == NULL || used == NULL ||
      (size != 0u && data == NULL) || size > cap - *used)
    return 0;
  if (size != 0u) memcpy(out + *used, data, size);
  *used += size;
  return 1;
}

static int put_name(
    unsigned char *out, size_t cap, size_t *used, const char *name) {
  size_t length = name != NULL ? strlen(name) : 0u;
  return length <= UINT32_MAX &&
         put_uleb(out, cap, used, (uint32_t)length) &&
         put_bytes(out, cap, used, name, length);
}

static int put_section(
    unsigned char *out, size_t cap, size_t *used, uint8_t id,
    const unsigned char *payload, size_t payload_size) {
  return payload_size <= UINT32_MAX &&
         put_u8(out, cap, used, id) &&
         put_uleb(out, cap, used, (uint32_t)payload_size) &&
         put_bytes(out, cap, used, payload, payload_size);
}

static int write_core_add_module(
    const char *path, const char *export_name) {
  static const unsigned char header[] = {
      0x00u,0x61u,0x73u,0x6du,0x01u,0x00u,0x00u,0x00u};
  unsigned char bytes[2048];
  unsigned char section[1024];
  unsigned char body[512];
  size_t used = 0u;
  size_t section_used = 0u;
  size_t body_used = 0u;
  FILE *file;

  if (!put_bytes(bytes, sizeof(bytes), &used, header, sizeof(header)))
    return 0;

  /* type0 realloc(i32,i32,i32,i32)->i32; type1 op(i32,i32)->i32 */
  if (!put_uleb(section, sizeof(section), &section_used, 2u) ||
      !put_u8(section, sizeof(section), &section_used, 0x60u) ||
      !put_uleb(section, sizeof(section), &section_used, 4u) ||
      !put_u8(section, sizeof(section), &section_used, 0x7fu) ||
      !put_u8(section, sizeof(section), &section_used, 0x7fu) ||
      !put_u8(section, sizeof(section), &section_used, 0x7fu) ||
      !put_u8(section, sizeof(section), &section_used, 0x7fu) ||
      !put_uleb(section, sizeof(section), &section_used, 1u) ||
      !put_u8(section, sizeof(section), &section_used, 0x7fu) ||
      !put_u8(section, sizeof(section), &section_used, 0x60u) ||
      !put_uleb(section, sizeof(section), &section_used, 2u) ||
      !put_u8(section, sizeof(section), &section_used, 0x7fu) ||
      !put_u8(section, sizeof(section), &section_used, 0x7fu) ||
      !put_uleb(section, sizeof(section), &section_used, 1u) ||
      !put_u8(section, sizeof(section), &section_used, 0x7fu) ||
      !put_section(bytes, sizeof(bytes), &used, 1u,
                   section, section_used))
    return 0;

  section_used = 0u;
  if (!put_uleb(section, sizeof(section), &section_used, 2u) ||
      !put_uleb(section, sizeof(section), &section_used, 0u) ||
      !put_uleb(section, sizeof(section), &section_used, 1u) ||
      !put_section(bytes, sizeof(bytes), &used, 3u,
                   section, section_used))
    return 0;

  /* one memory32 page */
  section_used = 0u;
  if (!put_uleb(section, sizeof(section), &section_used, 1u) ||
      !put_u8(section, sizeof(section), &section_used, 0x00u) ||
      !put_uleb(section, sizeof(section), &section_used, 1u) ||
      !put_section(bytes, sizeof(bytes), &used, 5u,
                   section, section_used))
    return 0;

  /* mutable i32 bump pointer = 64 */
  section_used = 0u;
  if (!put_uleb(section, sizeof(section), &section_used, 1u) ||
      !put_u8(section, sizeof(section), &section_used, 0x7fu) ||
      !put_u8(section, sizeof(section), &section_used, 0x01u) ||
      !put_u8(section, sizeof(section), &section_used, 0x41u) ||
      !put_sleb32(section, sizeof(section), &section_used, 64) ||
      !put_u8(section, sizeof(section), &section_used, 0x0bu) ||
      !put_section(bytes, sizeof(bytes), &used, 6u,
                   section, section_used))
    return 0;

  /* memory, cabi_realloc, Service operation */
  section_used = 0u;
  if (!put_uleb(section, sizeof(section), &section_used, 3u) ||
      !put_name(section, sizeof(section), &section_used, "memory") ||
      !put_u8(section, sizeof(section), &section_used, 0x02u) ||
      !put_uleb(section, sizeof(section), &section_used, 0u) ||
      !put_name(section, sizeof(section), &section_used, "cabi_realloc") ||
      !put_u8(section, sizeof(section), &section_used, 0x00u) ||
      !put_uleb(section, sizeof(section), &section_used, 0u) ||
      !put_name(section, sizeof(section), &section_used, export_name) ||
      !put_u8(section, sizeof(section), &section_used, 0x00u) ||
      !put_uleb(section, sizeof(section), &section_used, 1u) ||
      !put_section(bytes, sizeof(bytes), &used, 7u,
                   section, section_used))
    return 0;

  /* cabi_realloc: return old bump; bump += 64. */
  body_used = 0u;
  if (!put_uleb(body, sizeof(body), &body_used, 0u) ||
      !put_u8(body, sizeof(body), &body_used, 0x23u) ||
      !put_uleb(body, sizeof(body), &body_used, 0u) ||
      !put_u8(body, sizeof(body), &body_used, 0x23u) ||
      !put_uleb(body, sizeof(body), &body_used, 0u) ||
      !put_u8(body, sizeof(body), &body_used, 0x41u) ||
      !put_sleb32(body, sizeof(body), &body_used, 64) ||
      !put_u8(body, sizeof(body), &body_used, 0x6au) ||
      !put_u8(body, sizeof(body), &body_used, 0x24u) ||
      !put_uleb(body, sizeof(body), &body_used, 0u) ||
      !put_u8(body, sizeof(body), &body_used, 0x0bu))
    return 0;

  section_used = 0u;
  if (!put_uleb(section, sizeof(section), &section_used, 2u) ||
      !put_uleb(section, sizeof(section), &section_used,
                (uint32_t)body_used) ||
      !put_bytes(section, sizeof(section), &section_used,
                 body, body_used))
    return 0;

  /*
   * Service operation:
   * request wire = two little-endian u32 values.
   * result envelope at 256 = status(0) + response wire(sum).
   * result pair at 192 = {256, 8}; return 192.
   */
  body_used = 0u;
  if (!put_uleb(body, sizeof(body), &body_used, 0u) ||
      !put_u8(body, sizeof(body), &body_used, 0x41u) ||
      !put_sleb32(body, sizeof(body), &body_used, 256) ||
      !put_u8(body, sizeof(body), &body_used, 0x41u) ||
      !put_sleb32(body, sizeof(body), &body_used, 0) ||
      !put_u8(body, sizeof(body), &body_used, 0x36u) ||
      !put_uleb(body, sizeof(body), &body_used, 2u) ||
      !put_uleb(body, sizeof(body), &body_used, 0u) ||
      !put_u8(body, sizeof(body), &body_used, 0x41u) ||
      !put_sleb32(body, sizeof(body), &body_used, 260) ||
      !put_u8(body, sizeof(body), &body_used, 0x20u) ||
      !put_uleb(body, sizeof(body), &body_used, 0u) ||
      !put_u8(body, sizeof(body), &body_used, 0x28u) ||
      !put_uleb(body, sizeof(body), &body_used, 2u) ||
      !put_uleb(body, sizeof(body), &body_used, 0u) ||
      !put_u8(body, sizeof(body), &body_used, 0x20u) ||
      !put_uleb(body, sizeof(body), &body_used, 0u) ||
      !put_u8(body, sizeof(body), &body_used, 0x28u) ||
      !put_uleb(body, sizeof(body), &body_used, 2u) ||
      !put_uleb(body, sizeof(body), &body_used, 4u) ||
      !put_u8(body, sizeof(body), &body_used, 0x6au) ||
      !put_u8(body, sizeof(body), &body_used, 0x36u) ||
      !put_uleb(body, sizeof(body), &body_used, 2u) ||
      !put_uleb(body, sizeof(body), &body_used, 0u) ||
      !put_u8(body, sizeof(body), &body_used, 0x41u) ||
      !put_sleb32(body, sizeof(body), &body_used, 192) ||
      !put_u8(body, sizeof(body), &body_used, 0x41u) ||
      !put_sleb32(body, sizeof(body), &body_used, 256) ||
      !put_u8(body, sizeof(body), &body_used, 0x36u) ||
      !put_uleb(body, sizeof(body), &body_used, 2u) ||
      !put_uleb(body, sizeof(body), &body_used, 0u) ||
      !put_u8(body, sizeof(body), &body_used, 0x41u) ||
      !put_sleb32(body, sizeof(body), &body_used, 192) ||
      !put_u8(body, sizeof(body), &body_used, 0x41u) ||
      !put_sleb32(body, sizeof(body), &body_used, 8) ||
      !put_u8(body, sizeof(body), &body_used, 0x36u) ||
      !put_uleb(body, sizeof(body), &body_used, 2u) ||
      !put_uleb(body, sizeof(body), &body_used, 4u) ||
      !put_u8(body, sizeof(body), &body_used, 0x41u) ||
      !put_sleb32(body, sizeof(body), &body_used, 192) ||
      !put_u8(body, sizeof(body), &body_used, 0x0bu) ||
      !put_uleb(section, sizeof(section), &section_used,
                (uint32_t)body_used) ||
      !put_bytes(section, sizeof(section), &section_used,
                 body, body_used) ||
      !put_section(bytes, sizeof(bytes), &used, 10u,
                   section, section_used))
    return 0;

  file = fopen(path, "wb");
  if (file == NULL) return 0;
  if (fwrite(bytes, 1u, used, file) != used ||
      fflush(file) != 0 || fclose(file) != 0)
    return 0;
  return 1;
}

static int bytes_contains(
    const unsigned char *data, size_t size, const char *needle) {
  size_t needle_size;
  size_t i;
  if (data == NULL || needle == NULL) return 0;
  needle_size = strlen(needle);
  if (needle_size == 0u || needle_size > size) return 0;
  for (i = 0u; i + needle_size <= size; ++i)
    if (memcmp(data + i, needle, needle_size) == 0) return 1;
  return 0;
}

static void cleanup_outputs(
    const char *component,
    const char *host_header,
    const char *host_source,
    const char *guest_header,
    const char *core) {
  (void)cmeta_fs_unlink(component);
  (void)cmeta_fs_unlink(host_header);
  (void)cmeta_fs_unlink(host_source);
  (void)cmeta_fs_unlink(guest_header);
  if (core != NULL) (void)cmeta_fs_unlink(core);
}

static int run_projection(
    const char *schema,
    const char *component_id,
    const char *core_path,
    const char *component_output,
    const char *host_header,
    const char *host_source,
    const char *guest_header) {
  Node *root = NULL;
  IdlContract *contract = NULL;
  char *schema_data = NULL;
  databind_compiler_projection_input input = {0};
  databind_compiler_wasm_config config = {
      .component_id = component_id,
      .native_header = "wasm_projection_native.h",
      .core_module_path = core_path,
      .host_header_output = host_header,
      .host_source_output = host_source,
      .guest_header_output = guest_header,
      .symbol_prefix = "databind_wasm_projection"};
  databind_compiler_projection_request request = {
      .id = {DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT,
             DATABIND_COMPILER_ARTIFACT_WASM},
      .output = component_output,
      .config = &config};
  databind_compiler_projection_backend backend =
      DATABIND_COMPILER_WASM_BACKEND;
  int status;

  if (databind_compiler_parse_contract_file(
          schema, &root, &contract, &schema_data) != 0)
    return -2;
  input.contract = contract;
  status = databind_compiler_projection_run(
      &input, &request, 1u, &backend, 1u);
  idl_contract_destroy(contract);
  node_free(root);
  free(schema_data);
  return status;
}

spec("DataBind WASM projection backend") {
  it("wraps one scalar Service Core module as a Component and host capability") {
    static const char core[] = "databind_wasm_projection_core.wasm";
    static const char component[] = "databind_wasm_projection.wasm";
    static const char host_h[] = "databind_wasm_projection.wasm.h";
    static const char host_c[] = "databind_wasm_projection.wasm.c";
    static const char guest_h[] = "databind_wasm_projection.wasm_guest.h";
    static const char export_symbol[] =
        "databind_14_WasmProjection_4_Calc_3_Add";
    cmeta_fs_buf_t generated = {0};

    cleanup_outputs(component, host_h, host_c, guest_h, core);
    check_true(write_core_add_module(core, export_symbol));

    check_equal(run_projection(
                    WASM_PROJECTION_SCHEMA,
                    "WasmProjection.Calculator",
                    core, component, host_h, host_c, guest_h),
                0);

    check_equal(cmeta_fs_read_file(component, &generated), 0);
    check(generated.len >= 8u);
    check_equal((unsigned char)generated.base[0], 0x00u);
    check_equal((unsigned char)generated.base[1], 0x61u);
    check_equal((unsigned char)generated.base[2], 0x73u);
    check_equal((unsigned char)generated.base[3], 0x6du);
    check_equal((unsigned char)generated.base[4], 0x0du);
    check_true(bytes_contains(
        (const unsigned char *)generated.base, generated.len,
        "WasmProjection.Calc.Add"));
    check_true(bytes_contains(
        (const unsigned char *)generated.base, generated.len,
        export_symbol));
    cmeta_fs_buf_free(&generated);

    check_equal(cmeta_fs_read_file(host_c, &generated), 0);
    check_not_null(strstr(
        generated.base, "turbowasm_component_instance_invoke"));
    check_not_null(strstr(
        generated.base, "TURBOWASM_COMPONENT_HOST_LIST"));
    check_not_null(strstr(
        generated.base, "AddRequest_builder_bind"));
    check_not_null(strstr(
        generated.base, "AddResponse_view_bind"));
    check_not_null(strstr(
        generated.base, "DataBindNativeExecution"));
    check_not_null(strstr(
        generated.base, "WasmProjection.Calc.Add"));
    cmeta_fs_buf_free(&generated);

    check_equal(cmeta_fs_read_file(host_h, &generated), 0);
    check_not_null(strstr(
        generated.base, "#include <data_bind_binding_plan.h>"));
    check_not_null(strstr(
        generated.base, "DataBindServiceNativeBinding"));
    cmeta_fs_buf_free(&generated);

    check_equal(cmeta_fs_read_file(guest_h, &generated), 0);
    check_not_null(strstr(generated.base, export_symbol));
    check_not_null(strstr(generated.base, "cabi_realloc"));
    check_not_null(strstr(generated.base, "request_offset"));
    check_not_null(strstr(generated.base, "request_length"));
    check_null(strstr(generated.base, "result_pair_offset"));
    check_null(strstr(
        generated.base, "uint32_t left, uint32_t right"));
    cmeta_fs_buf_free(&generated);

    cleanup_outputs(component, host_h, host_c, guest_h, core);
  }

  it("carries multi-field fixed responses through one canonical wire result") {
    static const char core[] = "databind_wasm_multi_core.wasm";
    static const char component[] = "databind_wasm_multi.wasm";
    static const char host_h[] = "databind_wasm_multi.wasm.h";
    static const char host_c[] = "databind_wasm_multi.wasm.c";
    static const char guest_h[] = "databind_wasm_multi.wasm_guest.h";
    static const char export_symbol[] =
        "databind_9_WasmMulti_4_Calc_3_Add";
    cmeta_fs_buf_t generated = {0};

    cleanup_outputs(component, host_h, host_c, guest_h, core);
    check_true(write_core_add_module(core, export_symbol));
    check_equal(run_projection(
                    WASM_MULTI_RESPONSE_SCHEMA,
                    "WasmMulti.Calculator",
                    core, component, host_h, host_c, guest_h),
                0);

    check_equal(cmeta_fs_read_file(host_c, &generated), 0);
    check_not_null(strstr(generated.base, "AddResponse_view_bind"));
    check_not_null(strstr(generated.base, "response->value"));
    check_not_null(strstr(generated.base, "response->other"));
    check_null(strstr(generated.base, "TURBOWASM_COMPONENT_HOST_U32"));
    cmeta_fs_buf_free(&generated);

    cleanup_outputs(component, host_h, host_c, guest_h, core);
  }

  it("rejects optional and typed-error shapes while preserving wire ABI") {
    static const char core[] = "databind_wasm_reject_core.wasm";
    static const char component[] = "databind_wasm_reject.wasm";
    static const char host_h[] = "databind_wasm_reject.wasm.h";
    static const char host_c[] = "databind_wasm_reject.wasm.c";
    static const char guest_h[] = "databind_wasm_reject.wasm_guest.h";

    cleanup_outputs(component, host_h, host_c, guest_h, core);
    check_true(write_core_add_module(
        core, "databind_12_WasmOptional_4_Calc_3_Add"));

    check_equal(run_projection(
                    WASM_OPTIONAL_SCHEMA,
                    "WasmOptional.Calculator",
                    core, component, host_h, host_c, guest_h),
                -1);
    check(cmeta_fs_access(component, SALTS_FS_ACCESS_EXISTS) != 0);
    check(cmeta_fs_access(host_h, SALTS_FS_ACCESS_EXISTS) != 0);
    check(cmeta_fs_access(host_c, SALTS_FS_ACCESS_EXISTS) != 0);
    check(cmeta_fs_access(guest_h, SALTS_FS_ACCESS_EXISTS) != 0);

    check_equal(run_projection(
                    WASM_ERROR_SCHEMA,
                    "WasmError.Calculator",
                    core, component, host_h, host_c, guest_h),
                -1);
    check(cmeta_fs_access(component, SALTS_FS_ACCESS_EXISTS) != 0);

    cleanup_outputs(component, host_h, host_c, guest_h, core);
  }
}
