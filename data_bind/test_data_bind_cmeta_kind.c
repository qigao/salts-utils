#include "data_bind_cmeta.h"
#include "tinytest.h"

/* Keep kind conformance independent of parser/storage tests: these assertions
 * exercise the production compatibility mapping, not a test-side lowering. */
suite("DataBind compatibility kind mapping") {
  it("maps every legacy value kind to its canonical CMeta semantic") {
    static const struct { DataBindValueKind value; cmeta_data_kind semantic; } cases[] = {
      {DATA_BIND_VALUE_OBJECT, CMETA_DATA_STRUCT}, {DATA_BIND_VALUE_LIST, CMETA_DATA_SEQUENCE},
      {DATA_BIND_VALUE_SET, CMETA_DATA_SET}, {DATA_BIND_VALUE_MAP, CMETA_DATA_MAP},
      {DATA_BIND_VALUE_INT, CMETA_DATA_SINT}, {DATA_BIND_VALUE_INT64, CMETA_DATA_SINT},
      {DATA_BIND_VALUE_UINT64, CMETA_DATA_UINT}, {DATA_BIND_VALUE_DOUBLE, CMETA_DATA_FLOAT},
      {DATA_BIND_VALUE_BOOL, CMETA_DATA_BOOL}, {DATA_BIND_VALUE_STRING, CMETA_DATA_STRING},
      {DATA_BIND_VALUE_BYTES, CMETA_DATA_BYTES}, {DATA_BIND_VALUE_UUID, CMETA_DATA_CUSTOM},
      {DATA_BIND_VALUE_DATETIME, CMETA_DATA_CUSTOM}, {DATA_BIND_VALUE_DATE, CMETA_DATA_CUSTOM},
      {DATA_BIND_VALUE_TIME, CMETA_DATA_CUSTOM}, {DATA_BIND_VALUE_DURATION, CMETA_DATA_CUSTOM},
      {DATA_BIND_VALUE_DECIMAL, CMETA_DATA_CUSTOM}, {DATA_BIND_VALUE_BIGINT, CMETA_DATA_CUSTOM},
      {DATA_BIND_VALUE_MONEY, CMETA_DATA_CUSTOM}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      cmeta_data_kind kind = CMETA_DATA_BOOL;
      check_equal(data_bind_cmeta_data_kind(cases[i].value, &kind), DATA_BIND_OK);
      check_equal(kind, cases[i].semantic);
    }
  }
  it("rejects null and invalid dynamic kinds without publishing a CMeta kind") {
    cmeta_data_kind kind = CMETA_DATA_MAP;
    check_equal(data_bind_cmeta_data_kind(DATA_BIND_VALUE_NULL, &kind), DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(kind, CMETA_DATA_MAP);
    check_equal(data_bind_cmeta_data_kind((DataBindValueKind)999, &kind), DATA_BIND_ERR_INVALID_ARG);
    check_equal(kind, CMETA_DATA_MAP);
    check_equal(data_bind_cmeta_data_kind(DATA_BIND_VALUE_BOOL, NULL), DATA_BIND_ERR_INVALID_ARG);
  }
}
