#ifndef SALTS_UTILS_IDL_CONTRACT_H
#define SALTS_UTILS_IDL_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IDL_CONTRACT_ABI_VERSION 1u
#define IDL_DIAGNOSTIC_ABI_VERSION 1u

typedef enum IdlStatus {
  IDL_OK = 0,
  IDL_INVALID_ARGUMENT = 1,
  IDL_NO_MEMORY = 2,
  IDL_LEXER_ERROR = 3,
  IDL_SYNTAX_ERROR = 4,
  IDL_SEMANTIC_ERROR = 5
} IdlStatus;

typedef struct IdlDiagnostic {
  size_t size;
  uint32_t abi_version;
  IdlStatus status;
  int line;
  int column;
  char message[256];
} IdlDiagnostic;

#define IDL_DIAGNOSTIC_INIT \
  {sizeof(IdlDiagnostic), IDL_DIAGNOSTIC_ABI_VERSION, IDL_OK, -1, -1, {0}}

typedef enum IdlDataKind {
  IDL_DATA_MESSAGE = 1,
  IDL_DATA_COMPOSITE = 2,
  IDL_DATA_GROUP = 3,
  IDL_DATA_ENUM = 4,
  IDL_DATA_UNION = 5
} IdlDataKind;

typedef enum IdlCollectionKind {
  IDL_COLLECTION_NONE = 0,
  IDL_COLLECTION_ARRAY = 1,
  IDL_COLLECTION_LIST = 2,
  IDL_COLLECTION_SET = 3,
  IDL_COLLECTION_MAP = 4,
  IDL_COLLECTION_GROUP = 5
} IdlCollectionKind;

#define IDL_TYPE_REF_MAX_DEPTH 32u
#define IDL_TYPE_REF_MAX_NODES 128u
#define IDL_TYPE_REF_MAX_BYTES 4096u
#define IDL_TYPE_REF_MAX_ARGUMENTS 2u

/* Borrowed logical type syntax; native identity and lifecycle remain CMeta-owned.
 * Spans are valid until their source text or owning Contract is destroyed. */
typedef struct IdlTypeRef {
  IdlCollectionKind collection_kind;
  const char *name;
  size_t name_length;
  size_t argument_count;
  const char *arguments[IDL_TYPE_REF_MAX_ARGUMENTS];
  size_t argument_lengths[IDL_TYPE_REF_MAX_ARGUMENTS];
} IdlTypeRef;

/* Parse a complete compact type expression, e.g. list<map<string,User>>.
 * Accepts named leaves and list/set/map applications. Whitespace, malformed
 * arity and the named resource limits return 0 without modifying out_type.
 * This is a compiler/plan-construction query, never an item execution query. */
int idl_type_ref_parse(const char *text, size_t length, IdlTypeRef *out_type);

typedef enum IdlCapabilityKind {
  IDL_CAPABILITY_SERVICE = 1,
  IDL_CAPABILITY_CHANNEL = 2
} IdlCapabilityKind;

typedef struct IdlAnnotation {
  const char *name;
  /* Canonical first argument; for a bare annotation this is "1". */
  const char *value;
  size_t argument_count;
  const char *const *arguments;
  int bare;
} IdlAnnotation;

typedef struct IdlConstraint {
  const char *kind;
  const char *value;
  const char *minimum;
  const char *maximum;
  const char *pattern;
} IdlConstraint;

typedef struct IdlField {
  const char *name;
  const char *type_name;
  IdlCollectionKind collection_kind;
  const char *inner_type;
  const char *key_type;
  const char *value_type;
  const char *length;
  const char *default_value;
  int optional;
  int nullable;
  size_t constraint_count;
  const IdlConstraint *constraints;
  size_t annotation_count;
  const IdlAnnotation *annotations;
} IdlField;

typedef struct IdlEnumItem {
  const char *name;
  const char *value;
} IdlEnumItem;

typedef struct IdlDataDecl {
  IdlDataKind kind;
  const char *name;
  const char *underlying_type;
  int flags;
  size_t field_count;
  const IdlField *fields;
  size_t enum_item_count;
  const IdlEnumItem *enum_items;
  size_t annotation_count;
  const IdlAnnotation *annotations;
} IdlDataDecl;

typedef struct IdlOperation {
  const char *name;
  const char *request_type;
  const char *response_type;
  size_t error_count;
  const char *const *error_types;
  size_t annotation_count;
  const IdlAnnotation *annotations;
} IdlOperation;

typedef struct IdlService {
  const char *name;
  size_t operation_count;
  const IdlOperation *operations;
  size_t annotation_count;
  const IdlAnnotation *annotations;
} IdlService;

typedef struct IdlChannel {
  const char *name;
  const char *qualified_name;
  const char *message_type;
  size_t annotation_count;
  const IdlAnnotation *annotations;
} IdlChannel;

typedef struct IdlComponentCapability {
  IdlCapabilityKind kind;
  const char *name;
  const char *qualified_name;
} IdlComponentCapability;

typedef struct IdlComponent {
  const char *name;
  const char *qualified_name;
  size_t capability_count;
  const IdlComponentCapability *capabilities;
  size_t annotation_count;
  const IdlAnnotation *annotations;
} IdlComponent;

typedef struct IdlContract {
  size_t size;
  uint32_t abi_version;
  const char *name;
  const char *version;
  size_t annotation_count;
  const IdlAnnotation *annotations;
  size_t data_count;
  const IdlDataDecl *data;
  size_t service_count;
  const IdlService *services;
  size_t channel_count;
  const IdlChannel *channels;
  size_t component_count;
  const IdlComponent *components;
} IdlContract;

int idl_contract_parse(
    const char *text, size_t length,
    IdlContract **out_contract, IdlDiagnostic *diagnostic);

void idl_contract_destroy(IdlContract *contract);

const IdlDataDecl *idl_contract_find_data(
    const IdlContract *contract, const char *name);
const IdlService *idl_contract_find_service(
    const IdlContract *contract, const char *name);
const IdlChannel *idl_contract_find_channel(
    const IdlContract *contract, const char *name);
const IdlComponent *idl_contract_find_component(
    const IdlContract *contract, const char *name);

size_t idl_annotation_count(
    const IdlAnnotation *annotations, size_t annotation_count,
    const char *name);
const IdlAnnotation *idl_annotation_find(
    const IdlAnnotation *annotations, size_t annotation_count,
    const char *name, size_t occurrence);
const char *idl_annotation_argument(
    const IdlAnnotation *annotation, size_t index);

#ifdef __cplusplus
}
#endif

#endif
