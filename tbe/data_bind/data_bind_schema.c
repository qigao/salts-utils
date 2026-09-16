#include "data_bind_schema_internal.h"

#include "monocypher.h"

#include <stdint.h>
#include <string.h>

#define DATA_BIND_SCHEMA_FINGERPRINT_MAX_DEPTH 64U
#define DATA_BIND_SCHEMA_FINGERPRINT_DOMAIN "TurboUtils.DataBind.Schema.v1"

static void data_bind_schema_fingerprint_u64(crypto_blake2b_ctx *ctx, uint64_t value) {
  uint8_t encoded[8];
  size_t i;
  for (i = 0; i < sizeof(encoded); ++i) encoded[i] = (uint8_t)(value >> (i * 8U));
  crypto_blake2b_update(ctx, encoded, sizeof(encoded));
}

static void data_bind_schema_fingerprint_text(crypto_blake2b_ctx *ctx, const char *text) {
  size_t len;
  if (text == NULL) {
    data_bind_schema_fingerprint_u64(ctx, UINT64_MAX);
    return;
  }
  len = strlen(text);
  data_bind_schema_fingerprint_u64(ctx, (uint64_t)len);
  if (len != 0) crypto_blake2b_update(ctx, (const uint8_t *)text, len);
}

static int data_bind_schema_fingerprint_node(crypto_blake2b_ctx *ctx, const Node *node,
                                             unsigned depth) {
  uint8_t type;
  size_t count = 0;
  size_t i;
  if (ctx == NULL || node == NULL || depth > DATA_BIND_SCHEMA_FINGERPRINT_MAX_DEPTH) return 0;
  type = (uint8_t)node->type;
  crypto_blake2b_update(ctx, &type, sizeof(type));
  data_bind_schema_fingerprint_text(ctx, node->name);
  switch (node->type) {
  case NODE_STRING:
    data_bind_schema_fingerprint_text(ctx, node->data.string_val);
    return 1;
  case NODE_LIST:
    count = node->data.list.count;
    data_bind_schema_fingerprint_u64(ctx, (uint64_t)count);
    for (i = 0; i < count; ++i)
      if (!data_bind_schema_fingerprint_node(ctx, node->data.list.items[i], depth + 1U)) return 0;
    return 1;
  case NODE_ROOT:
  case NODE_MAP:
    count = node->data.map.count;
    data_bind_schema_fingerprint_u64(ctx, (uint64_t)count);
    for (i = 0; i < count; ++i)
      if (!data_bind_schema_fingerprint_node(ctx, node->data.map.items[i], depth + 1U)) return 0;
    return 1;
  default:
    return 0;
  }
}

int data_bind_schema_fingerprint(const Node *root,
                                 uint8_t out[DATA_BIND_SCHEMA_FINGERPRINT_SIZE]) {
  crypto_blake2b_ctx ctx;
  static const uint8_t domain[] = DATA_BIND_SCHEMA_FINGERPRINT_DOMAIN;
  if (root == NULL || out == NULL) return 0;
  crypto_blake2b_init(&ctx, DATA_BIND_SCHEMA_FINGERPRINT_SIZE);
  crypto_blake2b_update(&ctx, domain, sizeof(domain) - 1U);
  if (!data_bind_schema_fingerprint_node(&ctx, root, 0U)) return 0;
  crypto_blake2b_final(&ctx, out);
  return 1;
}
