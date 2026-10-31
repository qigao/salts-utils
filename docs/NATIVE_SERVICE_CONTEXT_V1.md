# Canonical Native Service receiver ABI v1

**Status:** Draft implementation. This is an additive, opt-in semantic of the sole
`IdlContract -> NativeSourceIR -> DataBindNativeExecution` lowering.

## IDL

```text
schema Game [version(1)];
message PingRequest  { uint64 seq; }
message PingResponse { uint64 ack; }
service Room {
    [orbit(10,100), native_context(1)] Ping: PingRequest -> PingResponse;
    [orbit(20,101), native_context(1)] Pong: PingRequest -> PingResponse;
}
```

`native_context(1)` is **not** a new IDL grammar or reflection engine. It
uses the already supported operation annotation list. The compiler accepts
exactly one positive supported version `1`; duplicates, bare annotations,
extra arguments and unknown versions fail before output publication.
The first slice rejects typed-error Service shapes for contextual operations.
An opted-in Orbit Service requires every operation to consistently have the
same receiver convention. Default/unannotated native methods retain their
old exact signature.

## Single ABI

```c
/* Canonical business C implementation, selected by one IdlOperation. */
int databind_4_Game_4_Room_4_Ping(
    void *receiver, const PingRequest *request, PingResponse *response);
```

The `receiver` belongs to the **Service instance**, not the current thread,
global plugin, socket, or Actor ID. It is the exact object already acquired
and pinned by application configuration / Salts ServiceHost provider Scope.
The authoritative `DataBindNativeExecution` API already has a
`void *context` field; the generated canonical `__databind_invoke` now
uses that field for `native_context(1)` methods and rejects null.

The existing canonical `FunctionDesc/FunctionAbi` still describes the
Request/Response parameters and integer business status. The receiver is
the pre-bound execution owner, not a new per-message FunctionAbi argument.
The generated `__databind_execution_bind(receiver, out)` copies the
original immutable Native execution descriptor, fills its context, and
checks the existing descriptor validity. The static descriptor is never
mutated while another Service instance is using it. A second
`Native Service` or Orbit-specific business function ABI is forbidden.

## Orbit projection

The existing optional `DATABIND_NATIVE_ENABLE_ORBIT` projection emits a
per-Service value `__orbit_binding { orbit_call_port *port; void *receiver; }`.
The application initializes it **once during Service activation**, before
publishing the CFlow Actor. Its post-commit `__orbit_dispatch(bound, token)`
reads the generation-checked method identity through the existing CallPort
and invokes exactly the same canonical business function with that receiver.

Every message stays in one Salts CFlow Actor Mailbox; Orbit owns only its
fixed response credit and terminal obligations. No global/TLS current
Service, second Actor runtime, alternate CMeta invocation, registry lookup
per packet, transport fallback, or extra parser is introduced.

The application owns and serializes the receiver's writable state. It must
not release or move that state, or the original ServiceHost Scope/Plugin
provider, until accepted Actor work, callbacks and CallPort terminals are
fully retired. Cancellation does **not** undo already executed state changes.

## Deliberate fail-closed boundaries

- The unannotated Native service remains unchanged; it is neither implicitly
  contextual nor transparently adapted.
- The contextual generated ordinary Native execution requires a nonnull
  explicit bound receiver; the static execution descriptor stays unbound.
- The older CFlow typed projection, which cannot pin a contextual receiver,
  returns UNSUPPORTED_SHAPE until that owner has an explicit binding API.
- Managed string/bytes, optional/nullable/collection values, typed errors and
  Streaming are **not** implemented by the current Orbit u64 adapter.
- No generated header may pull the older Salts 2.x DataBind/CMeta runtime
  into an Orbit application using the installed Salts 3 SDK. That split
  remains explicit and fail-fast.

## Verification

The existing `test_databind_service_native_codegen_tool` builds the same
canonical Contract and checks the generated v1 prototype, native execution
receiver invocation, Context binding factory and Orbit Dispatcher; negative
schemas reject mismatched or unsupported receiver versions.

The independent Orbit Native CTest must instantiate at least two different
Rooms from one generated contract, with separate live Actor/CallPort/State,
and verify interleaved calls cannot affect the other instance's state.
It must also verify real Provider/Plugin N->N+1 lifetime when combined with
the same generated dispatcher. This is a Draft gate, not a release claim.
