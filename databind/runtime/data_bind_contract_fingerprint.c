#include "data_bind_contract_fingerprint.h"
#include "monocypher.h"

#include <stdint.h>
#include <string.h>

#define DATA_BIND_CONTRACT_FINGERPRINT_MAX_DEPTH 64U
#define DATA_BIND_CONTRACT_FINGERPRINT_DOMAIN "SaltsUtils.DataBind.Contract.v1"

static void fingerprint_u64(crypto_blake2b_ctx *ctx, uint64_t value) {
  uint8_t encoded[8];
  size_t i;
  for (i = 0u; i < sizeof(encoded); ++i)
    encoded[i] = (uint8_t)(value >> (i * 8U));
  crypto_blake2b_update(ctx, encoded, sizeof(encoded));
}

static void fingerprint_text(crypto_blake2b_ctx *ctx, const char *text) {
  size_t len;
  if (text == NULL) {
    fingerprint_u64(ctx, UINT64_MAX);
    return;
  }
  len = strlen(text);
  fingerprint_u64(ctx, (uint64_t)len);
  if (len != 0u)
    crypto_blake2b_update(ctx, (const uint8_t *)text, len);
}

static int fingerprint_node(
    crypto_blake2b_ctx *ctx, const Node *node, unsigned depth) {
  uint8_t type;
  size_t count = 0u;
  size_t i;
  if (ctx == NULL || node == NULL ||
      depth > DATA_BIND_CONTRACT_FINGERPRINT_MAX_DEPTH)
    return 0;
  type = (uint8_t)node->type;
  crypto_blake2b_update(ctx, &type, sizeof(type));
  fingerprint_text(ctx, node->name);
  switch (node->type) {
  case NODE_STRING:
    fingerprint_text(ctx, node->data.string_val);
    return 1;
  case NODE_LIST:
    count = node->data.list.count;
    fingerprint_u64(ctx, (uint64_t)count);
    for (i = 0u; i < count; ++i)
      if (!fingerprint_node(ctx, node->data.list.items[i], depth + 1u))
        return 0;
    return 1;
  case NODE_ROOT:
  case NODE_MAP:
    count = node->data.map.count;
    fingerprint_u64(ctx, (uint64_t)count);
    for (i = 0u; i < count; ++i)
      if (!fingerprint_node(ctx, node->data.map.items[i], depth + 1u))
        return 0;
    return 1;
  default:
    return 0;
  }
}

int data_bind_contract_fingerprint(
    const Node *root,
    uint8_t out[DATA_BIND_CONTRACT_FINGERPRINT_SIZE]) {
  crypto_blake2b_ctx ctx;
  static const uint8_t domain[] = DATA_BIND_CONTRACT_FINGERPRINT_DOMAIN;
  if (root == NULL || out == NULL) return 0;
  crypto_blake2b_init(&ctx, DATA_BIND_CONTRACT_FINGERPRINT_SIZE);
  crypto_blake2b_update(&ctx, domain, sizeof(domain) - 1u);
  if (!fingerprint_node(&ctx, root, 0u)) return 0;
  crypto_blake2b_final(&ctx, out);
  return 1;
}
