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

## Retired legacy Candidate and DSO CI (M0 canonicalization)

The isolated `component_provider_candidate.c` / test-only FR2
`ComponentProvider Candidate` generator and its Salts 2.3 source,
negative-DSO and unpublished 2.3 release-package workflows were a
historical experiment; they are **not** the Native Context v1 ABI.
They are deleted rather than retained as a fallback. The unique
`IdlContract -> NativeSourceIR -> DataBindNativeExecution.context`
projection and original Salts Plugin / ServiceHost provider owner remain.
The canonical `ARTIFACTS PLUGIN` and its existing native tests remain
independent and are **not** removed by this change.

Orbit's genuine Salts3/Core0.1 generated Context-v1 dual-DSO lifecycle
qualification is tracked in Orbit M0g PR #18. Removing old Candidate
negative tests does **not** automatically establish equivalent general
failure-atomic / malformed Provider admission coverage; such coverage
belongs to the Salts Plugin/ServiceHost canonical owner, not a restored
second DataBind candidate generator.


## M0k Native owned VALUE qualification (not Orbit managed CallPort admission)

The canonical `NativeSourceIR` record renderer already emits owning
`string` / `bytes` storage as `databind_native_text` /
`databind_native_bytes`, plus generated `<Record>_clone`,
`<Record>_move`, `<Record>_clear` and complete
`<Record>_native_cmeta_value_bind` for a value whose fields have exact
canonical CMeta lifecycle providers. The `DataBindNativeExecution.context`
method ABI is orthogonal to that owning storage; it must not invent a
second callback form, binary serializer, registry or default receiver.

The existing `test_databind_native_context_v1` native executable now
runs an additional `Counter.Echo: OwnedPacket -> OwnedPacket` operation,
where `OwnedPacket` has an owning text label and owning raw bytes. Its
single generated Native method is
`int method(void *receiver, const OwnedPacket *, OwnedPacket *)`.
The application-owned method uses the **generated clone** to publish
caller-owned response storage before mutating its exact receiver.
Two differently bound receivers share the same immutable generated
FunctionAbi/invoke pointer but maintain independent totals. The C11
acceptance checks:

- deep-copy of stack-originating label and raw bytes, including embedded
  NUL and non-ASCII binary octets; changing the caller's scratch buffers
  must not change the owned request;
- two responses from separate receivers do not alias request buffers or
  each other; clearing the request does not invalidate either response;
- `OwnedPacket_native_cmeta_value_bind` admits a complete owning VALUE,
  while the original unbound Native execution remains fail-closed;
- malformed owned inputs reject clone without clearing an earlier
  destination, and application-error invocation does not mutate the
  receiver; moved records reset their source to semantic zero and clear
  twice safely.

**M0k.2a is complete as a separate Orbit Core prerequisite:** Orbit
Draft #27 has a bounded owning CMeta VALUE slot and an independent
post-commit execution borrow pin in the **original** CallPort, qualified
on Salts4/ServiceHost ABI2. The CFlow mailbox still only copies the uint64
token. That installed runtime API is not a second dispatcher or generator.

**M0k.2b generated opt-in:** with exactly one Service operation declaring
`[orbit(method,event), native_context(1), orbit_value(request_max,response_max)]`,
the single canonical NativeSourceIR compiler may now project owning
`string`/`bytes`-only Request/Response messages into:
- the original immutable Orbit CFlow Event/method identity (still a uint64 token);
- a caller-address-stable pair of CMeta VALUE binders and an explicit-budget
  `__orbit_port_init_value` helper;
- a typed `__orbit_call_async` using the existing bounded Core VALUE slot
  and its borrowed typed callback (valid only until callback return);
- one post-commit `__orbit_dispatch` using the original business symbol,
  `orbit_call_value_begin/stage_reply/end/observe` and generated
  `Record_clear` to release the app-owned response on success **or stale**.

This is a deliberately **single-managed-method-per-Service** opt-in. It
rejects a missing/duplicate/overflowing `orbit_value` annotation, unsupported
`native_context` version, optional/nullable, nested/collection, typed errors
and mixed scalar/managed Services before output publication. Ordinary scalar
Service wrappers and unannotated Native/Plugin artifacts are unchanged.
No new parser, transport, FunctionAbi, runtime or DSO loader is introduced.

M0k.2b is **not** accepted for product use until the generated managed
header compiles/runs inside a genuine installed Salts4 Orbit application
and the N/N+1 Provider DSO cancellation/retirement gates pass. Upstream
compiler CI alone is not proof of downstream memory safety.

This qualification is placed in the existing SaltsUtils native compiler
tests and primary CI. It adds no new project, CTest executable, alternate
engine, workflow, installed-consumer probe or production ABI.
