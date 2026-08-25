include_guard(GLOBAL)

include(CheckCSourceCompiles)

function(turboparser_require_turboutils_cbind_v2)
  foreach(required_target IN ITEMS TurboUtils::Core TurboUtils::CBind)
    if(NOT TARGET ${required_target})
      message(FATAL_ERROR
        "TurboParser TbeCBind v2 requires imported target ${required_target}")
    endif()
  endforeach()

  set(CMAKE_C_STANDARD 11)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_REQUIRED_QUIET ON)
  set(CMAKE_REQUIRED_LIBRARIES TurboUtils::Core TurboUtils::CBind)
  if(MSVC)
    set(CMAKE_REQUIRED_FLAGS "/experimental:c11atomics")
  endif()

  unset(TURBOPARSER_TURBOUTILS_CBIND_V2_AVAILABLE CACHE)
  check_c_source_compiles([=[
#include <cbind/cbind.h>
#include <cmeta/data.h>
#include <turbo_cmeta_data.h>
#include <turbo_cmeta_fixed_width.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(CMETA_DATA_ENUM_OPS_ABI_VERSION == 1u,
               "TurboParser requires the v1 CMeta enum adapter ABI");
_Static_assert(sizeof(turbo_uuid_t) == 16u,
               "TurboParser requires the canonical 16-byte UUID storage");

static bool probe_enum_is_zero(const void *object) {
  return object != NULL && *(const int32_t *)object == 0;
}

static cmeta_status probe_enum_read(const void *object, int64_t *out) {
  if (object == NULL || out == NULL)
    return CMETA_INVALID_ARGUMENT;
  *out = *(const int32_t *)object;
  return CMETA_OK;
}

static cmeta_status probe_enum_assign(void *object, int64_t value) {
  int32_t stored;
  if (object == NULL || (value != 0 && value != 1))
    return CMETA_INVALID_ARGUMENT;
  stored = (int32_t)value;
  memcpy(object, &stored, sizeof(stored));
  return CMETA_OK;
}

static void probe_enum_restore_zero(void *object) {
  if (object != NULL)
    memset(object, 0, sizeof(int32_t));
}

static const cmeta_type_identity probe_enum_identity =
    CMETA_TYPE_ID_ATOM_INIT("turboparser.probe.enum");
static const cmeta_type_desc probe_enum_type = {
    "turboparser_probe_enum", sizeof(int32_t), _Alignof(int32_t),
    CMETA_T_INTEGER, NULL, NULL, &probe_enum_identity};
static const cmeta_enum_item_desc probe_enum_items[] = {
    {0, "PROBE_ZERO", "zero"},
    {1, "PROBE_ONE", "one"}};
static const cmeta_enum_desc probe_enum_meta = {
    "turboparser_probe_enum", probe_enum_items, 2u};
static const cmeta_data_enum_shape probe_enum_shape = {&probe_enum_meta};
static const cmeta_data_enum_ops probe_enum_ops = {
    sizeof(cmeta_data_enum_ops), CMETA_DATA_ENUM_OPS_ABI_VERSION,
    &probe_enum_type, probe_enum_is_zero, probe_enum_read,
    probe_enum_assign, probe_enum_restore_zero};
static const cmeta_data_desc probe_enum_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "turboparser.probe.enum.data",
    .display_name = "TurboParser dependency probe enum",
    .kind = CMETA_DATA_ENUM,
    .storage_type = &probe_enum_type,
    .shape = &probe_enum_shape,
    .enum_ops = &probe_enum_ops};

int main(void) {
  static const cmeta_data_desc *const fixed_width[] = {
      &turbo_int8_cmeta_data,   &turbo_uint8_cmeta_data,
      &turbo_int16_cmeta_data,  &turbo_uint16_cmeta_data,
      &turbo_int32_cmeta_data,  &turbo_uint32_cmeta_data,
      &turbo_int64_cmeta_data,  &turbo_uint64_cmeta_data};
  cbind_status (*decode_fn)(const cbind_context *, const cmeta_data_desc *,
                            cserde_reader *, void *, cbind_error *) = cbind_decode;
  int32_t enum_value = 0;
  int64_t enum_readback = -1;
  bool enum_zero = false;
  size_t index;
  size_t width_sum = 0u;

  for (index = 0u; index < sizeof(fixed_width) / sizeof(fixed_width[0]); ++index) {
    const cmeta_data_integer_shape *shape =
        (const cmeta_data_integer_shape *)fixed_width[index]->shape;
    if (!cmeta_data_desc_valid(fixed_width[index]) || shape == NULL)
      return 1;
    width_sum += shape->bits;
  }
  if (width_sum != 240u || cmeta_data_enum_ops_of(&probe_enum_data) == NULL)
    return 2;
  if (cmeta_data_enum_is_zero(&probe_enum_data, &enum_value, &enum_zero) != CMETA_OK ||
      !enum_zero ||
      cmeta_data_enum_assign(&probe_enum_data, &enum_value, 1) != CMETA_OK ||
      cmeta_data_enum_read(&probe_enum_data, &enum_value, &enum_readback) != CMETA_OK ||
      enum_readback != 1 ||
      cmeta_data_enum_restore_zero(&probe_enum_data, &enum_value) != CMETA_OK)
    return 3;
  if (!turbo_uuid_cmeta_data_valid(&turbo_uuid_cmeta_data) ||
      turbo_uuid_cmeta_data.storage_type != &turbo_uuid_cmeta_type ||
      turbo_uuid_cmeta_data.shape != &turbo_uuid_cmeta_shape ||
      turbo_uuid_cmeta_data.buffer_ops != &turbo_uuid_cmeta_buffer_ops)
    return 4;
  return decode_fn == NULL;
}
]=] TURBOPARSER_TURBOUTILS_CBIND_V2_AVAILABLE)

  if(NOT TURBOPARSER_TURBOUTILS_CBIND_V2_AVAILABLE)
    message(FATAL_ERROR
      "TurboParser TbeCBind v2 requires TurboUtils fixed-width integer descriptors, enum adapter APIs, and canonical UUID Core metadata. "
      "The selected package failed the compile/link feature probe. Rebuild and install TurboUtils with fixed-width CMeta descriptors, enum CMeta/CBind support, and exported canonical UUID metadata; then ensure TurboUtils::Core and TurboUtils::CBind resolve from that same prefix.\n"
      "Inspect the CMake configure log for the compiler/linker diagnostic.")
  endif()
endfunction()
