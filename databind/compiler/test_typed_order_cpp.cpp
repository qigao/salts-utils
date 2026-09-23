#include "typed_order.h"

#include "tinytest.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

struct CppMacroRecord {
  std::uint32_t id;
};

DATA_BIND_TYPED_DEFINE_STRUCT(CPP_MACRO_RECORD_BINDING, CppMacroRecord, "CppMacroRecord",
                        DATA_BIND_TYPED_FIELD(CppMacroRecord, id, "id", DATA_BIND_TYPED_U32,
                                        DATA_BIND_TYPED_REQUIRED));

spec("generated typed Order C++ owner") {
  it("should expose the C struct API through an owning RAII wrapper") {
    static_assert(!std::is_copy_constructible_v<Orders_typed::OrderOwner>);
    static_assert(!std::is_move_constructible_v<Orders_typed::OrderOwner>);

    const char *json = "{\"header\":{\"seq\":7},\"id\":42,"
                       "\"request_id\":\"01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001\","
                       "\"min_value\":-9223372036854775808,\"max_value\":18446744073709551615,"
                       "\"side\":\"Buy\",\"routing_hint\":12,\"fills\":[],\"symbol\":\"ABC\","
                       "\"client_tag\":\"cpp\",\"payload\":\"raw\"}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = nullptr;
    Orders_typed::OrderOwner order;

    check_equal(Orders_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != nullptr) {
      char *serialized = nullptr;
      std::uint8_t *wire = nullptr;
      std::size_t serialized_len = 0;
      std::size_t wire_len = 0;
      Orders_typed::OrderOwner decoded;
      check_equal(order.from_json(codec, json, std::strlen(json), &error), DATA_BIND_OK);
      check_equal(order->order_id, 42u);
      check_equal(order->header.seq, 7u);
      check_equal(order->symbol, "ABC");
      check_equal(order->_presence[0],
                    (1u << Order_OPTIONAL_routing_hint) | (1u << Order_OPTIONAL_client_tag));
      check_equal(order->routing_hint, 12u);
      check_equal(order->client_tag, "cpp");
      check_equal(order.to_json(codec, &serialized, &serialized_len, &error), DATA_BIND_OK);
      check_not_null(serialized);
      check_greater(serialized_len, 0);
      check_equal(order.to_bin(&wire, &wire_len, &error), DATA_BIND_OK);
      check_not_null(wire);
      check_greater(wire_len, 0);
      if (wire != nullptr) {
        check_equal(decoded.from_bin(codec, wire, wire_len, &error), DATA_BIND_OK);
        check_equal(decoded->order_id, 42u);
        check_equal(decoded->symbol, "ABC");
        check_equal(decoded->_presence[0], order->_presence[0]);
        check_equal(decoded->routing_hint, 12u);
        check_equal(decoded->client_tag, "cpp");
      }
      data_bind_typed_serialized_free(serialized);
      data_bind_typed_serialized_free(wire);
    }
    data_bind_free(codec);
  }

  it("should compile header-only struct bindings in C++") {
    DataBindError error = DATA_BIND_ERROR_INIT;
    CppMacroRecord record;

    check_equal(DATA_BIND_TYPED_BIND_INIT(CPP_MACRO_RECORD_BINDING, &record, &error), DATA_BIND_OK);
    record.id = 7;
    check_equal(CPP_MACRO_RECORD_BINDING.name, "CppMacroRecord");
    check_equal(CPP_MACRO_RECORD_BINDING.size, sizeof(CppMacroRecord));
    check_equal(record.id, 7u);
    DATA_BIND_TYPED_BIND_CLEAR(CPP_MACRO_RECORD_BINDING, &record);
  }
}
