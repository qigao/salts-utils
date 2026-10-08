# salts-idlc Command Line Options

DataBind runtime、公共头和 target 由 SaltsUtils 构建、安装并导出为 `Salts::DataBind`。
DataBind 是生成 C、现有 C struct 与动态对象的绑定引擎；以下 typed 输出是当前受支持接口。

Canonical ownership is:

```text
CMeta: native structure and semantic type graph
Schema: Data-only logical shape, presence/defaults and validation
DataBind: logical/native binding, rollback and runtime format execution
CSTL: concrete container storage
CSerde/parsers: format tokens and mechanics
```

Generated/native and dynamic paths remain DataBind-owned over canonical CMeta structural
metadata; external names, presence/defaults, wire layout, validation, and fingerprints remain
overlay-only.

## Overview

`salts-idlc` generates code, bootstrap database DDL, and DSL declarations from IDL contract files for multiple target languages and RulesForge integration.

## Basic Usage

```bash
salts-idlc <schema_file> [options]
```

## Options

### Required

- `<schema_file>`
  - Path to the `.schema` definition file
  - Example: `order.schema`

### Language Output

- `--output <file>` or `-o <file>`
  - Output file path for generated code
  - Default: stdout for non-database languages
  - Required for `--lang sqlite`, `--lang postgresql`, and `--lang postgres`
  - Example: `--output order.h`

- `--lang <language>` or `-l <language>`
  - Target source language
  - Options: `c`, `cpp`, `go`, `rust`, `python`, `py`, `ts`, `typescript`, `sqlite`, `postgresql`, `postgres`
  - Default: `c`
  - Example: `--lang c`

- `--template <file>` or `-t <file>`
  - Path to custom Mustache template file
  - Overrides the built-in template selected by `--lang`
  - With database languages, custom templates consume the normalized database IR rather than the raw TBE AST
  - Example: `--template my_template.mustache`

- `--source-output <file>` or `-s <file>`
  - With the built-in C generator, emits the typed serialization companion `.c` file
  - Requires `--output`; custom templates, non-C languages, and database languages are rejected
  - The generated header exposes strong record types plus binary/JSON/YAML/CSV/XML APIs
  - Example: `--output order.h --source-output order.c`

- `--guest-output <file>` or `-g <file>`
  - With the built-in C generator, emits a Wasm-friendly guest adapter `.c` file
  - Requires `--output`; custom templates, non-C languages, and database languages are rejected
  - The adapter converts JSON/YAML/CSV/XML byte slices to generated wire views, and back,
    through a caller-provided `tbe_guest_bridge_t`
  - The adapter performs no parsing and owns no buffers; the runtime bridge controls
    schema registration, sandbox policy, quotas, and provider errors
  - Example: `--output order.h --guest-output order_guest.c`

Lua adapters are not a DataBind compiler output. Runtime Lua interoperability
uses Salts `Salts::Lua` and `<salts/bindings/lua/cmeta.h>` against the canonical
CMeta descriptors emitted with the native C representation. DataBind does not
generate schema-specific Lua metadata, ownership models, operation wrappers, or
fallback adapters.

### TypeScript: Contract-only Type Projection

`salts-idlc contract.schema --lang ts --output contract.ts` renders immutable,
validated `IdlContract` through a compiler-owned TypeScript presentation IR.
No Binary/TBE layout overlay is evaluated unless a separate Binary projection
is explicitly selected. Variable-data fields may precede fixed fields in a
TypeScript-only contract.

The built-in Mustache template and `--template` overrides consume source
presentation fields (`enums`, `messages`, `composites`, `groups`,
`name`, `ts_type`, `ts_optional`, enum `items`, and
`schema.schema_name`). They no longer receive Binary wire offsets,
sizes, cursor metadata or native lifecycle attributes.

- `list<T>`, `set<T>`, `map<K,V>` lower recursively to `Array<T>`,
  `Set<T>`, `Map<K, V>`; fixed arrays and `group<T>` lower to `Array<T>`.
- `optional` becomes an optional property (`?`); `nullable` becomes
  `| null`. The two properties remain independent.
- Signed and unsigned 64-bit integers and logical `varint` use TypeScript
  `bigint` instead of precision-losing JavaScript `number`. Numeric
  64-bit enum declarations remain rejected, since TypeScript numeric enums
  cannot preserve their complete value range.
- Native-only/domain types without an admitted TypeScript mapping (for
  example `datetime`) and union declarations fail before publishing output.
  There is no fallback emitter, guessed type alias, or hidden Binary admission.

This output is a **type declaration artifact**, not a serializer. Binary
layout/encoding validation belongs to an explicitly selected format plan.

### Python: Contract-only Dataclasses

`salts-idlc contract.schema --lang python --output contract.py` renders
validated, logical `IdlContract` through the same compiler-owned source
presentation IR used by TypeScript. Neither the built-in Python template nor
a custom `--template` consumes Binary wire layout/offset/size metadata.
Only an explicitly selected Binary projection performs Binary admission.

The current output contract is deliberately narrow and fail-fast:

- `list<T>`, `set<T>`, `map<K,V>` become `list[T]`, `set[T]`,
  `dict[K,V]`, with recursively resolved argument types; fixed arrays and
  `group<T>` become `list[T]`.
- `int64`, `uint64`, `varint` and other integer aliases map to exact Python
  `int`; `bool`, `float/double`, `string/uuid`, `bytes` map to
  `bool`, `float`, `str`, `bytes` respectively.
- `nullable` maps to `T | None` on a **required** dataclass field.
  `optional` represents omission, not explicit `None`, and is therefore
  **rejected** until a canonical presence-aware Python representation is
  introduced. Fields with IDL `default` are also rejected rather than
  silently dropping default semantics.
- IDL enum and flags declarations become `IntEnum` and `IntFlag`. Future
  annotations are enabled so a declaration may reference a later record.
- Unknown domain storage and unions fail before output publication rather
  than emitting guessed imports or silently omitting members.
- Python presentation templates consume `schema.schema_name`,
  `messages/composites/groups/enums`, `name`, `python_type`,
  `enum_name`, `python_flags`, and `items`; legacy
  Binary-derived `size_bytes` and cursor attributes are unavailable.

This is a **type-only** source artifact, not a serializer or a runtime
schema compiler. Native lifecycle stays CMeta-owned and formats remain
independent projection axes.

### Go: Typed Contract Rendering

`salts-idlc contract.schema --lang go --output contract.go` projects Go
source-only records, enums and flags from the immutable `IdlContract`
rather than requiring a Binary layout or materializing Binary field offsets.
Custom `--template` files receive the same presentation-only keys
(`schema.go_package_name`, `messages/composites/groups/enums`,
`go_name`, `go_type`, `go_underlying_type`, `items`).

- Scalars and fixed integer widths use canonical Go spellings: `uint64`
  remains `uint64`, not a JavaScript-style number. `uuid` is
  `[16]byte`; dynamically sized `bytes` is `[]byte`; `bytes(16)`
  produces `[16]byte`.
- `list<T>`, `group<T>`, `set<T>`, and `map<K,V>` lower to
  `[]T`, `[]T`, `map[T]struct{}`, `map[K]V`; recursive nested
  generics use the IDL parser's bounded type-expression semantics.
- Go map keys and set values are admitted only when the resulting Go key
  is provably comparable (integer, bool, string, UUID or enum); slices,
  floats and records do not become guessed key providers.
- Fixed arrays such as `uint8[8]` use `[8]uint8`, with decimal length
  normalized at rendering. Symbolic/dynamic array bounds fail closed rather
  than quietly becoming a slice.
- `nullable T` maps to the **required** field type `*T`. As with the
  Python dataclass source-only projection, omitted-field `optional` and
  schema `default` have no exact plain-struct representation and are
  rejected until explicit presence/default-aware Go storage exists.
- Logical `varint` or `bigint` require a width/ownership policy not
  supplied by a Go primitive type; they fail before publication. Unknown
  domain storage and unions are likewise rejected. An explicit Binary
  projection still performs Binary-specific admission on a distinct
  projection view.

The Go output is **type declarations only**, not a runtime serializer,
validator, or ABI replacement. All failures retain atomic output semantics.

### Rust: Contract-only Type Declarations

`salts-idlc contract.schema --lang rust --output contract.rs` generates
Rust record/enum declarations directly from the validated immutable
`IdlContract` via compiler-owned presentation IR. Rust source-only output
does **not** run Binary offset/order/size admission, unless a separate Binary
format or transport is explicitly selected.

- Exact numeric storage maps via canonical CMeta scalar aliases:
  `int64 -> i64`, `uint64 -> u64`, `uuid -> [u8; 16]`,
  `bytes -> Vec<u8>`, and bounded `bytes(16) -> [u8; 16]`.
  Fixed arrays such as `uint8[8]` become `[u8; 8]`.
- `list<T>`, `group<T>`, `set<T>` and `map<K,V>` become
  `Vec<T>`, `Vec<T>`, `HashSet<T>` and `HashMap<K,V>`, with
  nested generic expressions resolved by the canonical bounded type parser.
  Maps and sets admit only provably hashable keys; float/record/collection
  keys are rejected rather than generating Rust that cannot compile.
- `nullable T` becomes `Option<T>` on a **required** field.
  Omitted `optional` fields and schema `default` values require their own
  explicit presence/default representation and are rejected for now.
- By-value record cycles are rejected: `struct Node { next: Node }` and
  `Option<Node>` cannot have a finite size. `Vec<Node>` breaks the
  recursive sizing edge and is permitted.
- Enums use `#[repr(T)]` with exact integer widths and
  `Debug + Clone + Copy + Eq + Hash` so they work as collection keys.
  Decimal discriminants are canonicalized for Rust source syntax.
  IDL `flags` require a dedicated Rust bitflag representation and are
  rejected instead of being silently lowered into a semantically different
  enum.
- Reserved Rust identifiers, unknown domains, unions, logical `varint`
  without a specified width/owner, and unsupported fixed array shapes
  fail before publishing any output.
- The built-in Rust template depends on the Rust standard library only;
  it does not inject `serde` or promise serialization. Custom `--template`
  files consume type presentation keys (`rust_type`, `rust_underlying_type`,
  `name`, `items`, `messages/composites/groups/enums`), not Binary
  offsets, ownership facts or runtime format metadata.

This is source declaration generation, not a native ABI mapping, wire codec,
validation engine, or serialization runtime.

### C++17: Contract-only Type Declarations

`salts-idlc contract.schema --lang cpp --output contract.hpp` projects
standard C++17 declarations directly from validated immutable
`IdlContract`. It does **not** use Binary field layout, wire offsets,
CMeta native storage or RAII lifecycle annotations. A requested separate
Binary transport/format still has explicit Binary admission.

The built-in C++ Mustache renderer consumes the presentation-only
`cpp_records` (topologically ordered), `cpp_forward_declarations`
and `enums` views, plus `cpp_type`, `cpp_underlying_type`,
`c_literal`, `name` and enum `items`. Per-kind
`messages/composites/groups` presentation views remain available to custom
Mustache templates, but are not an alternative semantic authority.

- Exact scalar types use standard integer spellings, e.g.
  `uint64 -> std::uint64_t`. Logical `string -> std::string`,
  `bytes -> std::vector<std::uint8_t>`, and
  `uuid -> std::array<std::uint8_t, 16>`. This type-only UUID value
  representation does not expose a CMeta or Binary wire ABI.
- `bytes(16) -> std::array<std::uint8_t, 16>`,
  `uint8[8] -> std::array<std::uint8_t, 8>`,
  `list<T>/group<T> -> std::vector<T>`,
  `set<T> -> std::set<T>`, and
  `map<K,V> -> std::map<K,V>` with bounded nested generic admission.
- Map keys and set elements must be a provably comparable scalar
  (bool/integer/string/UUID/enum), not floating point, mutable bytes,
  composite records or containers. Source output never guesses
  `operator<` for unknown classes.
- `nullable T -> std::optional<T>` denotes explicit nullability on a
  required field. IDL `optional` means omitted/present and `default`
  is a schema initializer; neither can be silently represented by a bare
  C++ member. Both fail closed until the canonical presence/default
  representation is defined.
- Forward declarations plus Contract dependency ordering ensure inline
  records are complete when needed, including cross-kind
  message/composite/group references. Direct and indirect by-value record
  cycles are rejected; recursion through `std::vector<T>` is admitted.
- Enum underlying widths remain exact. The full uint64 range uses
  `ULL` integer constants, and `INT64_MIN` is emitted as a safe
  subtraction expression. Unsupported `flags`, unions, reserved C++
  identifiers, unknown domain types and width-unresolved
  `varint/bigint` fail before any artifact is published.
- The built-in header depends only on the C++17 standard library, not a
  transitive CMeta/native library. It is **not** a Binary serializer,
  codec, or drop-in C ABI counterpart.

### DSL Integration (RulesForge)

- `--dsl-output <file>` or `-d <file>`
  - Generate DSL type declarations (.rfl file)
  - C-generation-only auxiliary output; database languages reject it
  - Contains `declare` statements for use in RulesForge
  - Example: `--dsl-output order.rfl`

## Database DDL Generation

`salts-idlc` can generate deterministic bootstrap DDL for empty SQLite or PostgreSQL
databases. This is a build-time feature only: TurboDB, ORM, and generated runtime code do
not parse TBE at runtime and do not gain a SaltsUtils dependency from these outputs.

Database output generates tables, foreign keys, normal or unique composite indexes, custom
checks, and seed inserts. It does not inspect live schemas, emit `ALTER TABLE`, track migration
history, or open database connections.

### Runnable Schema Example

```tbe
schema Accounts [
    id(1), version(1), byte_order(little),
    db_init(sqlite, "INSERT INTO users(id, tenant) VALUES (1, 7)")
];

[db_table("users"), db_unique_index(users_identity, id, tenant)]
message User {
    [db_column("id"), db_primary_key(1)] int64 id;
    [db_primary_key(2)] int32 tenant;
}

[db_table("orders"),
 db_foreign_key(fk_orders_user, User, user_id, id, tenant, tenant),
 db_index(idx_orders_lookup, tenant, user_id),
 db_check(ck_total, sqlite, "total_cents >= 0")]
message Order {
    int64 user_id;
    int32 tenant;
    int64 total_cents;
}
```

### Database Commands

```bash
salts-idlc accounts.schema --lang sqlite --output accounts.sqlite.sql
salts-idlc accounts.schema --lang postgresql --output accounts.postgresql.sql
salts-idlc accounts.schema --lang postgres --output accounts.postgresql.sql
salts-idlc accounts.schema --lang sqlite --template custom_sqlite.mustache --output accounts.sql
```

### Database Annotation Contract

| Annotation | Parameter | Generated result | Compilation errors |
|---|---|---|---|
| `db_table("name")` | Non-empty logical table name without embedded NUL | Emits one quoted SQL table | Missing/empty value, duplicate table name, or no persistent fields |
| `db_foreign_key(name, Target, local, remote, ...)` | Constraint name, target message, and one or more local/remote field pairs | Emits a same-schema single or composite foreign key | Missing field/table, repeated fields, type mismatch, non-unique target, or duplicate constraint name |
| `db_foreign_key_on_delete(name, action)` | Existing foreign-key constraint name and `cascade`, `restrict`, or `no_action` | Appends the normalized `ON DELETE` action to that foreign key | Unknown key, duplicate action, unsupported action, or wrong argument count |
| `db_index(name, field, ...)` | Globally unique index name and ordered persistent fields | Emits a normal single or composite index | Missing/repeated field or duplicate index name |
| `db_unique_index(name, field, ...)` | Globally unique index name and ordered persistent fields | Emits a unique single or composite index | Missing/repeated field or duplicate index name |
| `db_check(name, dialect, "expression")` | Table-local name, `sqlite` or `postgresql`, and one safe expression | Emits a table-level check only for that dialect | Unknown dialect, duplicate constraint name, or unsafe SQL boundary |
| `db_init(dialect, "INSERT ...")` | Schema-level dialect and one INSERT without a terminator | Emits the statement after tables and indexes, in declaration order | Unknown dialect, non-INSERT statement, or unsafe SQL boundary |
| `db_column("name")` | Optional column name override | Emits one quoted SQL column name | Missing/empty value, embedded NUL, or duplicate column name in the same table |
| `db_primary_key(order)` | Integer order starting at `1` | Adds the field to the primary key in order | Missing/duplicate/gapped order, or `optional` field used as a primary key |
| `db_unique(1)` | Literal `1` only | Adds a single-column `UNIQUE` constraint | Any value other than `1`, or conflicting field annotations |
| `db_generated(identity)` | Literal `identity` only | Emits dialect identity syntax for a single-column integer primary key | Non-integer type, composite key, `optional`, TBE `default`, `db_unique`, or unsupported dialect/type combination |
| `db_ignore(1)` | Literal `1` only | Excludes the field from the normalized database IR and output | Any value other than `1`, or combination with other `db_*` field annotations |

All annotation arguments are retained in declaration order. The legacy AST `value` field remains
the first argument for compatibility. Existing single-argument database annotations reject extra
arguments.

Foreign-key targets must be persistent messages in the same schema. Their referenced field list
must exactly match a primary key, a `db_unique(1)` column, or a `db_unique_index`, and normalized
local/remote SQL types must match. Constraint names are unique per table; index names are unique
for the schema and cannot collide with table names. SQLite applications must enable
`PRAGMA foreign_keys=ON` on each connection that requires enforcement.

SQLite emits foreign keys inline. PostgreSQL creates all tables and indexes first, then emits
`ALTER TABLE ... ADD CONSTRAINT` for foreign keys. This supports forward message references and
ensures a referenced `db_unique_index` exists before the foreign key is added. Seed inserts are
always emitted last.

Built-in SQLite and PostgreSQL outputs are complete standard DDL transactions: the file starts
with `BEGIN;` and ends with `COMMIT;`. Consumers execute the file directly and must not add an
outer transaction. `CREATE`, `ALTER`, index creation, and seed inserts therefore commit as one
unit or remain uncommitted when execution fails.

Custom checks and seed statements are constrained build inputs, not arbitrary SQL scripts. The
compiler rejects controls, semicolons, line/block comments, unterminated quotes, and unbalanced
parentheses; initialization must begin with an independent `INSERT` keyword. This is a statement
boundary check rather than a full SQL parser, so the selected database remains authoritative for
SQL validity. Every annotation is validated even when its dialect is not selected.

### Database Type Mapping

| TBE type | SQLite | PostgreSQL |
|---|---|---|
| `bool` | `INTEGER` + `CHECK (col IN (0, 1))` | `boolean` |
| `int8` / `int16` / `int32` and aliases | `INTEGER` | `smallint` / `integer` |
| `int64` and aliases | `INTEGER` | `bigint` |
| `uint8` / `byte` and aliases | `INTEGER` + range `CHECK` | `smallint` + range `CHECK` |
| `uint16` and aliases | `INTEGER` + range `CHECK` | `integer` + range `CHECK` |
| `uint32` and aliases | `INTEGER` + range `CHECK` | `bigint` + range `CHECK` |
| `uint64` and aliases | Canonical decimal `TEXT` + syntax/range `CHECK` | `numeric(20,0)` + range `CHECK` |
| `float` / `f32` | `REAL` | `real` |
| `double` / `f64` | `REAL` | `double precision` |
| `string` | `TEXT` | `text` |
| `bytes` and `bytes(n)` | `BLOB` | `bytea` |
| `uuid` | `TEXT` | `uuid` |
| enum references | Underlying integer mapping + range `CHECK` | Underlying integer mapping + range `CHECK` |

Collection, map, group, composite, and union references do not fall back to JSON or BLOB.
Use `db_ignore(1)` for fields that should stay out of the bootstrap schema.

Signed integers, decimal fractions, and exponent tokens are accepted only after a field `default`.
Enum/flags assignments, numeric annotations, and fixed lengths keep their existing non-negative
integer rules; hexadecimal integers remain accepted only in contexts that already supported them.

SQLite stores `uint64` as canonical decimal text so values above signed 64-bit remain exact:
only ASCII digits are accepted, leading zeroes are rejected except for `0`, and the maximum is
`18446744073709551615`. Its check requires the BLOB byte length to equal the text character
length, rejecting embedded NUL and multibyte non-ASCII payloads. SQLite integer range checks also
require integer storage, so fractional REAL values cannot pass; optional columns continue to allow
`NULL`.

PostgreSQL string defaults are emitted as `E'...'` literals with both backslashes and single
quotes escaped. Their meaning therefore does not depend on the server's
`standard_conforming_strings` setting. Defaults remain typed constants; raw SQL expressions are
never accepted.

### Database Errors and Boundaries

- Database languages fail fast when `--output` is omitted.
- `--source-output`, `--guest-output`, and `--dsl-output` fail fast with
  `--lang sqlite` or `--lang postgresql`; the error names the conflicting option and
  language before schema parsing or output creation.
- Database output requires at least one `db_table(...)` message and at least one non-ignored
  field per generated table.
- Numeric defaults accept signed integers, decimal fractions, and decimal exponents; hexadecimal
  integer syntax remains supported. Malformed numeric tokens fail during parsing, and defaults
  must stay within the TBE field type.
- SQLite identity accepts only signed integer single-column primary keys. PostgreSQL accepts
  `uint8`, `uint16`, and `uint32` identity columns while retaining their unsigned range checks,
  but rejects `uint64` because PostgreSQL sequences do not support `numeric(20,0)`.
- Foreign keys fail when target fields are not an exact declared unique key or normalized SQL
  types differ. Index fields must exist, be persistent, and appear only once.
- Custom SQL fragments fail when they contain statement terminators/comments, malformed quote or
  parenthesis structure, or when an initializer is not an INSERT.
- Output replacement is atomic. POSIX first creation follows `0666 & ~umask`, while overwriting an
  existing target preserves its permission bits. No Windows ACL preservation guarantee is made.

### Custom Database Templates

Custom database templates receive normalized database IR instead of the raw TBE AST. Stable
fields include:

- Schema scope: `db_tables`, `db_indexes`, `db_initializers`
- Table scope: `sql_table_name`, `db_columns`, `db_primary_key_columns`,
  `db_foreign_keys`, `db_checks`, `has_composite_primary_key`, `has_next_table`
- Column scope: `sql_column_name`, `sql_type`, `sql_constraints`,
  `has_sql_constraints`, `has_next_column`
- Primary-key reference scope: `sql_column_name`, `has_next_primary_key`
- Foreign-key scope: `sql_constraint_name`, `sql_referenced_table_name`,
  `db_foreign_key_columns`; each field pair exposes `sql_column_name`,
  `sql_referenced_column_name`, `has_next_foreign_key_column`
- Index scope: `sql_index_name`, `sql_table_name`, `db_index_columns`, and the truthy marker
  `is_unique_index`; index fields expose `sql_column_name`, `has_next_index_column`
- Check scope: `sql_constraint_name`, `sql_check_expression`
- Initializer scope: `sql_statement`

`has_next_*` and `has_sql_constraints` are the supported marker fields. Database templates
must not depend on `is_last`.

## Usage Examples

### Example 1: Generate C Header Only

```bash
salts-idlc order.schema --output order.h
```

### Example 2: Generate DSL Type Declarations

```bash
salts-idlc order.schema --dsl-output order.rfl
```

### Example 2a: Generate Strong Typed C Bindings

```bash
salts-idlc order.schema --lang c --output order.h --source-output order.c
```

Lua hosts consume the generated records through their canonical CMeta descriptors
and Salts `Salts::Lua`; no Lua-specific source artifact is generated by `salts-idlc`.

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

DataBind supports generated owning `.h/.c` and schema bindings for existing C structs.
The latter needs no code generation: provide a canonical `cmeta_data_desc` graph and
`DataBindNativeTypeBinding`, compile a MessagePlan and a FormatPlan, then bind through
CSerde readers/writers. CMeta providers own initialization, move and release. Decode into
fresh staging and publish only after successful decoding and reader close; failure must
leave the previous object intact. Text field aliases and output names belong to the
FormatPlan, not to a second native metadata table.

See the [native binding guide](../runtime/README.md#路线二映射现有-c-struct) and the
[executable native benchmark](../runtime/benchmark_data_bind_native.c) for ownership,
capacity limits and cleanup. The historical typed runtime is removed. Generated
Binary entry points use validated BinaryLayoutIR/providers, CSerde and native
MessagePlan execution; unsupported shapes fail before object/output publication.

`Orders_schema_codec()` exposes a schema-specific dispatch table for trusted host providers.
Its `text_to_binary_into` operation binds JSON/YAML/CSV/XML directly into caller-owned,
capacity-bounded wire storage, so a runtime can enforce its output quota before conversion.
The reverse `binary_to_text` operation returns an allocated host buffer and is intended for
trusted host code unless the runtime also enforces the serializer's temporary-allocation budget.
Schema `uuid` fields are generated as `cmeta_uuid_t`; text formats use canonical UUID strings
and binary serialization preserves the fixed 16-byte wire value.
Generated C uses DataBind for native conversion, transactional rollback, and format
orchestration over its canonical CMeta graph and schema overlay:

```cmake
add_executable(order_app main.c order.c)
target_link_libraries(order_app PRIVATE Salts::DataBind)
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

Compile the companion `order.c` as C even when the application target is C++.

`--source-output` 会在写文件前检查所有生成记录的 canonical CMeta/native
生命周期；缺少完整生命周期或已验证本地生命周期时，报告记录名称并失败，
已有头文件和源文件保持不变。包含一个合法 CSTL 字段并不能使整个记录获准。
生成代码不再引用历史 TBE typed 表、descriptor 或 raw init/clear。

生命周期准入与格式准入独立：已准入的类型使用现有 CMeta/provider 释放资源，
不支持的格式仍返回错误。仅生成 wire view/builder 的 C 头文件保留原有能力。
`--lang cpp` without `--source-output` continues to generate data-only
`std::string`/`std::vector` types and does not provide these serialization functions.


### Example 2b: Generate a Wasm Guest Adapter

```bash
salts-idlc order.schema --lang c --output order.h --guest-output order_guest.c
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

For a freestanding wasm32 build, define `DATA_BIND_BINARY_WASM_GUEST=1`. This removes the generated
wire header's dependency on host libc and the Salts UUID runtime while preserving
the same fixed 16-byte `cmeta_uuid_t` value layout:

```bash
clang --target=wasm32-unknown-unknown -DDATA_BIND_BINARY_WASM_GUEST=1 -O2 -nostdlib \
  -Igenerated -Ipath/to/databind/schema/include -c order_guest.c
```

Initialize an object before its first use, clear it when finished, and release serialized
buffers with `data_bind_serialized_free`:

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
data_bind_serialized_free(json);
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
salts-idlc order.schema --output order.h --dsl-output order.rfl
```

### Example 4: Generate C++ Types

```bash
salts-idlc order.schema --lang cpp --output order.hpp
```

### Example 5: Generate Go Types

```bash
salts-idlc order.schema --lang go --output order.go
```

### Example 6: Generate TypeScript Types

```bash
salts-idlc order.schema --lang ts --output order.ts
```

### Example 7: Generate Rust Types

```bash
salts-idlc order.schema --lang rust --output order.rs
```

### Example 8: Generate Python Types

```bash
salts-idlc order.schema --lang py --output order.py
```

### Example 9: Compile Generated C as a Static Library

```cmake
add_library(order_schema STATIC order.c)
target_include_directories(order_schema PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(order_schema PUBLIC Salts::DataBind)
```

### Example 10: Compile Generated C as a Shared Library

```cmake
add_library(order_schema SHARED order.c)
target_compile_definitions(order_schema PRIVATE TBE_GENERATED_BUILD_SHARED)
target_include_directories(order_schema PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(order_schema PUBLIC Salts::DataBind)
```

## Removed Options

- `--codec-project` (REMOVED)
  - Codec DLL project generation is no longer supported by the compiler directly.
- `--rfl-output` (REPLACED)
  - Replaced by `--dsl-output`.

## Notes

- `--dsl-output` uses `templates/reflection/rfl_types.mustache` by default.
- DSL output is intended for use in RulesForge to define the structure of data being processed.
- C output is the complete wire-access target with generated view/builder APIs.
- `--source-output` adds owning strong types and schema-driven text/binary serialization on top
  of the existing zero-copy view/builder API. Union declarations are currently rejected for
  this companion source. Generated `*_to_bin_into` and schema-codec `text_to_binary_into`
  functions never allocate their output buffer; insufficient capacity is reported with the
  required size in `out_len`.
- `--guest-output` adds allocation-free adapters over the zero-copy wire views. It does not
  embed JSON/YAML/CSV/XML parsers into Wasm and does not require `--source-output`.
- C++, Go, Rust, Python, and TypeScript outputs currently generate schema type definitions, not complete wire codecs.
- The compiler is a build-time tool. Generated C links DataBind, but a deployed
  RulesForge/TurboScript process loads only DataBind and any prebuilt schema
  libraries; it does not need an external C compiler.
- Dynamic schema hosts may skip code generation and use `DataBindObject`. Existing
  C structs use canonical CMeta graphs and native bindings without invoking the compiler.
