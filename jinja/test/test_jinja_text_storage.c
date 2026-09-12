#include "jinja_cmeta_text.h"
#include "tinytest.h"

spec("Jinja output storage accounting") {
  static JINJA_CMETA_MEMORY memory;
  static JINJA_CMETA_TEXT text;
  static void *copy;
  static size_t copy_bytes;
  static int transferred;

  before_each() {
    memory = (JINJA_CMETA_MEMORY){.limit = SIZE_MAX};
    text = (JINJA_CMETA_TEXT){0};
    copy = NULL;
    copy_bytes = 0u;
    transferred = 0;
  }
  after_each() {
    if (copy != NULL) {
      if (transferred) free(copy);
      else jinja_cmeta_memory_deallocate(&memory, copy, copy_bytes);
    }
    jinja_cmeta_text_destroy(&text);
    check_equal(memory.used, (size_t)0u);
    check_equal(memory.allocations, (size_t)0u);
  }

  it("rejects zero quota without publishing an owner") {
    memory.limit = 0u;
    check_equal(jinja_cmeta_text_init(&text, &memory), JINJA_CMETA_ERR_CAPACITY);
    check_null(text.bytes);
    check_equal(memory.peak, (size_t)0u);
  }

  it("admits an empty terminator at the exact initialization budget") {
    check_equal(jinja_cmeta_text_init(&text, &memory), JINJA_CMETA_OK);
    const size_t required = memory.peak;
    jinja_cmeta_text_destroy(&text);
    memory.limit = required - 1u;
    check_equal(jinja_cmeta_text_init(&text, &memory), JINJA_CMETA_ERR_CAPACITY);
    check_null(text.bytes);
    check_equal(memory.used, (size_t)0u);
    memory.limit = required;
    check_equal(jinja_cmeta_text_init(&text, &memory), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_text_length(&text), (size_t)0u);
    check_equal(jinja_cmeta_text_data(&text)[0], '\0');
    check_equal(memory.used, required);
  }

  it("preserves embedded NUL bytes and appends a separate terminator") {
    const char payload[] = {'a', '\0', 'b'};
    check_equal(jinja_cmeta_text_init(&text, &memory), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_text_append(&text, payload, sizeof(payload)), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_text_append(&text, "c", 1u), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_text_length(&text), sizeof(payload) + 1u);
    check_equal(memcmp(jinja_cmeta_text_data(&text), payload, sizeof(payload)), 0);
    check_equal(jinja_cmeta_text_data(&text)[sizeof(payload)], 'c');
    check_equal(jinja_cmeta_text_data(&text)[sizeof(payload) + 1u], '\0');
  }

  it("preserves bytes and views when growth admission fails") {
    enum { GROWTH_BYTES = 256 };
    const char payload[GROWTH_BYTES] = {0};
    check_equal(jinja_cmeta_text_init(&text, &memory), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_text_append(&text, "abc", 3u), JINJA_CMETA_OK);
    check_equal(vec_capacity(vec_alloc_view(text.bytes)) < sizeof(payload), true);
    const char *saved = jinja_cmeta_text_data(&text);
    const size_t retained = memory.used;
    memory.limit = retained;
    check_equal(jinja_cmeta_text_append(&text, payload, sizeof(payload)), JINJA_CMETA_ERR_CAPACITY);
    check_equal(jinja_cmeta_text_data(&text) == saved, true);
    check_equal(jinja_cmeta_text_length(&text), (size_t)3u);
    check_equal(memcmp(saved, "abc", sizeof("abc")), 0);
    check_equal(memory.used, retained);
  }

  it("includes the old buffer in growth peak admission") {
    enum { GROWTH_BYTES = 256 };
    const char payload[GROWTH_BYTES] = {0};
    check_equal(jinja_cmeta_text_init(&text, &memory), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_text_append(&text, payload, sizeof(payload)), JINJA_CMETA_OK);
    const size_t required_peak = memory.peak;
    check_equal(required_peak > memory.used, true);
    jinja_cmeta_text_destroy(&text);
    check_equal(jinja_cmeta_text_init(&text, &memory), JINJA_CMETA_OK);
    memory.limit = required_peak - 1u;
    check_equal(jinja_cmeta_text_append(&text, payload, sizeof(payload)), JINJA_CMETA_ERR_CAPACITY);
    check_equal(jinja_cmeta_text_length(&text), (size_t)0u);
    memory.limit = required_peak;
    check_equal(jinja_cmeta_text_append(&text, payload, sizeof(payload)), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_text_length(&text), sizeof(payload));
  }

  it("rejects oversized appends before reading input or changing content") {
    check_equal(jinja_cmeta_text_init(&text, &memory), JINJA_CMETA_OK);
    const size_t retained = memory.used;
    check_equal(jinja_cmeta_text_append(&text, "", SIZE_MAX), JINJA_CMETA_ERR_CAPACITY);
    check_equal(jinja_cmeta_text_append(&text, NULL, 1u), JINJA_CMETA_ERR_INVALID_ARGUMENT);
    check_equal(jinja_cmeta_text_append(&text, NULL, 0u), JINJA_CMETA_OK);
    check_equal(memory.used, retained);
    check_equal(jinja_cmeta_text_length(&text), (size_t)0u);
    check_equal(jinja_cmeta_text_data(&text)[0], '\0');
  }

  it("admits a result copy only when both live allocations fit") {
    check_equal(jinja_cmeta_text_init(&text, &memory), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_text_append(&text, "result", 6u), JINJA_CMETA_OK);
    const size_t retained = memory.used;
    copy_bytes = jinja_cmeta_text_length(&text) + 1u;
    memory.limit = retained + copy_bytes - 1u;
    check_equal(jinja_cmeta_memory_allocate(&memory, copy_bytes, &copy), STL_CAPACITY_EXCEEDED);
    check_null(copy);
    check_equal(memory.used, retained);
    memory.limit = retained + copy_bytes;
    check_equal(jinja_cmeta_memory_allocate(&memory, copy_bytes, &copy), STL_OK);
    memcpy(copy, jinja_cmeta_text_data(&text), copy_bytes);
    check_equal(memory.used, retained + copy_bytes);
    check_equal(memory.peak, retained + copy_bytes);
    check_equal(memcmp(copy, "result", copy_bytes), 0);
  }

  it("transfers a malloc compatible copy beyond the ledger lifetime") {
    check_equal(jinja_cmeta_text_init(&text, &memory), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_text_append(&text, "owned", 5u), JINJA_CMETA_OK);
    copy_bytes = jinja_cmeta_text_length(&text) + 1u;
    check_equal(jinja_cmeta_memory_allocate(&memory, copy_bytes, &copy), STL_OK);
    memcpy(copy, jinja_cmeta_text_data(&text), copy_bytes);
    jinja_cmeta_memory_disown(&memory, copy_bytes);
    transferred = 1;
    jinja_cmeta_text_destroy(&text);
    check_equal(memory.used, (size_t)0u);
    check_equal(memory.allocations, (size_t)0u);
    check_equal(memcmp(copy, "owned", copy_bytes), 0);
  }

  it("admits measured format scratch before exposing writable bytes") {
    enum { SCRATCH_BYTES = 256 };
    check_equal(jinja_cmeta_text_init(&text, &memory), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_text_append(&text, "old", 3u), JINJA_CMETA_OK);
    const size_t retained = memory.used;
    memory.limit = retained;
    check_equal(jinja_cmeta_text_resize(&text, SCRATCH_BYTES), JINJA_CMETA_ERR_CAPACITY);
    check_equal(jinja_cmeta_text_length(&text), (size_t)3u);
    check_equal(memcmp(jinja_cmeta_text_data(&text), "old", sizeof("old")), 0);
    check_equal(memory.used, retained);
    memory.limit = SIZE_MAX;
    check_equal(jinja_cmeta_text_resize(&text, SCRATCH_BYTES), JINJA_CMETA_OK);
    memset(jinja_cmeta_text_mutable(&text), 'x', SCRATCH_BYTES);
    check_equal(jinja_cmeta_text_data(&text)[SCRATCH_BYTES], '\0');
    check_equal(jinja_cmeta_text_resize(&text, 1u), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_text_data(&text)[0], 'x');
    check_equal(jinja_cmeta_text_data(&text)[1], '\0');
    check_equal(jinja_cmeta_text_resize(&text, SIZE_MAX), JINJA_CMETA_ERR_CAPACITY);
    check_equal(jinja_cmeta_text_length(&text), (size_t)1u);
  }
}
