#ifndef TBE_CBIND_TEST_BENCHMARK_TBE_CBIND_FIXTURE_H
#define TBE_CBIND_TEST_BENCHMARK_TBE_CBIND_FIXTURE_H

#include "tbe_cbind_benchmark.h"

#include <cmeta/data.h>

#include <stddef.h>
#include <stdint.h>

/* Runtime plan creation consumes native C member names, before schema name overlays apply. */
static const cmeta_type_identity tbe_cbind_bench_header_identity =
    CMETA_TYPE_ID_ATOM_INIT("benchmark.tbe-cbind.header");
static const cmeta_type_desc tbe_cbind_bench_header_type = {"TbeCBindBenchHeader_t",
                                                            sizeof(TbeCBindBenchHeader_t),
                                                            _Alignof(TbeCBindBenchHeader_t),
                                                            CMETA_T_OBJECT,
                                                            NULL,
                                                            NULL,
                                                            &tbe_cbind_bench_header_identity};
static const cmeta_field_desc tbe_cbind_bench_header_layout_fields[] = {
    {"sequence", "int", offsetof(TbeCBindBenchHeader_t, sequence), sizeof(int32_t),
     _Alignof(int32_t), &cmeta_type_int, NULL}};
static const cmeta_struct_desc tbe_cbind_bench_header_layout = {
    "TbeCBindBenchHeader_t", sizeof(TbeCBindBenchHeader_t), _Alignof(TbeCBindBenchHeader_t),
    tbe_cbind_bench_header_layout_fields, 1u};
static const cmeta_data_field_desc tbe_cbind_bench_header_data_fields[] = {
    {"benchmark.tbe-cbind.header.sequence", "sequence", offsetof(TbeCBindBenchHeader_t, sequence),
     &cmeta_data_int}};
static const cmeta_data_struct_shape tbe_cbind_bench_header_shape = {
    &tbe_cbind_bench_header_layout, tbe_cbind_bench_header_data_fields, 1u};
static const cmeta_data_desc tbe_cbind_bench_header_data = {sizeof(cmeta_data_desc),
                                                            CMETA_DATA_DESC_ABI_VERSION,
                                                            "benchmark.tbe-cbind.header.data",
                                                            "TbeCBindBenchHeader_t native storage",
                                                            CMETA_DATA_STRUCT,
                                                            &tbe_cbind_bench_header_type,
                                                            &tbe_cbind_bench_header_shape,
                                                            NULL};

static const cmeta_type_identity tbe_cbind_bench_envelope_identity =
    CMETA_TYPE_ID_ATOM_INIT("benchmark.tbe-cbind.envelope");
static const cmeta_type_desc tbe_cbind_bench_envelope_type = {"TbeCBindBenchEnvelope_t",
                                                              sizeof(TbeCBindBenchEnvelope_t),
                                                              _Alignof(TbeCBindBenchEnvelope_t),
                                                              CMETA_T_OBJECT,
                                                              NULL,
                                                              NULL,
                                                              &tbe_cbind_bench_envelope_identity};
static const cmeta_field_desc tbe_cbind_bench_envelope_layout_fields[] = {
    {"header", "TbeCBindBenchHeader_t", offsetof(TbeCBindBenchEnvelope_t, header),
     sizeof(TbeCBindBenchHeader_t), _Alignof(TbeCBindBenchHeader_t), &tbe_cbind_bench_header_type,
     NULL},
    {"event_id", "int", offsetof(TbeCBindBenchEnvelope_t, event_id), sizeof(int32_t),
     _Alignof(int32_t), &cmeta_type_int, NULL},
    {"score", "double", offsetof(TbeCBindBenchEnvelope_t, score), sizeof(double), _Alignof(double),
     &cmeta_type_double, NULL}};
static const cmeta_struct_desc tbe_cbind_bench_envelope_layout = {
    "TbeCBindBenchEnvelope_t", sizeof(TbeCBindBenchEnvelope_t), _Alignof(TbeCBindBenchEnvelope_t),
    tbe_cbind_bench_envelope_layout_fields, 3u};
static const cmeta_data_field_desc tbe_cbind_bench_envelope_data_fields[] = {
    {"benchmark.tbe-cbind.envelope.header", "header", offsetof(TbeCBindBenchEnvelope_t, header),
     &tbe_cbind_bench_header_data},
    {"benchmark.tbe-cbind.envelope.event-id", "event_id",
     offsetof(TbeCBindBenchEnvelope_t, event_id), &cmeta_data_int},
    {"benchmark.tbe-cbind.envelope.score", "score", offsetof(TbeCBindBenchEnvelope_t, score),
     &cmeta_data_double}};
static const cmeta_data_struct_shape tbe_cbind_bench_envelope_shape = {
    &tbe_cbind_bench_envelope_layout, tbe_cbind_bench_envelope_data_fields, 3u};
static const cmeta_data_desc tbe_cbind_bench_envelope_data = {
    sizeof(cmeta_data_desc),
    CMETA_DATA_DESC_ABI_VERSION,
    "benchmark.tbe-cbind.envelope.data",
    "TbeCBindBenchEnvelope_t native storage",
    CMETA_DATA_STRUCT,
    &tbe_cbind_bench_envelope_type,
    &tbe_cbind_bench_envelope_shape,
    NULL};

#endif
