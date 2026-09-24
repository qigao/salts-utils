# DataBind TBE format backend

This directory owns only TBE wire/storage-format primitives.

It does not own the DataBind IDL, schema parser, compiler frontend, native
binding model, Service/Channel semantics, or transport projections.

The build target is internal. Consumers continue to use:

```cmake
find_package(SaltsUtils CONFIG REQUIRED)
target_link_libraries(app PRIVATE Salts::DataBind)
```

Generated code may continue to include `<tbe_wire.h>`; the installed header
spelling is preserved while source ownership lives under `formats/tbe`.

`runtime/tbe_typed.*` remains in the generic runtime until its mixed
format-neutral native semantics and binary-only TBE behavior are split.
