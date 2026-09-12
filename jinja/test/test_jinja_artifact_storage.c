#include "jinja_cmeta_artifact.h"
#include "jinja_cmeta_environment.h"
#include "tinytest.h"

spec("Jinja compiled artifact accounting") {
  static JINJA_CMETA_TEMPLATE owner;
  static JINJA_CMETA_TEMPLATE *compiled;
  static JINJA_CMETA_TEMPLATE *named;
  static JINJA_CMETA_ENV *env;
  static void *storage;
  static JINJA_CMETA_ERROR error;
  static JINJA_CMETA_MEMORY memory;
  static JINJA_CMETA_TEMPLATE *bound;

  before_each() {
    owner = (JINJA_CMETA_TEMPLATE){.retained_bytes = sizeof(owner)};
    owner.allocator = (stl_allocator){NULL, jinja_cmeta_artifact_default_allocate,
        jinja_cmeta_artifact_default_deallocate};
    memory = (JINJA_CMETA_MEMORY){.limit = SIZE_MAX};
    bound = NULL;
    compiled = NULL;
    named = NULL;
    env = NULL;
    storage = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
  }
  after_each() {
    jinja_cmeta_artifact_clear(&owner);
    jinja_cmeta_artifact_destroy(bound);
    check_equal(memory.used, (size_t)0u);
    check_equal(memory.allocations, (size_t)0u);
    jinja_cmeta_release(named);
    jinja_cmeta_release(compiled);
    jinja_cmeta_env_destroy(env);
  }

  it("records allocated capacity independently of logical counts") {
    enum { ELEMENTS = 8 };
    storage = jinja_cmeta_artifact_zero(&owner, ELEMENTS, sizeof(uint64_t));
    check_not_null(storage);
    check_equal(owner.retained_bytes, sizeof(owner) + ELEMENTS * sizeof(uint64_t));
    check_equal(owner.cell_count, (size_t)0u);
    for (size_t i = 0u; i < ELEMENTS; ++i)
      check_equal(((const uint64_t *)storage)[i], UINT64_C(0));
  }

  it("rejects byte sum overflow without changing the recorded request") {
    owner.retained_bytes = SIZE_MAX - 1u;
    storage = jinja_cmeta_artifact_allocate(&owner, 2u, sizeof(char));
    check_null(storage);
    check_equal(owner.allocation_status, JINJA_CMETA_ERR_CAPACITY);
    check_equal(owner.retained_bytes, SIZE_MAX - 1u);
    check_null(jinja_cmeta_artifact_allocate(&owner, 1u, sizeof(char)));
    check_equal(owner.allocation_status, JINJA_CMETA_ERR_CAPACITY);
  }

  it("rejects multiplication overflow before allocating") {
    storage = jinja_cmeta_artifact_allocate(&owner, SIZE_MAX, sizeof(uint64_t));
    check_null(storage);
    check_equal(owner.allocation_status, JINJA_CMETA_ERR_CAPACITY);
    check_equal(owner.retained_bytes, sizeof(owner));
  }

  it("records all retained allocations of an empty artifact") {
    compiled = jinja_cmeta_compile(vstr_from_cstr(""), NULL, &error);
    check_not_null(compiled);
    check_equal(compiled->source_bytes, (size_t)0u);
    check_equal(compiled->lexical_scope_count, (size_t)1u);
    check_equal(compiled->expression_count, (size_t)0u);
    const size_t expected = sizeof(*compiled)
        + compiled->instruction_count * sizeof(*compiled->instructions)
        + strlen(compiled->program_strings) + 1u
        + sizeof(*compiled->lexical_scopes);
    check_equal(compiled->retained_bytes, expected);
    check_equal(compiled->allocation_status, JINJA_CMETA_OK);
  }

  it("preserves original source admission before trailing newline removal") {
    const vstr source = vstr_from_cstr("literal\r\n");
    compiled = jinja_cmeta_compile(source, NULL, &error);
    check_not_null(compiled);
    check_equal(compiled->source_bytes, source.len);
    check_equal(compiled->allocation_status, JINJA_CMETA_OK);
  }

  it("includes the owned diagnostic name in named artifacts") {
    const vstr source = vstr_from_cstr("{% set x = [1, 2] %}{{ x }}");
    const vstr name = vstr_from_cstr("directory/template.jinja");
    compiled = jinja_cmeta_compile(source, NULL, &error);
    check_not_null(compiled);
    env = jinja_cmeta_env_create(NULL, &error);
    check_not_null(env);
    named = jinja_cmeta_env_compile(env, name, source, &error);
    check_not_null(named);
    check_equal(named->source_bytes, source.len);
    check_equal(named->retained_bytes, compiled->retained_bytes + name.len + 1u);
    check_equal(named->allocation_status, JINJA_CMETA_OK);
  }

  it("admits the root object before publishing an allocator-bound owner") {
    const stl_allocator allocator = jinja_cmeta_memory_allocator(&memory);
    JINJA_CMETA_STATUS status;
    memory.limit = sizeof(JINJA_CMETA_TEMPLATE) - 1u;
    bound = jinja_cmeta_artifact_create(&allocator, &status);
    check_null(bound);
    check_equal(status, JINJA_CMETA_ERR_CAPACITY);
    check_equal(memory.used, (size_t)0u);
    memory.limit = sizeof(JINJA_CMETA_TEMPLATE);
    bound = jinja_cmeta_artifact_create(&allocator, &status);
    check_not_null(bound);
    check_equal(status, JINJA_CMETA_OK);
    check_equal(memory.used, sizeof(*bound));
  }

  it("returns retained arrays through a copied allocator on failed construction") {
    enum { PAYLOAD_BYTES = 16 };
    stl_allocator allocator = jinja_cmeta_memory_allocator(&memory);
    JINJA_CMETA_STATUS status;
    memory.limit = sizeof(JINJA_CMETA_TEMPLATE) + PAYLOAD_BYTES;
    bound = jinja_cmeta_artifact_create(&allocator, &status);
    check_not_null(bound);
    allocator = (stl_allocator){0};
    check_not_null(jinja_cmeta_artifact_allocate(bound, PAYLOAD_BYTES, sizeof(char)));
    check_equal(bound->retained_bytes, memory.limit);
    check_equal(memory.used, memory.limit);
    check_null(jinja_cmeta_artifact_allocate(bound, 1u, sizeof(char)));
    check_equal(bound->allocation_status, JINJA_CMETA_ERR_CAPACITY);
    check_equal(bound->allocation_count, (size_t)1u);
    jinja_cmeta_artifact_destroy(bound);
    bound = NULL;
    check_equal(memory.used, (size_t)0u);
    check_equal(memory.allocations, (size_t)0u);
  }

  it("bounds the registry without losing earlier allocation ownership") {
    for (size_t i = 0u; i < JINJA_CMETA_MAX_ARTIFACT_ALLOCATIONS; ++i)
      check_not_null(jinja_cmeta_artifact_allocate(&owner, 1u, sizeof(char)));
    const size_t retained = owner.retained_bytes;
    check_null(jinja_cmeta_artifact_allocate(&owner, 1u, sizeof(char)));
    check_equal(owner.allocation_status, JINJA_CMETA_ERR_CAPACITY);
    check_equal(owner.retained_bytes, retained);
    check_equal(owner.allocation_count, (size_t)JINJA_CMETA_MAX_ARTIFACT_ALLOCATIONS);
  }
}
