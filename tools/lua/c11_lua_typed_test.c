#include "turbo_lua_bind.h"

#include "lauxlib.h"
#include "tinytest.h"

#include <string.h>

typedef struct LuaTypedMapEntry {
  tstr_t key;
  int32_t value;
} LuaTypedMapEntry;

TURBO_VEC_DEFINE(LuaTypedMap, LuaTypedMapEntry)

typedef struct LuaTypedRecord {
  uint8_t presence[1];
  uint8_t digest[3];
  int16_t samples[2];
  LuaTypedMap labels;
  tstr_t note;
} LuaTypedRecord;

TBE_TYPED_DEFINE_STRUCT_WITH_PRESENCE(
    LuaTypedRecordType, LuaTypedRecord, "LuaTypedRecord", presence,
    TBE_TYPED_FIXED_BYTES_FIELD(LuaTypedRecord, digest, "digest", TBE_TYPED_REQUIRED),
    TBE_TYPED_FIXED_ARRAY_FIELD(LuaTypedRecord, samples, "samples", TBE_TYPED_I16,
                                int16_t, NULL, TBE_TYPED_REQUIRED),
    TBE_TYPED_MAP_FIELD(LuaTypedRecord, labels, "labels", LuaTypedMapEntry, key,
                        value, TBE_TYPED_I32, NULL, TBE_TYPED_REQUIRED),
    TBE_TYPED_FIELD(LuaTypedRecord, note, "note", TBE_TYPED_STRING,
                    TBE_TYPED_OPTIONAL(0)));

spec("C11 Lua typed adapter") {
  static lua_State *L = NULL;
  static LuaTypedRecord record;

  before_each() {
    LuaTypedMapEntry entry = {0};
    L = luaL_newstate();
    check_not_null(L);
    check_int_eq(TBE_TYPED_BIND_INIT(LuaTypedRecordType, &record, NULL), DATA_BIND_OK);
    memcpy(record.digest, "A\0Z", sizeof(record.digest));
    record.samples[0] = -2;
    record.samples[1] = 7;
    entry.key = tstr_dup("priority");
    entry.value = 3;
    check_not_null(entry.key);
    check_int_eq(LuaTypedMap_push(&record.labels, entry), TURBO_OK);
  }

  after_each() {
    TBE_TYPED_BIND_CLEAR(LuaTypedRecordType, &record);
    if (L != NULL) lua_close(L);
  }

  it("should copy fixed arrays, binary bytes, maps, and optional absence") {
    size_t digest_len = 0;
    const char *digest;

    check_int_eq(c11_lua_push_tbe_typed(L, &LuaTypedRecordType, &record, 8u),
                 DATA_BIND_OK);
    lua_getfield(L, -1, "digest");
    digest = lua_tolstring(L, -1, &digest_len);
    check_size_eq(digest_len, sizeof(record.digest));
    check_mem_eq(digest, "A\0Z", sizeof(record.digest));
    lua_pop(L, 1);

    lua_getfield(L, -1, "samples");
    check_int_eq((int)lua_rawlen(L, -1), 2);
    lua_rawgeti(L, -1, 1);
    check_int_eq((int)lua_tointeger(L, -1), -2);
    lua_pop(L, 1);
    lua_rawgeti(L, -1, 2);
    check_int_eq((int)lua_tointeger(L, -1), 7);
    lua_pop(L, 2);

    lua_getfield(L, -1, "labels");
    lua_getfield(L, -1, "priority");
    check_int_eq((int)lua_tointeger(L, -1), 3);
    lua_pop(L, 2);

    lua_getfield(L, -1, "note");
    check_true(lua_isnil(L, -1));
    lua_pop(L, 1);
  }

  it("should transactionally read fixed arrays and maps from Lua") {
    LuaTypedRecord decoded;
    const LuaTypedMapEntry *entry;

    check_int_eq(TBE_TYPED_BIND_INIT(LuaTypedRecordType, &decoded, NULL), DATA_BIND_OK);
    check_int_eq(c11_lua_push_tbe_typed(L, &LuaTypedRecordType, &record, 8u),
                 DATA_BIND_OK);
    check_int_eq(c11_lua_read_tbe_typed(L, -1, &LuaTypedRecordType, &decoded,
                                        8u, 16u),
                 DATA_BIND_OK);
    check_int_eq(lua_gettop(L), 1);
    check_mem_eq(decoded.digest, "A\0Z", sizeof(decoded.digest));
    check_int_eq(decoded.samples[0], -2);
    check_int_eq(decoded.samples[1], 7);
    check_size_eq(LuaTypedMap_size(&decoded.labels), 1u);
    entry = LuaTypedMap_at_const(&decoded.labels, 0u);
    check_not_null(entry);
    if (entry != NULL) {
      check_str_eq(entry->key, "priority");
      check_int_eq(entry->value, 3);
    }
    TBE_TYPED_BIND_CLEAR(LuaTypedRecordType, &decoded);
  }
}
