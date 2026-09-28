#include "database_schema.h"
#include "tinytest.h"

#include <string.h>

static Node *db_child(Node *parent, const char *name) {
  size_t i;
  if (parent == NULL || name == NULL) return NULL;
  if (parent->type == NODE_MAP) {
    for (i = 0u; i < parent->data.map.count; ++i) {
      Node *child = parent->data.map.items[i];
      if (child != NULL && child->name != NULL &&
          strcmp(child->name, name) == 0)
        return child;
    }
  }
  return NULL;
}

spec("typed database schema projection") {
  it("consumes canonical IDL annotations without parser-tree input") {
    static const char source[] =
        "schema Db [db_init(sqlite, \"INSERT INTO users (id) VALUES (1)\")];"
        "[db_table(users)] message User {"
        "  [db_primary_key(1)] int64 id;"
        "  string name;"
        "}";
    IdlContract *contract = NULL;
    IdlDiagnostic idl_error = IDL_DIAGNOSTIC_INIT;
    tbe_database_schema_diagnostic_t db_error = {{0}};
    Node *database_ir = NULL;
    Node *tables;
    Node *initializers;

    check_true(idl_contract_parse(
        source, sizeof(source) - 1u, &contract, &idl_error));
    check_not_null(contract);
    if (contract == NULL) return;

    check_equal(
        idl_annotation_count(
            contract->annotations, contract->annotation_count, "db_init"),
        (size_t)1u);
    check_equal(
        tbe_database_schema_build_contract(
            contract, TBE_DATABASE_DIALECT_SQLITE,
            &database_ir, &db_error),
        TBE_DATABASE_SCHEMA_STATUS_OK);
    check_not_null(database_ir);
    if (database_ir != NULL) {
      tables = db_child(database_ir, "db_tables");
      initializers = db_child(database_ir, "db_initializers");
      check_not_null(tables);
      check_not_null(initializers);
      if (tables != NULL)
        check_equal(tables->data.list.count, (size_t)1u);
      if (initializers != NULL)
        check_equal(initializers->data.list.count, (size_t)1u);
    }

    tbe_database_schema_destroy(database_ir);
    idl_contract_destroy(contract);
  }
}
