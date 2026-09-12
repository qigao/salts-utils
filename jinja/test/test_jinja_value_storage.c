#include "jinja_cmeta_values.h"
#include "jinja_cmeta_cells.h"
#include "tinytest.h"

spec("Jinja retained VALUE spans") {
  static JINJA_CMETA_VALUE_STORE store;
  static JINJA_CMETA_MEMORY memory;
  static JINJA_CMETA_CELL_STORE cells;
  static JINJA_CMETA_TEMPLATE *compiled;
  before_each() {
    memory = (JINJA_CMETA_MEMORY){.limit = SIZE_MAX};
    store = (JINJA_CMETA_VALUE_STORE){.memory = &memory};
    cells = (JINJA_CMETA_CELL_STORE){0};
    compiled = NULL;
  }
  after_each() {
    jinja_cmeta_cells_destroy(&cells);
    jinja_cmeta_values_destroy(&store);
    jinja_cmeta_release(compiled);
    check_equal(memory.used, (size_t)0u);
    check_equal(memory.allocations, (size_t)0u);
  }

  it("admits empty spans without storage and rejects nonempty zero quotas") {
    check_equal(jinja_cmeta_values_prepare(&store, 0u, 0u), JINJA_CMETA_OK);
    check_equal(vec_size(vec_alloc_view(store.chunks)), (size_t)0u);
    check_equal(jinja_cmeta_values_prepare(&store, 0u, 1u), JINJA_CMETA_ERR_CAPACITY);
    check_equal(jinja_cmeta_values_prepare(&store, 1u, 0u), JINJA_CMETA_ERR_CAPACITY);
    check_equal(vec_size(vec_alloc_view(store.chunks)), (size_t)0u);
    check_null(jinja_cmeta_values_at(&store, 0u));
  }

  it("rejects a zero byte budget without retaining an empty directory") {
    store.limit = 1u;
    memory.limit = 0u;
    check_equal(jinja_cmeta_values_prepare(&store, 0u, 1u), JINJA_CMETA_ERR_CAPACITY);
    check_null(store.chunks);
    check_equal(memory.used, (size_t)0u);
  }

  it("shares one byte ledger across snapshots and activation cells") {
    enum { CELL_LIMIT = 16, ACTIVATION_LIMIT = 2, VALUE_LIMIT = 2 };
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    compiled = jinja_cmeta_compile(vstr_from_cstr("{% set x=1 %}"), NULL, &error);
    check_not_null(compiled);
    check_equal(jinja_cmeta_cells_init(&cells, compiled, ACTIVATION_LIMIT, CELL_LIMIT), JINJA_CMETA_OK);
    cells.memory = &memory;
    store.limit = VALUE_LIMIT;
    check_equal(jinja_cmeta_values_prepare(&store, 0u, 1u), JINJA_CMETA_OK);
    const size_t snapshot_bytes = memory.used;
    memory.limit = memory.used;
    JINJA_CMETA_ACTIVATION *activation = NULL;
    check_equal(jinja_cmeta_activation_new(&cells, NULL, 0u, &activation), JINJA_CMETA_ERR_CAPACITY);
    check_null(activation);
    check_equal(cells.activation_count, (size_t)0u);
    check_equal(cells.cell_count, (size_t)0u);
    check_equal(memory.used, snapshot_bytes);
    memory.limit = SIZE_MAX;
    check_equal(jinja_cmeta_activation_new(&cells, NULL, 0u, &activation), JINJA_CMETA_OK);
    check_not_null(activation);
    check_equal(memory.used > snapshot_bytes, true);
    const size_t retained = memory.used;
    memory.limit = retained;
    check_equal(jinja_cmeta_values_prepare(&store, 1u, 1u), JINJA_CMETA_ERR_CAPACITY);
    check_equal(memory.used, retained);
    check_equal(vec_size(vec_alloc_view(store.chunks)), (size_t)1u);
    jinja_cmeta_cells_destroy(&cells);
    check_equal(memory.used, snapshot_bytes);
  }

  it("preserves a context when its growth peak exceeds the shared byte budget") {
    enum { CONTEXT_SLOTS = 32 };
    store.limit = CONTEXT_SLOTS;
    check_equal(jinja_cmeta_values_prepare_context(&store, 0u, 0u), JINJA_CMETA_OK);
    JINJA_CMETA_VALUE_CHUNK *chunk = (JINJA_CMETA_VALUE_CHUNK *)vec_alloc_at(store.chunks, 0u);
    const size_t capacity = vec_capacity(vec_alloc_view(chunk->values));
    for (size_t i = JINJA_CMETA_VALUE_PAIR_WIDTH; i < capacity; i += JINJA_CMETA_VALUE_PAIR_WIDTH)
      check_equal(jinja_cmeta_values_prepare_context(&store, i, i), JINJA_CMETA_OK);
    JINJA_CMETA_VALUE *saved = jinja_cmeta_values_at(&store, 0u);
    check_not_null(saved);
    saved->integer = CONTEXT_SLOTS;
    const size_t retained = memory.used;
    memory.limit = retained;
    check_equal(jinja_cmeta_values_prepare_context(&store, capacity, capacity), JINJA_CMETA_ERR_CAPACITY);
    check_equal(jinja_cmeta_values_at(&store, 0u) == saved, true);
    check_equal(saved->integer, (int64_t)CONTEXT_SLOTS);
    check_equal(memory.used, retained);
    check_equal(chunk->end, capacity);
  }

  it("rejects one slot beyond capacity without changing a published snapshot") {
    enum { SLOT_LIMIT = 4, RETAINED_INTEGER = 17 };
    store.limit = SLOT_LIMIT;
    check_equal(jinja_cmeta_values_prepare(&store, 0u, SLOT_LIMIT), JINJA_CMETA_OK);
    JINJA_CMETA_VALUE *saved = jinja_cmeta_values_at(&store, 0u);
    check_not_null(saved);
    saved->kind = JINJA_CMETA_VALUE_INTEGER;
    saved->integer = RETAINED_INTEGER;
    check_equal(jinja_cmeta_values_prepare(&store, SLOT_LIMIT, 1u), JINJA_CMETA_ERR_CAPACITY);
    check_equal(jinja_cmeta_values_at(&store, 0u) == saved, true);
    check_equal(saved->integer, (int64_t)RETAINED_INTEGER);
    check_equal(vec_size(vec_alloc_view(store.chunks)), (size_t)1u);
  }

  it("retains every snapshot while the chunk directory grows") {
    enum { SLOT_LIMIT = 64 };
    store.limit = SLOT_LIMIT;
    JINJA_CMETA_VALUE *addresses[SLOT_LIMIT] = {0};
    for (size_t i = 0u; i < SLOT_LIMIT; ++i) {
      check_equal(jinja_cmeta_values_prepare(&store, i, 1u), JINJA_CMETA_OK);
      addresses[i] = jinja_cmeta_values_at(&store, i);
      check_not_null(addresses[i]);
      addresses[i]->kind = JINJA_CMETA_VALUE_INTEGER;
      addresses[i]->integer = (int64_t)i;
    }
    for (size_t i = 0u; i < SLOT_LIMIT; ++i) {
      check_equal(jinja_cmeta_values_at(&store, i) == addresses[i], true);
      check_equal(addresses[i]->integer, (int64_t)i);
    }
  }

  it("keeps parent and nested collection spans contiguous and independent") {
    enum { PAIR_WIDTH = 2, SLOT_LIMIT = 4, CHILD_INTEGER = 23 };
    store.limit = SLOT_LIMIT;
    check_equal(jinja_cmeta_values_prepare(&store, 0u, PAIR_WIDTH), JINJA_CMETA_OK);
    JINJA_CMETA_VALUE *parent = jinja_cmeta_values_at(&store, 0u);
    check_equal(jinja_cmeta_values_prepare(&store, PAIR_WIDTH, PAIR_WIDTH), JINJA_CMETA_OK);
    JINJA_CMETA_VALUE *child = jinja_cmeta_values_at(&store, PAIR_WIDTH);
    check_not_null(parent);
    check_not_null(child);
    parent[0] = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_LIST,
        .collection_values = child, .collection_item_count = PAIR_WIDTH};
    child[1] = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = CHILD_INTEGER};
    check_equal(jinja_cmeta_values_at(&store, 1u) == parent + 1u, true);
    check_equal(jinja_cmeta_values_at(&store, SLOT_LIMIT - 1u) == child + 1u, true);
    check_equal(parent[0].collection_values[1].integer, (int64_t)CHILD_INTEGER);
  }

  it("reuses an unused reservation tail without relocating its prefix") {
    enum { RESERVED_SLOTS = 3, NEXT_SLOTS = 2, SLOT_LIMIT = 5 };
    store.limit = SLOT_LIMIT;
    check_equal(jinja_cmeta_values_prepare(&store, 0u, RESERVED_SLOTS), JINJA_CMETA_OK);
    JINJA_CMETA_VALUE *saved = jinja_cmeta_values_at(&store, 0u);
    check_equal(jinja_cmeta_values_prepare(&store, RESERVED_SLOTS - 1u, 1u), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_values_at(&store, RESERVED_SLOTS - 1u) == saved + RESERVED_SLOTS - 1u, true);
    check_equal(vec_size(vec_alloc_view(store.chunks)), (size_t)1u);
    check_equal(jinja_cmeta_values_prepare(&store, RESERVED_SLOTS, NEXT_SLOTS), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_values_at(&store, 0u) == saved, true);
    check_equal(vec_size(vec_alloc_view(store.chunks)), (size_t)2u);
  }

  it("preserves context entries during growth and freezes them before later snapshots") {
    enum { CONTEXT_SLOTS = 34, SLOT_LIMIT = CONTEXT_SLOTS + 1 };
    store.limit = SLOT_LIMIT;
    for (size_t i = 0u; i < CONTEXT_SLOTS; i += JINJA_CMETA_VALUE_PAIR_WIDTH) {
      check_equal(jinja_cmeta_values_prepare_context(&store, i, i), JINJA_CMETA_OK);
      JINJA_CMETA_VALUE *entries = jinja_cmeta_values_at(&store, 0u);
      check_not_null(entries);
      for (size_t j = 0u; j < i; ++j) check_equal(entries[j].integer, (int64_t)j);
      entries[i] = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = (int64_t)i};
      entries[i + 1u] = (JINJA_CMETA_VALUE){.kind = JINJA_CMETA_VALUE_INTEGER, .integer = (int64_t)(i + 1u)};
    }
    JINJA_CMETA_VALUE *published = jinja_cmeta_values_at(&store, 0u);
    check_equal(jinja_cmeta_values_prepare(&store, CONTEXT_SLOTS, 1u), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_values_at(&store, 0u) == published, true);
    for (size_t i = 0u; i < CONTEXT_SLOTS; ++i)
      check_equal(published[i].integer, (int64_t)i);
  }

  it("rejects an incomplete context pair without changing retained entries") {
    enum { SLOT_LIMIT = 3, RETAINED_INTEGER = 29 };
    store.limit = SLOT_LIMIT;
    check_equal(jinja_cmeta_values_prepare_context(&store, 0u, 0u), JINJA_CMETA_OK);
    JINJA_CMETA_VALUE *entries = jinja_cmeta_values_at(&store, 0u);
    check_not_null(entries);
    entries[0].integer = RETAINED_INTEGER;
    check_equal(jinja_cmeta_values_prepare_context(&store,
        JINJA_CMETA_VALUE_PAIR_WIDTH, JINJA_CMETA_VALUE_PAIR_WIDTH), JINJA_CMETA_ERR_CAPACITY);
    check_equal(jinja_cmeta_values_at(&store, 0u) == entries, true);
    check_equal(entries[0].integer, (int64_t)RETAINED_INTEGER);
    check_null(jinja_cmeta_values_at(&store, JINJA_CMETA_VALUE_PAIR_WIDTH));
  }

  it("rejects unrepresentable quotas before allocating a directory") {
    store.limit = SIZE_MAX;
    check_equal(jinja_cmeta_values_prepare(&store, 0u, 1u), JINJA_CMETA_ERR_CAPACITY);
    check_equal(vec_size(vec_alloc_view(store.chunks)), (size_t)0u);
    check_null(store.chunks);
  }
}
