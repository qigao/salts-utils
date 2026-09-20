#ifndef DATA_BIND_CMETA_H
#define DATA_BIND_CMETA_H

#include "data_bind.h"

#include <cmeta/data.h>
#include <cmeta/range.h>

#if defined(_WIN32) && defined(DATA_BIND_CMETA_BUILD_DLL)
  #define DATA_BIND_CMETA_API __declspec(dllexport)
#else
  #define DATA_BIND_CMETA_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Selects the immutable DataBind view exposed through a CMeta range. */
typedef enum DataBindCMetaRangeKind {
  DATA_BIND_CMETA_RANGE_VALUES = 1,
  DATA_BIND_CMETA_RANGE_FIELDS = 2,
  DATA_BIND_CMETA_RANGE_MAP_ENTRIES = 3
} DataBindCMetaRangeKind;

/** Borrowed collection element. Invalid when the owning DataBindValue is freed. */
typedef struct DataBindValueRef {
  const DataBindValue *value;
} DataBindValueRef;

/** Borrowed object field. Both pointers are invalid when the owner is freed. */
typedef struct DataBindFieldRef {
  const char *name;
  const DataBindValue *value;
} DataBindFieldRef;

/** Borrowed map entry. Both pointers are invalid when the owner is freed. */
typedef struct DataBindMapEntryRef {
  const char *key;
  const DataBindValue *value;
} DataBindMapEntryRef;

DATA_BIND_CMETA_API const cmeta_type_desc *data_bind_cmeta_value_ref_type(void);
DATA_BIND_CMETA_API const cmeta_type_desc *data_bind_cmeta_field_ref_type(void);
DATA_BIND_CMETA_API const cmeta_type_desc *data_bind_cmeta_map_entry_ref_type(void);

/**
 * Maps the legacy dynamic value kind onto its canonical CMeta data semantic.
 *
 * This is a migration compatibility surface only; it does not create or own a
 * second descriptor/type-identity universe. NULL has no standalone native CMeta
 * storage kind and is rejected with DATA_BIND_ERR_TYPE_MISMATCH. Invalid input
 * and a NULL output pointer are rejected with DATA_BIND_ERR_INVALID_ARG. On
 * failure, *out_kind is left unchanged.
 */
DATA_BIND_CMETA_API DataBindStatus data_bind_cmeta_data_kind(DataBindValueKind value_kind,
                                                             cmeta_data_kind *out_kind);

/**
 * Creates a synchronous, allocation-free range over an immutable DataBind value.
 *
 * VALUES accepts LIST or SET, FIELDS accepts OBJECT, and MAP_ENTRIES accepts MAP.
 * The returned range and every emitted reference borrow owner; owner must outlive
 * all cursors and consumers. On failure, out_range is reset to all-bits-zero.
 */
DATA_BIND_CMETA_API DataBindStatus data_bind_cmeta_range_init(
    const DataBindValue *owner, DataBindCMetaRangeKind kind, cmeta_range *out_range);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_CMETA_H */
