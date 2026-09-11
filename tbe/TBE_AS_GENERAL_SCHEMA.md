# TBE As A General Schema Format

> 本文中的 DataBind 路径是遗留设计记录。DataBind 已退出 SaltsUtils 默认构建与安装；
> 新代码使用基础 Salts 的 `Salts::CBind`，并通过 CMeta/CSerde 描述数据与格式边界。

Updated: 2026-09-11

## Summary

TBE can serve as a schema format for performance-sensitive data, but it is not a fully general data-modeling system like JSON Schema, Protobuf, or Avro.

Its best fit is still:

- low-latency binary messages
- deterministic fixed-prefix layouts
- generated type-safe accessors
- compact wire representation
- pure C runtime schema binding through DataBind

## Current Capabilities

TBE currently supports:

- `schema` metadata with version and byte order attributes
- `composite`, `group`, `message`, and `union`
- enum and flags declarations
- required and optional fields
- default values
- fixed arrays and fixed bytes
- repeating groups
- var-data fields such as `string`, variable `bytes`, and dynamic collection fields in the var-data section
- C wire-access generation
- C++ / Go / Rust / Python / TypeScript type generation
- RulesForge `.rfl` declaration output
- generated C owning bindings and pure C dynamic runtime binding

## Deliberate Constraints

TBE requires message fields to be ordered by layout section:

1. fixed-size fields
2. groups
3. var-data fields

This is not an accidental parser limitation. It is the core layout contract that allows generated accessors to use compile-time offsets for the fixed prefix.

Examples:

```c
message Correct {
    uint32 id;
    group<Level> bids;
    string symbol;
}
```

```c
message Broken {
    string symbol;
    group<Level> bids;
}
```

The second form is rejected because a group cannot be located after a var-data field without runtime offset metadata.

### `varint` capability

`varint` is explicitly unsupported by the current TBE profile. The schema validator rejects it before compiler model construction or code generation. It must not be treated as a parser-only compatibility alias.

Support may only be enabled after the runtime, typed descriptors, binary conversion paths, and every advertised generator agree on one documented wire encoding and range contract.

## Language Output Model

### Full Wire API

C is the full zero-copy wire target. It emits view/builder APIs, accessors, enum helpers, flags helpers, optional/default helpers, group cursors, and union tag helpers.

### Type Definition Targets

C++, Go, Rust, Python, and TypeScript currently emit type declarations. They do not yet provide complete generated wire encode/decode APIs.

This split is intentional for now:

- C owns the stable low-level wire surface.
- Other languages get schema-aligned types for adapters, bindings, tests, and generated SDK layers.

## Comparison

| Feature | TBE | Protobuf | JSON Schema |
| --- | --- | --- | --- |
| Zero-copy fixed-prefix access | Strong | No | No |
| Compact binary wire format | Strong | Strong | No |
| Optional fields | Supported | Supported | Supported |
| Union-like tagged values | Supported | Supported | Schema-dependent |
| Flexible field ordering | Constrained | Supported | Supported |
| Runtime reflection | Schema-driven DataBind | Strong | Strong |
| Human-readable payload | No | No | Yes |
| Full multi-language codecs | C complete, others type-only | Strong | Library-dependent |

## Recommended Use

Use TBE for:

- market data and trading messages
- game/network state packets
- IoT and embedded telemetry
- high-throughput internal streams
- binary protocol boundaries where schema and layout are controlled

Avoid TBE for:

- human-authored configuration
- public JSON-style APIs
- highly dynamic object graphs
- storage models requiring ad hoc queries
- schemas that require arbitrary field reordering without performance tradeoffs

## Salts Integration Direction

Recommended boundaries:

- Use TBE C generation for native high-performance codecs.
- Use DataBind dynamic objects where runtime schema binding needs an owning dynamic value tree.
- Use `TBE_TYPED_*` descriptors for DataBind's independent existing-struct conversion route.
- Use C++/Go/Rust/Python/TypeScript type outputs for adapters and typed client surfaces.
- Use RulesForge output when a TBE schema should describe data consumed by rules.

## Conclusion

TBE is a specialized high-performance schema and wire-layout system. It has grown beyond a minimal fixed-message format, but its value still comes from keeping layout constraints explicit instead of becoming a fully dynamic schema language.
