#include "data_bind_schema_error.h"
#include "data_bind_schema_parser.h"
#include "tinytest.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test schemas defined once outside benchmarks
static const char *SCHEMA_SMALL = "message SmallMsg { "
                                  "uint64 timestamp; "
                                  "uint32 value1; "
                                  "uint32 value2; "
                                  "}";

static const char *SCHEMA_MEDIUM = "composite Header { uint32 seq; uint64 ts; } "
                                   "message MediumMsg { "
                                   "Header header; "
                                   "uint64 field1; "
                                   "uint64 field2; "
                                   "uint64 field3; "
                                   "uint64 field4; "
                                   "}";

static const char *SCHEMA_LARGE = "group Level { uint64 price; uint32 qty; } "
                                  "message LargeMsg { "
                                  "uint64 timestamp; "
                                  "group<Level> bids; "
                                  "group<Level> asks; "
                                  "string symbol; "
                                  "}";

static const char *SCHEMA_ENUM = "enum Status <uint8> { "
                                 "Idle = 0; Active = 1; Pending = 2; "
                                 "Processing = 3; Completed = 4; Failed = 5; "
                                 "Cancelled = 6; Timeout = 7; Retry = 8; Unknown = 9; "
                                 "}";

static const char *SCHEMA_FLAGS = "flags Permissions { "
                                  "Read; Write; Execute; Delete; "
                                  "Create; Modify; Share; Admin; "
                                  "}";

static const char *SCHEMA_COMPLEX = "schema Market [id(1), version(1), byte_order(little)]; "
                                    "enum Side <uint8> { Buy = 1; Sell = 2; } "
                                    "flags OrderFlags { IOC; FOK; PostOnly; } "
                                    "composite Header { uint32 seq; uint64 ts; } "
                                    "group Level { uint64 price; uint32 qty; } "
                                    "message Order { "
                                    "Header header; "
                                    "uint64 order_id; "
                                    "Side side; "
                                    "OrderFlags order_flags; "
                                    "uint32 price; "
                                    "uint32 quantity; "
                                    "group<Level> levels; "
                                    "string symbol; "
                                    "}";

suite("tbe bench") {
#ifdef _WIN32
  system("chcp 65001 >nul"); // UTF-8
#endif
  bench("TBE Parser Performance") {
    benchmark("Parse small message (32 bytes)", 100000, 1.0) {
      Node *root = create_node_map("root");
      data_bind_schema_parse(SCHEMA_SMALL, strlen(SCHEMA_SMALL), root, NULL);
      node_free(root);
    }

    benchmark("Parse medium message (256 bytes)", 50000, 1.0) {
      Node *root = create_node_map("root");
      data_bind_schema_parse(SCHEMA_MEDIUM, strlen(SCHEMA_MEDIUM), root, NULL);
      node_free(root);
    }

    benchmark("Parse large message (4KB)", 10000, 1.0) {
      Node *root = create_node_map("root");
      data_bind_schema_parse(SCHEMA_LARGE, strlen(SCHEMA_LARGE), root, NULL);
      node_free(root);
    }

    benchmark("Parse enum with 10 items", 50000, 1.0) {
      Node *root = create_node_map("root");
      data_bind_schema_parse(SCHEMA_ENUM, strlen(SCHEMA_ENUM), root, NULL);
      node_free(root);
    }

    benchmark("Parse flags with 8 items", 50000, 1.0) {
      Node *root = create_node_map("root");
      data_bind_schema_parse(SCHEMA_FLAGS, strlen(SCHEMA_FLAGS), root, NULL);
      node_free(root);
    }

    benchmark("Parse complex schema (all features)", 5000, 1.0) {
      Node *root = create_node_map("root");
      data_bind_schema_parse(SCHEMA_COMPLEX, strlen(SCHEMA_COMPLEX), root, NULL);
      node_free(root);
    }

  }
}
