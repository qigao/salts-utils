#include "orbit_native_projection.h"
#include "service_native.h"
#include "native_service_projection.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Use the original Salts IDL parser and canonical Service-native lowering.
 * No second parser, hand-maintained ABI or test-only Orbit runtime.
 */
static const char ORBIT_GOOD[] =
    "schema Game [version(1)];\n"
    "message PingRequest { uint64 seq; }\n"
    "message PingResponse { uint64 ack; }\n"
    "service Room {\n"
    "    [orbit(10,100)] Ping: PingRequest -> PingResponse;\n"
    "    [orbit(20,101)] Pong: PingRequest -> PingResponse;\n"
    "}\n";

static const char ORBIT_CONTEXTUAL[] =
    "schema Game [version(1)];\n"
    "message PingRequest { uint64 seq; }\n"
    "message PingResponse { uint64 ack; }\n"
    "service Room {\n"
    "    [orbit(10,100),native_context(1)] Ping: PingRequest -> PingResponse;\n"
    "    [orbit(20,101),native_context(1)] Pong: PingRequest -> PingResponse;\n"
    "}\n";

static const char ORBIT_CONTEXT_MIXED[] =
    "schema Game [version(1)];\n"
    "message Req { uint64 seq; }\n"
    "message Resp { uint64 ack; }\n"
    "service Room {\n"
    "    [orbit(10,100),native_context(1)] Ping: Req -> Resp;\n"
    "    [orbit(20,101)] Pong: Req -> Resp;\n"
    "}\n";

static const char ORBIT_CONTEXT_VERSION_UNSUPPORTED[] =
    "schema Game [version(1)];\n"
    "message Req { uint64 seq; }\n"
    "message Resp { uint64 ack; }\n"
    "service Room {\n"
    "    [orbit(10,100),native_context(2)] Ping: Req -> Resp;\n"
    "}\n";

static const char ORBIT_CONTEXT_DUPLICATED[] =
    "schema Game [version(1)];\n"
    "message Req { uint64 seq; }\n"
    "message Resp { uint64 ack; }\n"
    "service Room {\n"
    "    [orbit(10,100),native_context(1),native_context(1)] Ping: Req -> Resp;\n"
    "}\n";

static const char ORBIT_DUP_EVENT[] =
    "schema Game [version(1)];\n"
    "message Req { uint64 seq; }\n"
    "message Resp { uint64 ack; }\n"
    "service Room {\n"
    "    [orbit(10,100)] Ping: Req -> Resp;\n"
    "    [orbit(20,100)] Pong: Req -> Resp;\n"
    "}\n";

static const char ORBIT_OUT_OF_ORDER[] =
    "schema Game [version(1)];\n"
    "message Req { uint64 seq; }\n"
    "message Resp { uint64 ack; }\n"
    "service Room {\n"
    "    [orbit(20,100)] Ping: Req -> Resp;\n"
    "    [orbit(10,101)] Pong: Req -> Resp;\n"
    "}\n";

static const char ORBIT_MISSING_METHOD[] =
    "schema Game [version(1)];\n"
    "message Req { uint64 seq; }\n"
    "message Resp { uint64 ack; }\n"
    "service Room {\n"
    "    [orbit(10,100)] Ping: Req -> Resp;\n"
    "    Pong: Req -> Resp;\n"
    "}\n";

static const char ORBIT_MANAGED_FIELD[] =
    "schema Game [version(1)];\n"
    "message Req { string name; }\n"
    "message Resp { uint64 ack; }\n"
    "service Room { [orbit(10,100)] Ping: Req -> Resp; }\n";

/* The canonical Native ABI may own these fields, but Orbit's current
 * generation-token/u64 CallPort has NO managed request/response lifecycle.
 * Explicit contextual annotations must not accidentally enable it.
 */
static const char ORBIT_CONTEXT_MANAGED_FIELD[] =
    "schema Game [version(1)];\n"
    "message Req { string name; bytes payload; }\n"
    "message Resp { uint64 ack; }\n"
    "service Room {\n"
    "    [orbit(10,100),native_context(1)] Ping: Req -> Resp;\n"
    "}\n";

/* Admission positive: exactly one contextual, budgeted owned-record method.
 * All ordinary unannotated managed messages still remain Native-only.
 */
static const char ORBIT_OWNED_CONTEXT_V1[] =
    "schema Game [version(1)];\n"
    "message OwnedPacket { string label; bytes payload; }\n"
    "service Room {\n"
    "    [orbit(10,100),native_context(1),orbit_value(32,32)]\n"
    "    Echo: OwnedPacket -> OwnedPacket;\n"
    "}\n";

static const char ORBIT_OWNED_OVER_BUDGET[] =
    "schema Game [version(1)];\n"
    "message OwnedPacket { string label; bytes payload; }\n"
    "service Room {\n"
    "    [orbit(10,100),native_context(1),orbit_value(65537,32)]\n"
    "    Echo: OwnedPacket -> OwnedPacket;\n"
    "}\n";

static const char ORBIT_OWNED_NO_RECEIVER[] =
    "schema Game [version(1)];\n"
    "message OwnedPacket { string label; bytes payload; }\n"
    "service Room {\n"
    "    [orbit(10,100),orbit_value(32,32)] Echo: OwnedPacket -> OwnedPacket;\n"
    "}\n";

static const char ORBIT_OWNED_OPTIONAL[] =
    "schema Game [version(1)];\n"
    "message OwnedPacket { optional string label; bytes payload; }\n"
    "service Room {\n"
    "    [orbit(10,100),native_context(1),orbit_value(32,32)]\n"
    "    Echo: OwnedPacket -> OwnedPacket;\n"
    "}\n";

static const char ORBIT_OWNED_MIXED[] =
    "schema Game [version(1)];\n"
    "message OwnedPacket { string label; bytes payload; }\n"
    "message Req { uint64 seq; }\n"
    "message Resp { uint64 ack; }\n"
    "service Room {\n"
    "    [orbit(10,100),native_context(1),orbit_value(32,32)]\n"
    "    Echo: OwnedPacket -> OwnedPacket;\n"
    "    [orbit(20,101),native_context(1)] Ping: Req -> Resp;\n"
    "}\n";

static const char ORBIT_OVERFLOW_ID[] =
    "schema Game [version(1)];\n"
    "message Req { uint64 seq; }\n"
    "message Resp { uint64 ack; }\n"
    "service Room { [orbit(18446744073709551616,100)] Ping: Req -> Resp; }\n";

static const char ORBIT_OVERFLOW_EVENT[] =
    "schema Game [version(1)];\n"
    "message Req { uint64 seq; }\n"
    "message Resp { uint64 ack; }\n"
    "service Room { [orbit(10,4294967296)] Ping: Req -> Resp; }\n";

static const char ORBIT_TYPED_ERROR[] =
    "schema Game [version(1)];\n"
    "message Req { uint64 seq; }\n"
    "message Resp { uint64 ack; }\n"
    "message Failure { uint32 code; }\n"
    "service Room { [orbit(10,100)] Ping: Req -> Resp throws Failure; }\n";

static const char ORBIT_OPTIONAL_FIELD[] =
    "schema Game [version(1)];\n"
    "message Req { optional uint64 seq; }\n"
    "message Resp { uint64 ack; }\n"
    "service Room { [orbit(10,100)] Ping: Req -> Resp; }\n";

static const char ORBIT_UNANNOTATED[] =
    "schema Game [version(1)];\n"
    "message Req { uint64 seq; }\n"
    "message Resp { uint64 ack; }\n"
    "service Room { Ping: Req -> Resp; }\n";

static char *render_to_text(
    const IdlContract *contract,
    const databind_compiler_service_native_ir *native) {
  FILE *file = tmpfile();
  long last;
  char *buffer = NULL;
  size_t length;
  if (file == NULL) return NULL;
  if (!databind_compiler_orbit_native_write_header(
          file, contract, native) ||
      fflush(file) != 0 || fseek(file, 0, SEEK_END) != 0)
    goto done;
  last = ftell(file);
  if (last < 0 || fseek(file, 0, SEEK_SET) != 0)
    goto done;
  length = (size_t)last;
  buffer = (char *)malloc(length + 1u);
  if (buffer == NULL) goto done;
  if (fread(buffer, 1u, length, file) != length) {
    free(buffer);
    buffer = NULL;
    goto done;
  }
  buffer[length] = '\0';
done:
  fclose(file);
  return buffer;
}

static int check_case(const char *source, int should_admit) {
  IdlContract *contract = NULL;
  IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
  databind_compiler_service_native_ir native = {0};
  char *printed = NULL;
  int actual;
  int valid = 0;

  if (idl_contract_parse(
          source, strlen(source), &contract, &diagnostic) == 0 ||
      contract == NULL) {
    fprintf(stderr, "Orbit unary IDL parser rejected test Contract: %s\n",
            diagnostic.message);
    goto done;
  }
  actual = databind_compiler_orbit_native_admitted(contract);
  if (actual != should_admit) {
    fprintf(stderr, "Orbit unary admission mismatch (expected %d): %s\n",
            should_admit, source);
    goto done;
  }

  if (should_admit) {
    if (databind_compiler_service_native_build(contract, &native) != 0)
      goto done;
    if (strstr(source, "native_context") != NULL &&
        strstr(source, "orbit_value") == NULL) {
      FILE *functions = tmpfile();
      long count;
      char *text = NULL;
      if (functions == NULL || native.operation_count == 0u) {
        if (functions != NULL) fclose(functions);
        goto done;
      }
      /* Both canonical Native Service lowering paths must consume one and
       * the same receiver convention, never an Orbit-specific method ABI.
       */
      if (!native.operations[0].requires_context ||
          databind_compiler_service_native_emit_prototype(
              functions, &native.operations[0]) != 0 ||
          databind_compiler_service_native_emit_execution(
              functions, &native.operations[0], 1) != 0 ||
          !databind_compiler_native_contract_emit_header_op(
              functions, &native.operations[0]) ||
          !databind_compiler_native_contract_emit_invoke(
              functions, &native.operations[0]) ||
          fflush(functions) != 0 ||
          fseek(functions, 0, SEEK_END) != 0 ||
          (count = ftell(functions)) < 0 ||
          fseek(functions, 0, SEEK_SET) != 0) {
        fclose(functions);
        goto done;
      }
      text = (char *)malloc((size_t)count + 1u);
      if (text == NULL) {
        fclose(functions);
        goto done;
      }
      if (fread(text, 1u, (size_t)count, functions) != (size_t)count) {
        free(text);
        fclose(functions);
        goto done;
      }
      text[count] = '\0';
      fclose(functions);
      if (strstr(text, "void *receiver, const PingRequest_t *request") == NULL ||
          strstr(text, "if (context == NULL) return false;") == NULL ||
          strstr(text, "context, (const PingRequest_t *)params[0]") == NULL ||
          strstr(text, "__databind_execution_bind(") == NULL ||
          strstr(text, "out->context = receiver;") == NULL ||
          strstr(text, "void *receiver, const PingRequest *request") == NULL ||
          strstr(text, "context, (const PingRequest *)params[0]") == NULL) {
        fprintf(stderr, "Canonical Native receiver ABI disagrees with Orbit lowering\n");
        free(text);
        goto done;
      }
      free(text);
    }
    printed = render_to_text(contract, &native);
    if (printed == NULL) goto done;
    if (databind_compiler_orbit_native_selected(contract) &&
        strstr(source, "orbit_value") != NULL) {
      if (strstr(printed, "__orbit_port_init_value(") == NULL ||
          strstr(printed, "orbit_call_value_async(") == NULL ||
          strstr(printed, "orbit_call_value_begin(") == NULL ||
          strstr(printed, "orbit_call_value_stage_reply(") == NULL ||
          strstr(printed, "orbit_call_value_end(") == NULL ||
          strstr(printed, "OwnedPacket_native_cmeta_value_bind(") == NULL ||
          strstr(printed, "32u, 32u") == NULL ||
          strstr(printed, "business_status = databind_4_Game_4_Room_4_Echo(") == NULL ||
          strstr(printed, "DATABIND_NATIVE_ENABLE_ORBIT") == NULL)
        goto done;
    } else if (databind_compiler_orbit_native_selected(contract)) {
      /* Type-safe wrappers and one ordered methods[] table originate
       * exclusively from canonical, parsed IdlContract IDs and fields.
       */
      if (strstr(printed, "databind_4_Game_4_Room__orbit_methods[]") == NULL ||
          strstr(printed, "UINT64_C(10), UINT32_C(100)") == NULL ||
          strstr(printed, "UINT64_C(20), UINT32_C(101)") == NULL ||
          strstr(printed, "request->seq") == NULL ||
          strstr(printed, "record.ack = scalar;") == NULL ||
          strstr(printed, "__orbit_call_async(") == NULL ||
          strstr(printed, "__orbit_dispatch(") == NULL ||
          strstr(printed, "__orbit_service_config(") == NULL ||
          strstr(printed, "orbit_call_u64_method(port, token, &method)") == NULL ||
          strstr(printed, "business_status = databind_4_Game_4_Room_4_Ping(") == NULL ||
          strstr(printed, "ORBIT_METHOD_CALL_TOKEN") == NULL)
        goto done;
      if (strstr(source, "native_context") != NULL &&
          (strstr(printed, "__orbit_binding") == NULL ||
           strstr(printed, "__orbit_bind(") == NULL ||
           strstr(printed, "void *receiver = bound->receiver;") == NULL ||
           strstr(printed, "receiver, &typed_request, &typed_response") == NULL))
        goto done;
      if (strstr(source, "native_context") == NULL &&
          strstr(printed, "void *receiver = bound->receiver;") != NULL)
        goto done;
    } else if (printed[0] != '\0') {
      fprintf(stderr, "Unannotated Service unexpectedly emitted Orbit code\n");
      goto done;
    }
  }
  valid = 1;

done:
  free(printed);
  databind_compiler_service_native_destroy(&native);
  idl_contract_destroy(contract);
  return valid;
}

int databind_compiler_orbit_native_tests(void) {
  struct case_row {
    const char *source;
    int allowed;
  };
  const struct case_row cases[] = {
      {ORBIT_GOOD, 1},
      {ORBIT_CONTEXTUAL, 1},
      {ORBIT_CONTEXT_MIXED, 0},
      {ORBIT_CONTEXT_VERSION_UNSUPPORTED, 0},
      {ORBIT_CONTEXT_DUPLICATED, 0},
      {ORBIT_DUP_EVENT, 0},
      {ORBIT_OUT_OF_ORDER, 0},
      {ORBIT_MISSING_METHOD, 0},
      {ORBIT_MANAGED_FIELD, 0},
      {ORBIT_CONTEXT_MANAGED_FIELD, 0},
      {ORBIT_OWNED_CONTEXT_V1, 1},
      {ORBIT_OWNED_OVER_BUDGET, 0},
      {ORBIT_OWNED_NO_RECEIVER, 0},
      {ORBIT_OWNED_OPTIONAL, 0},
      {ORBIT_OWNED_MIXED, 0},
      {ORBIT_OVERFLOW_ID, 0},
      {ORBIT_OVERFLOW_EVENT, 0},
      {ORBIT_TYPED_ERROR, 0},
      {ORBIT_OPTIONAL_FIELD, 0},
      {ORBIT_UNANNOTATED, 1}
  };
  size_t i;
  for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    if (!check_case(cases[i].source, cases[i].allowed)) {
      fprintf(stderr, "Orbit unary projection failure in case %zu\n", i);
      return 0;
    }
  }
  return 1;
}
