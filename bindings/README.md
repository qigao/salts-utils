# Salts language bindings

This directory binds existing C/C++ libraries to Lua and QuickJS. Native headers
and implementations need no CMeta dependency or annotations. A separate binding
translation unit declares the export surface; macros and C++ type deduction
generate the metadata and exact call adapters used by both VMs and TS tooling.

Bindings are consumers of CMeta reflection, invocation and provider metadata.
They do not define schema semantics, native type identity, ownership models, or
format/wire layouts.

## Modules

- `common/` — `Salts::Bindings`, exact adapter contracts, C11 declaration macros and export tables.
- `cpp/` — `Salts::BindingsCpp`, C++17 function/member deduction and borrowed-object facades.
- `lua/` — CMeta <-> Lua runtime binding.
- `quickjs/` — CMeta <-> QuickJS runtime binding.

## Dependency rule

```text
Lua / QuickJS
      |
      v
    CMeta
```

A binding must not depend on DataBind, TBE, a generated schema AST, or another
binding module. DataBind and other higher-level systems expose their native
objects through CMeta before invoking a binding.

Collection access is also a CMeta provider contract. A binding must not infer
container storage from CSTL/vec_t or from another consumer such as Jinja.

TypeScript declaration generation is compiler tooling and is intentionally not
owned by the QuickJS runtime binding.

## Export existing C/C++ implementations

The source of truth is the existing native declaration. Export names and selected
members are listed separately, in the style of the project's historical `moon`
binding helpers. CMeta descriptors are generated implementation details; callers
do not have to construct descriptors, invokables or operation providers.

1. In a C++ binding TU, include `salts/bindings/native.hpp` and link
   `Salts::BindingsCpp`. `SALTS_BIND_FUNCTION(function)` deduces the actual
   function pointer type. `Salts::Binding::object(instance,
   SALTS_BIND_MEMBERS(Class, method, field))` deduces member pointers and builds
   one borrowed object binding; `binding.export_as("name")` produces its export
   row. Explicit `function<Pointer>("name")` / `member<Pointer>("name")` calls
   allow aliases and cast-selected overloads.
   In C11, include `salts/bindings/native.h`: `SALTS_BIND_C_FUNCTION(Return,
   function, (Type, argument), ...)` declares a typed adapter;
   `SALTS_BIND_C_FUNCTION0` handles no arguments; `SALTS_BIND_C_EXPORT` selects
   an export. `SALTS_BIND_C_FIELD` describes a mutable scalar struct member and
   verifies its actual type. All of these declarations live outside native code.
2. Pass the same export table to `salts_lua_push_module` or
   `salts_quickjs_push_module`. Each creates a module value; the host chooses
   whether to publish it as a global or return it from a module loader.
3. In a native build tool, link `Salts::DataBindProducer` and call
   `data_bind_typescript_emit` from `data_bind_typescript.h` with that table and
   a write callback. It emits `export interface <name_prefix>Bindings` for the
   QuickJS surface. The tool stages the `.d.ts` file and publishes it only after
   success. No VM or DataBind runtime is required by the emitter.

Complete compiled examples are the [C frontend test](tests/native_frontend_test.c)
and [C++ frontend test](tests/native_frontend_test.cpp). Their independent
[C implementation](tests/fixtures/native_math.c) and
[C++ class](tests/fixtures/native_counter.hpp) contain no binding declarations.
The C++ test demonstrates registration, field updates, methods, TS generation
and calling scripts from native code. Existing admitted CMeta invokables and
object refs remain accepted for applications that already use them.

## Call scripts from native code

Include `salts/bindings/lua/native.hpp` or
`salts/bindings/quickjs/native.hpp`. `Salts::Lua::call(context, stack_index,
result, args...)` and `Salts::QuickJS::call(context, function_value, result,
args...)` deduce argument/result conversion from C++ types. `call_void` handles
procedures. Results must start in the type's semantic-zero state. Both return
`cmeta_status`; Lua restores the original stack, while QuickJS leaves a script
exception pending for the host to consume. C callers use the corresponding
`salts_*_call_script` API with a generated FunctionData signature.

QuickJS executes JavaScript. TypeScript callers compile TS to JavaScript using
their normal build tool and consume the generated `.d.ts` interface. This
binding library does not transpile TypeScript.

## Lifetime and failure contract

Top-level export rows and binding records are copied during registration. Their underlying
objects, descriptors, providers, capture dependencies and outer plugin leases
remain borrowed. The host keeps these owners alive until every VM proxy and
extracted closure has been released. Registration never transfers or retains
the source object's ownership. VM access and native mutation require the host's
normal single-threaded or externally serialized access discipline.
Generated native-object member tables are also borrowed: keep the C++ `Object`
binding owner (or C property/method arrays) alive with the native instance. The
C++ owner is deliberately non-copyable/non-movable to keep table addresses stable.
Native C++ exceptions become `CMETA_CALLBACK_ERROR` and then ordinary VM errors;
native mutations performed before an exception remain the native library's responsibility.

Duplicate export names and unsupported synchronous call contracts fail before
publication. Method providers may be called to bind metadata during validation
and declaration generation; they must be stable and must not invoke the native
method. Lua registration restores its original stack on failure; QuickJS leaves
the result undefined. Neither registration API writes globals. Limits bound
export/member counts and value conversion; invocation errors become VM errors.

Declaration generation validates the whole output before calling the sink.
Depth, node and byte budgets reject excessive or recursive shapes. A failing
sink may leave partial output, so file publication belongs to the caller.
Exact 64-bit integers use `bigint`, byte values use `ArrayBuffer`, and maps use
arrays of `{ key, value }`. Ordinary enum declarations use the symbols actually
returned by QuickJS. Fields without an assign provider and object methods are
read-only in the declaration. A field provider can still reject an assignment.

Current boundaries: generated adapters support synchronous, non-owning value
arguments, including zero-argument/void functions; their signatures do not depend
on the installed CMeta callable catalog. C++ deduction supports up to 16
parameters, arithmetic values, const/non-const/noexcept methods and mutable or
const fields. Pure C macros currently support bool/int/long/float/double scalars
and void returns, with 1–16 argument rows or the explicit zero-argument macro.
References, pointers, other value types and their ownership need an explicit
adapter; `Salts::Binding::Data<T>` is the C++ canonical conversion-provider
customization point. This entry point binds existing host-owned instances;
script-side constructors, automatic native ownership and script-to-C callback
trampolines are not exposed. TS generation requires ASCII names
(quoted keys and control characters are escaped), shape-based ordinary enums,
and static element/key/value descriptors for collections. Variant/custom value
shapes and enum-bits providers return explicit errors; custom objects can expose
provider-bound methods. The APIs compile native types plus external binding
declarations; no source scanner or IDL service definition is required.

## Architecture and compatibility

The shared export table lets both VMs and declaration tooling consume one native
surface. Generating runtime glue from IDL would introduce a second schema and a
runtime dependency; separate per-VM export lists would duplicate signatures and
names. The chosen common target depends only on CMeta, while the producer depends
on that target. Runtime bindings remain independent of the producer.

The existing CMeta callable catalog does not cover arbitrary native signatures
such as `int(int,int)` or `void()`. Exact adapters therefore carry a generated
FunctionData descriptor and compiler-checked thunk directly. Existing invokable
exports use their admitted CMeta adapter. Both share the same conversion and
declaration paths, without changing the installed Salts ABI. This is an explicit
choice of adapter, not a fallback after failed admission. Compared with adding
sol2 solely for Lua, this keeps a shared declaration surface for QuickJS and TS
and reuses the existing VM conversions without adding a dependency.

This is additive: existing value/object/invokable APIs retain their behavior.
Applications can migrate one registration site at a time, or roll back by using
their earlier explicit registrations. The new library target must be included
when packaging the bindings. Validation now traverses export metadata at
registration; invocation reuses the existing conversion/call adapters. No
performance improvement is claimed.

Focused Windows validation (after configuring `win-capture-release-user`):

```powershell
cmake --build --preset win-capture-release-user --target salts_native_c_frontend_test salts_native_cpp_frontend_test
ctest --preset win-capture-release-user -R "^salts_native_(c|cpp)_frontend_test$"
```
