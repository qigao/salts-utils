#include <tbe_cbind/tbe_cbind.h>

#include "turbo_cmeta_data.h"
#include "turbo_parser_json.h"
#include "turbo_str.h"

#include <stddef.h>
#include <string.h>

typedef struct installed_order {
  int quantity;
  tstr symbol;
} installed_order;

static const cmeta_data_buffer_shape installed_string_shape = {
    CMETA_DATA_BUFFER_OWNED};
static const cmeta_data_desc installed_string_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "consumer.tbe-cbind.string",
    .display_name = "installed consumer string",
    .kind = CMETA_DATA_STRING,
    .storage_type = &turbo_tstr_cmeta_type,
    .shape = &installed_string_shape,
    .buffer_ops = &turbo_tstr_cmeta_buffer_ops};
static const cmeta_type_identity installed_order_identity =
    CMETA_TYPE_ID_ATOM_INIT("consumer.tbe-cbind.order");
static const cmeta_type_desc installed_order_type = {
    .name = "installed_order",
    .size = sizeof(installed_order),
    .align = _Alignof(installed_order),
    .kind = CMETA_T_OBJECT,
    .identity = &installed_order_identity};
static const cmeta_field_desc installed_order_layout_fields[] = {
    {"quantity", "int", offsetof(installed_order, quantity), sizeof(int),
     _Alignof(int), &cmeta_type_int, NULL},
    {"symbol", "tstr", offsetof(installed_order, symbol), sizeof(tstr),
     _Alignof(tstr), &turbo_tstr_cmeta_type, NULL}};
static const cmeta_struct_desc installed_order_layout = {
    "installed_order", sizeof(installed_order), _Alignof(installed_order),
    installed_order_layout_fields, 2u};
static const cmeta_data_field_desc installed_order_fields[] = {
    {"consumer.tbe-cbind.order.quantity", "quantity",
     offsetof(installed_order, quantity), &cmeta_data_int},
    {"consumer.tbe-cbind.order.symbol", "symbol",
     offsetof(installed_order, symbol), &installed_string_data}};
static const cmeta_data_struct_shape installed_order_shape = {
    &installed_order_layout, installed_order_fields, 2u};
static const cmeta_data_desc installed_order_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "consumer.tbe-cbind.order.data",
    .display_name = "installed consumer order",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &installed_order_type,
    .shape = &installed_order_shape};

int main(void) {
  static const char schema[] =
      "message Order { [c(quantity), name(amount)] int32 count; "
      "[c(symbol), name(ticker)] string label; }";
  static const char json[] = "{\"amount\":12,\"ticker\":\"SDK\"}";
  tbe_cbind_plan_options options;
  tbe_cbind_plan_error plan_error;
  tbe_cbind_plan *plan = NULL;
  turbo_json_doc_t *document = NULL;
  cserde_reader *reader = NULL;
  installed_order out = {0};
  unsigned char scratch[1] = {0};
  cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
      scratch, sizeof(scratch), 1u, 0u, 32u);
  cbind_error bind_error = CBIND_ERROR_INIT;
  int result = 0;

  tbe_cbind_plan_options_init(&options);
  tbe_cbind_plan_error_init(&plan_error);
  if (tbe_cbind_plan_create_from_text(
          schema, sizeof(schema) - 1u, "Order", sizeof("Order") - 1u,
          &installed_order_data, &options, &plan, &plan_error) !=
      TBE_CBIND_OK) {
    result = 1;
    goto cleanup;
  }
  if (turbo_parse_json((const uint8_t *)json, sizeof(json) - 1u,
                       &document) != 0) {
    result = 2;
    goto cleanup;
  }
  reader = turbo_json_cserde_reader_create(document, 1u);
  if (reader == NULL) {
    result = 3;
    goto cleanup;
  }
  if (tbe_cbind_plan_decode(plan, &context, reader, &out, &bind_error) !=
      CBIND_OK) {
    result = 4;
    goto cleanup;
  }
  if (out.quantity != 12 || out.symbol == NULL ||
      strcmp(out.symbol, "SDK") != 0) {
    result = 5;
  }

cleanup:
  turbo_json_cserde_reader_destroy(reader);
  turbo_free_json(&document);
  if (cmeta_data_buffer_restore_zero(&installed_string_data, &out.symbol) !=
          CMETA_OK &&
      result == 0) {
    result = 6;
  }
  tbe_cbind_plan_destroy(plan);
  return result;
}
