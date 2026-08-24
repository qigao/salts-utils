#ifndef TBE_CBIND_CAPABILITY_H
#define TBE_CBIND_CAPABILITY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tbe_cbind_scalar_kind {
  TBE_CBIND_SCALAR_BOOL = 0,
  TBE_CBIND_SCALAR_INTEGER,
  TBE_CBIND_SCALAR_FLOAT,
  TBE_CBIND_SCALAR_STRING,
  TBE_CBIND_SCALAR_UUID
} tbe_cbind_scalar_kind;

typedef struct tbe_cbind_capability {
  const char *canonical_name;
  tbe_cbind_scalar_kind kind;
  /* CMeta numeric shape width; bool, string, and UUID have no numeric width. */
  unsigned int bits;
  /* Signedness applies only when kind is TBE_CBIND_SCALAR_INTEGER. */
  int is_signed;
  int is_uuid;
  const char *c_storage;
  const char *cmeta_type_symbol;
  /* String data metadata remains schema-owned so its max_bytes stays explicit. */
  const char *cmeta_data_symbol;
} tbe_cbind_capability;

/* Returns process-lifetime immutable metadata, or NULL for a non-scalar spelling. */
const tbe_cbind_capability *tbe_cbind_capability_find(const char *type_name);

#ifdef __cplusplus
}
#endif

#endif /* TBE_CBIND_CAPABILITY_H */
