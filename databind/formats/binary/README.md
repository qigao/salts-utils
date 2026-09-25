# DataBind Binary format backend

This directory owns only DataBind Binary wire/storage-format primitives.

It does not own the DataBind IDL, schema parser, compiler frontend, native
binding model, Service/Channel semantics, or transport projections.

Canonical format spelling is `binary`. Inside DataBind schema source, the
concise dialect annotation namespace is `@bin.*`.

The build target is internal. Consumers continue to use:

```cmake
find_package(SaltsUtils CONFIG REQUIRED)
target_link_libraries(app PRIVATE Salts::DataBind)
```

Wire/endian/version APIs use the `data_bind_binary_*` namespace.
`runtime/tbe_typed.*` is not Binary-only and must be split before its
historical name is removed.
