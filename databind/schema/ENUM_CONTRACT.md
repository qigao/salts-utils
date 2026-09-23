# Enum and flags storage contract

Enum/flags validation runs on the unpublished parser tree. A rejected declaration
returns `TBE_ERR_SEMANTIC_ERROR` without replacing the caller's existing schema.
Allocation failure remains `TBE_ERR_OUT_OF_MEMORY`.

## Storage and literals

Every accepted declaration has one canonical `underlying_type`: `int8`, `uint8`,
`int16`, `uint16`, `int32`, `uint32`, `int64`, or `uint64`. Integer aliases such as
`i8`, `uint64_t` and `byte` normalize to these names. Omitted storage means
`int32` for enums and `uint32` for flags. Other storage types are rejected, not
silently mapped to an integer. The runtime and compiler use this same metadata.

Decimal integers (including signed decimal literals) and unsigned `0x` hexadecimal
literals are accepted only if they fit the declared storage. Leading zeroes in
numbers such as `008` do not imply octal. Published values are exact decimal
strings; no floating-point or signed conversion is used for `UINT64_MAX`.
Negative literals are rejected for unsigned storage. Fractional and exponential
forms remain valid where the default-value grammar permits them, but not as
enum/flags values. Signed enum literals do not enable signed schema IDs or lengths.

An omitted first enum value is zero; each next omitted enum value adds one.
An omitted first flags value is one; each next omitted flags value doubles the
previous value. Arithmetic must fit storage: a terminal maximum is valid, while
an overflowing implicit successor is rejected. A following explicit value resets
the sequence and is validated independently.

## Names, aliases and helpers

A declaration must contain at least one member. Duplicate member names and
**duplicate numeric values are rejected** for both enums and flags, across all
generators. Different spellings (`1`, `01`, `0x01`) are the same numeric value.
There is no preferred-name alias selection: an accepted enum value has exactly
one member name. C string conversion returns that name; unknown values return
NULL. C min/max helpers use numeric signed order, not declaration order or an
unsigned reinterpretation of negative literals.

## Backends

C, C++, Go and Rust derive their integer storage from canonical `underlying_type`.
C/C++ constants use portable integer constant expressions, including a dedicated
`INT64_MIN` expression. Python `IntEnum` retains exact arbitrary-precision values;
its generated documentation records the wire storage. This does not change the
wire size into an arbitrary-precision integer.

TypeScript's numeric enum backend rejects `int64`/`uint64` storage before publishing
output, even when the currently declared members happen to be small. It cannot
preserve that storage domain with a JavaScript number. Use a supported <=32-bit
storage or another target; there is no automatic narrowing or lossy fallback.

The existing RulesForge template emits only names, not explicit numeric assignments.
`--dsl-output` therefore accepts only ordinal declarations with values `0..N-1` in
declaration order, and rejects other enum/flags declarations before writing either
the primary output or the DSL output. An existing output is preserved on failure.
`uint32` enum storage maps to RulesForge `long` rather than signed `int`.

## Regression commands

The normal schema CTest set includes `test_schema_enum_conformance`, checking
canonical metadata, range rejection and unchanged caller roots. The enum CI builds
the real compiler and runs the complete TBE tests with ASan and UBSan, then runs:

```sh
python3 databind/compiler/test/enum_conformance.py \
  --compiler build/linux-gcc-debug/bin/databindc \
  --source "$PWD" --salts-include /opt/salts/debug/include
```

This compiles and executes generated C/C++/Go boundary consumers, checks Python
values, and tests rejection/output preservation for invalid schemas and unsupported
targets. Deterministic cases are linked to fuzzing follow-up #7; these tests are not
a claim that the larger fuzzing work has been implemented.
