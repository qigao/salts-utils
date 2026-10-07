# Lua binding

The Lua binding is the runtime projection of canonical CMeta data/function
reflection into Lua.

The implementation lives here. `Salts::Lua` exports the value, invokable and
object bridges in `salts/bindings/lua/cmeta.h`, and grouped native registration
in `salts/bindings/lua/module.h`.

`salts_lua_push_module` creates a table from external C/C++ binding declarations
or existing admitted CMeta bindings. Native libraries need no CMeta dependency;
`salts/bindings/native.h` and `native.hpp` generate adapters from native types.
The host can publish this value or return it from its Lua module loader.
Free functions use ordinary calls; object methods support the existing colon
call convention. Registration preserves the old stack on error and does not
transfer ownership of native objects. See the shared [module contract](../README.md)
for owner lifetimes, supported signatures, tests and TS declaration tooling.
`salts/bindings/lua/native.hpp` also exposes typed C++ calls into Lua functions.

Required contract:

- scalar/enum values use canonical CMeta identities;
- struct members come from CMeta field metadata;
- string/bytes use explicit CMeta buffer providers and ownership;
- sequence/set/map require explicit CMeta collection providers;
- functions/interfaces use CMeta invocation metadata;
- no DataBind/TBE descriptors or schema-specific wrapper metadata;
- bounded recursion/conversion and fail-closed unsupported providers.
