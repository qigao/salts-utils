# tbe_compiler Command Line Options

## Overview

`tbe_compiler` generates code and DSL declarations from TBE schema files for multiple target languages and RulesForge integration.

## Basic Usage

```bash
tbe_compiler <schema_file> [options]
```

## Options

### Required

- `<schema_file>`
  - Path to the `.schema` definition file
  - Example: `order.schema`

### Language Output

- `--output <file>` or `-o <file>`
  - Output file path for generated code
  - Default: stdout
  - Example: `--output order.h`

- `--lang <language>` or `-l <language>`
  - Target source language
  - Options: `c`, `cpp`, `go`, `rust`, `python`, `py`, `ts`, `typescript`
  - Default: `c`
  - Example: `--lang c`

- `--template <file>` or `-t <file>`
  - Path to custom Mustache template file
  - Overrides `--lang` option
  - Example: `--template my_template.mustache`

- `--source-output <file>` or `-s <file>`
  - With the built-in C generator, emits the typed serialization companion `.c` file
  - Requires `--output`; custom templates and non-C languages are rejected
  - The generated header exposes strong record types plus binary/JSON/YAML/CSV/XML APIs
  - Example: `--output order.h --source-output order.c`

- `--cbind-output <file>`
  - With the built-in C generator, emits an immutable CMeta/CBind semantic sidecar `.c` file
  - Requires both `--output` and `--source-output`; custom templates and non-C languages are rejected
  - Its path must differ from the header, typed source, guest, Lua, and DSL outputs
  - The generated header declares each record's semantic descriptor accessor and CSerde decode façade
  - The generated source contains static descriptors only; the caller owns initialized decoded records
  - The sidecar schema accepts only `int32`, `int64`, `uint64`, `float`, `double`, owning
    `string`, and nested composite/group/message records; aliases, optional fields, and other
    storage forms fail before any output file is written
  - Example: `--output order.h --source-output order.c --cbind-output order_cbind.c`

  典型的完整调用如下；三个路径必须不同，未请求该选项时生成 header 与 typed source
  不会引入 CBind：

  ```powershell
  tbe_compiler order.schema --lang c `
    --output generated/order.h `
    --source-output generated/order.c `
    --cbind-output generated/order_cbind.c
  ```

  sidecar 中的 `Type_cbind_data()` 返回 immutable CMeta semantic descriptor，
  `Type_from_cserde(context, reader, object, error)` 将 format-neutral CSerde token
  decode 到已由 `Type_init()` 初始化的 owning object；成功或失败后都由调用方调用
  `Type_clear()`。该 descriptor 不含 `TbeTypedType` 的 TBE wire/layout metadata。
  `[name]` 选择 CSerde map key，`[c]` 只选择 C member offset。CBind v1 仅接受
  int32/int64/uint64、float、double、owning string 与嵌套 record；alias、optional、bool、
  其他整数、enum、uuid、bytes、containers 与 unions 在写 output 前 fail fast。生成的
  int64 mapping 断言 `int64_t` 与 `long` 的大小和对齐一致，因此 LLP64 Windows 上会编译
  失败；以目标 ABI 编译 generated sidecar（例如 MSVC Release CI）验证这一防线。

- `--guest-output <file>` or `-g <file>`
  - With the built-in C generator, emits a Wasm-friendly guest adapter `.c` file
  - Requires `--output`; custom templates and non-C languages are rejected
  - The adapter converts JSON/YAML/CSV/XML byte slices to generated wire views, and back,
    through a caller-provided `tbe_guest_bridge_t`
  - The adapter performs no parsing and owns no buffers; the runtime bridge controls
    schema registration, sandbox policy, quotas, and provider errors
  - Example: `--output order.h --guest-output order_guest.c`

- `--lua-output <file>`
  - With the built-in C generator, emits typed C-to-Lua adapter functions
  - Requires both `--output` and `--source-output`; custom templates and non-C languages are rejected
  - Each owning record receives `Type_push_lua` and transactional `Type_from_lua` adapters
  - The generated source includes the C binding header `turbo_lua.h`; link
    the consumer with `turbo_lua_bind` (`TurboParser::LuaBind` in the build tree)
  - C++ consumers may include `turbo_lua.hpp` to combine the same C/DataBind
    adapters with function, class, property, inheritance, and reflection binding
  - Example: `--output order.h --source-output order.c --lua-output order_lua.c`
  - A request message annotated with
    `[lua_operation(create_order), lua_response(OrderResult)]` also generates a
    typed callback entry and a schema module constructor. The caller supplies
    the callback table and resource limits; expected failures return Lua
    `nil, { code, path, message }`. Generated glue initializes and clears
    request/response objects, so callbacks fill the response without retaining
    either pointer.
  - `lua_async(future)` is the only asynchronous Lua binding mode. Add it to an
    operation when the application has an explicit asynchronous state machine.
    The generated starter returns opaque
    `state`, `poll`, and `destroy` hooks. Lua sees the same `poll()`, `await()`,
    `done()`, and `cancel()` API. `await()` is called inside a yieldable Lua
    coroutine and yields the Future to its host until that coroutine is resumed.
    No stackful C coroutine or separate coroutine stack is allocated.
    `max_pending_operations` bounds live Futures. The generated Future invokes
    `poll` only on the Lua owner
    thread and invokes `destroy` exactly once on completion, failure, or
    cancellation. External workers may update application state, but must not
    touch Lua or the generated Future.
  - `lua_import` is no longer supported. Bind Lua functions and coroutines
    explicitly through the C11 Lua API.

### DSL Integration (RulesForge)

- `--dsl-output <file>` or `-d <file>`
  - Generate DSL type declarations (.rfl file)
  - Contains `declare` statements for use in RulesForge
  - Example: `--dsl-output order.rfl`

## Usage Examples

### Example 1: Generate C Header Only

```bash
tbe_compiler order.schema --output order.h
```

### Example 2: Generate DSL Type Declarations

```bash
tbe_compiler order.schema --dsl-output order.rfl
```

### Example 2a: Generate Strong Typed C Bindings

```bash
tbe_compiler order.schema --lang c --output order.h --source-output order.c
```

Add `--lua-output order_lua.c` to generate direct Lua table adapters from the same typed
metadata. The adapters omit absent optional fields, preserve binary strings, use 1-based
Lua arrays, bound recursion and each dynamic string/container extent, and reject integers that cannot fit
`lua_Integer`. `Type_from_lua` accepts canonical descriptor keys, rejects unknown keys, and
replaces an initialized destination only after the complete table has been converted.

The generated API includes `Order_t`, `Order_init`/`Order_clear`, schema codec creation,
and `Order_from_*`/`Order_to_*` functions for `bin`, `json`, `yaml`, `csv`, and `xml`.
Optional fields receive a generated `_presence` bitmap, stable per-field bit constants, and
bounded view/builder helpers. Text decoding sets presence only for supplied fields, text
serialization omits absent fields, and binary round trips preserve the bitmap.
An owning C member can be renamed without changing its schema or wire name:

```text
message Order {
  [name("order-id"), alias("legacy-id"), alias("old-id"), c(order_id)] uint32 id;
}
```

Here the generated member is `order_id`, while the native descriptor still identifies the
schema field as `id`. DataBind text input accepts `order-id`, then aliases in declaration
order (`legacy-id`, `old-id`), then `id`. Generated `Order_to_json`, `Order_to_yaml`,
`Order_to_csv`, and `Order_to_xml` require the codec and always output `order-id`.
Typed C generation rejects duplicate `[c(...)]` member names and the reserved `_presence`
member. Multiple `[alias(...)]` attributes are accepted in declaration order. Union variants
accept the same `[name(...)]` and `[alias(...)]` annotations as record fields.

DataBind has two typed routes: generate owning `.h/.c` from schema, or map the same schema
to an existing C struct. Code generation is optional for the second route. Include
`tbe_typed.h`, declare fields with `TBE_TYPED_FIELD` and related collection/object macros,
then create a static descriptor with `TBE_TYPED_DEFINE_STRUCT` or
`TBE_TYPED_DEFINE_STRUCT_WITH_PRESENCE`. `TBE_TYPED_BIND_PARSE` and
`TBE_TYPED_BIND_SERIALIZE` use that descriptor directly and apply schema names automatically.
These convenience descriptors do not infer a binary wire layout; use the explicit `_EX`
macros or generated code when direct TBE binary encoding is required.

`Orders_schema_codec()` exposes a schema-specific dispatch table for trusted host providers.
Its `text_to_binary_into` operation binds JSON/YAML/CSV/XML directly into caller-owned,
capacity-bounded wire storage, so a runtime can enforce its output quota before conversion.
The reverse `binary_to_text` operation returns an allocated host buffer and is intended for
trusted host code unless the runtime also enforces the serializer's temporary-allocation budget.
Schema `uuid` fields are generated as `turbo_uuid_t`; text formats use canonical UUID strings
and binary serialization preserves the fixed 16-byte wire value.
Compile `order.c` in the consumer target and link `TurboParser::DataBind`:

```cmake
add_executable(order_app main.c order.c)
target_link_libraries(order_app PRIVATE TurboParser::DataBind)
```

The same generated header is C++ compatible. Its C functions use `extern "C"`, and
the schema namespace provides non-copyable RAII owners that initialize and clear the
generated C record automatically:

```cpp
#include "order.h"

#include <string_view>

int main() {
    std::string_view input = R"({"id":42})";
    DataBind *codec = nullptr;
    DataBindError error = DATA_BIND_ERROR_INIT;
    Orders_typed::OrderOwner order;
    int result = 1;

    if (Orders_codec_create(&codec, &error) == DATA_BIND_OK &&
        order.from_json(codec, input.data(), input.size(), &error) == DATA_BIND_OK) {
        result = 0;
    }
    data_bind_free(codec);
    return result;
}
```

Compile the companion `order.c` as C even when the application target is C++. Fixed-layout
binary input is decoded directly through the generated native descriptor after validating
it against the codec schema. Variable `list`/`set`/`map` layouts use the dynamic binary
parser before committing into the owning struct. Text formats
retain the schema binder so enum names, field formats, and extended scalar rules remain
identical to the dynamic API. `--lang cpp` without `--source-output` continues to generate
data-only `std::string`/`std::vector` types and does not provide these serialization functions.

### Example 2b: Generate a Wasm Guest Adapter

```bash
tbe_compiler order.schema --lang c --output order.h --guest-output order_guest.c
```

Compile `order_guest.c` together with the guest application. The guest supplies a
`tbe_guest_bridge_t` whose callbacks forward to the runtime's versioned DataBind
capability. For example, `Order_guest_from_json` writes wire bytes into a caller-owned
buffer and binds an `Order_view_t`; `Order_guest_to_json` serializes an existing view
into a caller-owned text buffer. Callback failures are propagated unchanged, while
invalid pointers, `size_t` values outside the Wasm `uint32_t` ABI, invalid bridge
output lengths, and unbindable wire data return `tbe_guest_status_t` errors.

CSV input accepts a zero-based logical record index. The runtime remains responsible
for applying its input, output, object, and execution quotas before invoking the
trusted host codec.

For a freestanding wasm32 build, define `TBE_WASM_GUEST=1`. This removes the generated
wire header's dependency on host libc and the TurboUtils UUID runtime while preserving
the same fixed 16-byte `turbo_uuid_t` value layout:

```bash
clang --target=wasm32-unknown-unknown -DTBE_WASM_GUEST=1 -O2 -nostdlib \
  -Igenerated -Ipath/to/tbe/schema/include -c order_guest.c
```

Initialize an object before its first use, clear it when finished, and release serialized
buffers with `tbe_typed_serialized_free`:

```c
#include "order.h"

#include <string.h>

int main(void) {
const char *input = "{\"id\":42}";
DataBind *codec = NULL;
DataBindError error = DATA_BIND_ERROR_INIT;
Order_t order;
char *json = NULL;
size_t json_len = 0;
int result = 1;

Order_init(&order);
if (Orders_codec_create(&codec, &error) == DATA_BIND_OK &&
    Order_from_json(codec, &order, input, strlen(input), &error) == DATA_BIND_OK &&
    Order_to_json(codec, &order, &json, &json_len, &error) == DATA_BIND_OK) {
    result = 0;
}
tbe_typed_serialized_free(json);
Order_clear(&order);
data_bind_free(codec);
return result;
}
```

Output `order.rfl`:
```rfl
package OrderSchema

declare Order
    id: int
    total: double
    tier: String
end
```

### Example 3: Generate Both

```bash
tbe_compiler order.schema --output order.h --dsl-output order.rfl
```

### Example 4: Generate C++ Types

```bash
tbe_compiler order.schema --lang cpp --output order.hpp
```

### Example 5: Generate Go Types

```bash
tbe_compiler order.schema --lang go --output order.go
```

### Example 6: Generate TypeScript Types

```bash
tbe_compiler order.schema --lang ts --output order.ts
```

### Example 7: Generate Rust Types

```bash
tbe_compiler order.schema --lang rust --output order.rs
```

### Example 8: Generate Python Types

```bash
tbe_compiler order.schema --lang py --output order.py
```

### Example 9: Compile Generated C as a Static Library

```cmake
add_library(order_schema STATIC order.c)
target_include_directories(order_schema PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(order_schema PUBLIC TurboParser::DataBind)
```

### Example 10: Compile Generated C as a Shared Library

```cmake
add_library(order_schema SHARED order.c)
target_compile_definitions(order_schema PRIVATE TBE_GENERATED_BUILD_SHARED)
target_include_directories(order_schema PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(order_schema PUBLIC TurboParser::DataBind)
```

## Removed Options

- `--codec-project` (REMOVED)
  - Codec DLL project generation is no longer supported by the compiler directly.
- `--rfl-output` (REPLACED)
  - Replaced by `--dsl-output`.

## Notes

- `--dsl-output` uses `templates/rfl_types.mustache` by default.
- DSL output is intended for use in RulesForge to define the structure of data being processed.
- C output is the complete wire-access target with generated view/builder APIs.
- `--source-output` adds owning strong types and schema-driven text/binary serialization on top
  of the existing zero-copy view/builder API. Union declarations are currently rejected for
  this companion source. Generated `*_to_bin_into` and schema-codec `text_to_binary_into`
  functions never allocate their output buffer; insufficient capacity is reported with the
  required size in `out_len`.
- `--cbind-output` is an opt-in semantic sidecar. When requested, it adds CBind declarations to
  the generated header without changing `TbeTypedType`; default generation remains CBind-free.
- `--guest-output` adds allocation-free adapters over the zero-copy wire views. It does not
  embed JSON/YAML/CSV/XML parsers into Wasm and does not require `--source-output`.
- C++, Go, Rust, Python, and TypeScript outputs currently generate schema type definitions, not complete wire codecs.
- The compiler is a build-time tool. Generated C links DataBind, but a deployed
  RulesForge/TurboScript process loads only DataBind and any prebuilt schema
  libraries; it does not need an external C compiler.
- Dynamic schema hosts may skip code generation and use `DataBindObject`. Existing
  C structs use `TBE_TYPED_*` macro descriptors and also do not invoke the compiler.
