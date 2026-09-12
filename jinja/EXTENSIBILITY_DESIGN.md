# Jinja Runtime and Extensibility Design

## Scope and status

This document records the accepted six-stage direction. It is a design contract,
not a claim that every capability below is implemented. The additive opaque
configuration and extended-entry-point approach has been approved; existing public
structures and entry points retain their ABI. No alternate executor or silent
compatibility path is introduced.

Implemented groundwork: render-local shared storage, template instances,
include/import/from, inheritance, block dispatch, explicit module values and
module initialization states. Module string conversion emits its body; module
repr identifies the module rather than a self reference.

Still required: generic data adapters, a compiled-template cache, extension-tag
execution, and host filters/tests. The host function registry is now available
as a frozen environment snapshot; the latest regression run covers that subset,
not full Jinja conformance.

## Ownership and execution invariants

| Owner | Authoritative state | Lifetime |
| --- | --- | --- |
| Environment | Frozen configuration, extension registry, loader, compilation cache | Environment |
| Compiled template | Immutable instructions, functions, lexical layout, source locations | Retained compiled artifact |
| Render session | Quotas, cells, values, instances, closures, module cache, captures | One render |
| Execution frame | Actual instance, activation, context, block position, output state | One call |

- A saved frame restores its actual instance, not the inheritance chain root.
- Function identity is a defining instance plus a function index.
- Module values and self references have distinct value and node kinds.
- Compiled artifacts never retain render-local cells, closures, or borrowed views.
- All nested execution consumes the same session quotas.
- Root cells own export values. An export index may reference cells but must not
  maintain a separately mutable copy of those values.
- Context snapshots freeze visible bindings, not the contents of referenced
  mutable objects such as namespace values.
- Rendering is single-threaded by default. Environment destruction, registry
  construction, and cache invalidation require quiescence unless an explicit
  concurrency contract is added. Loader and host callback safety is separate.

## Stage 1: Explicit module values and frame identity

Module references expose exports, captured body, and identity. Self references
expose block lookup. Block references carry the defining instance and function;
ancestor lookup must preserve that identity through macros, loops, and includes.

Module initialization follows NONE -> INITIALIZING -> READY or FAILED. Only READY
modules can be published to callers or returned from the session module cache.
A failed render releases the failed module with the rest of its session; it does
not publish partially initialized exports or recover by retrying another path.

Acceptance evidence includes module/self repr distinction, module identity and
exports through containers, context-dependent imports, cyclic imports, multilevel
super calls, and a parent loop executing multiple iterations with child overrides.

## Stage 2: Independent, bounded render resources

Implemented increment: the runtime configuration exposes independent cell and
activation quotas through additive streaming and string render entry points.
Cells remain stable, per-activation allocations. The default cell quota is now
fixed rather than derived from the entry template. VALUE chunks and complete
retained-byte accounting remain required before this stage is complete.

Entry-template expression and cell counts must not determine global capacity.
Introduce independently configurable limits for cells, values, activations,
instances, retained payload, and total retained allocation. Preserve existing
limits for visits, call depth, captures, strings, and source admission.

Every reservation follows check quota -> checked arithmetic -> allocate ->
initialize -> publish. CAPACITY and OUT_OF_MEMORY remain distinguishable. Failed
reservations do not expose handles or partially initialized values.

Reuse Salts memory facilities for bounded stable-address chunks. Do not introduce
a handwritten arena or resize storage after publishing pointers into it. A single
collection snapshot remains contiguous because existing consumers use pointer plus
count views; separate snapshots may occupy different chunks.

Account for allocated chunk capacity, alignment, metadata, retained strings,
capture buffers, and externally retained payload. Current retained bytes decrease
on release; cumulative work counters never reset during nested execution.
Unpublished temporary storage still counts while it is live.

The initial compatibility adapter keeps existing entry points and public layouts.
New configuration uses an opaque object and additive entry points rather than
appending fields to public structs. Default limits and their memory consequences
must be documented before migration; they must not be chosen from the entry
template's complexity.

Validation: small entry/large dependency, sibling dependencies, exact capacity,
capacity plus one, checked multiplication/addition overflow, allocation failure,
escaped closures after storage growth, and cumulative budget exhaustion.

## Stage 3: Host functions, filters, and tests

Build a registry before environment publication, then freeze it. Registration
declares category, name, positional and keyword signature, context requirements,
callback, and user-data lifetime. Duplicate names require explicit replacement
policy; registrations must not silently replace builtins.

The current public subset registers named scalar functions through
`JINJA_CMETA_REGISTRY`. Registration rejects invalid UTF-8, dotted names,
duplicate names, and builtin collisions; replacement is explicit. Creating an
environment copies descriptors and names, so later registry mutations do not
change published environments. Calls share the active render session and
included/imported templates use the same environment lookup.

Use one binding and invocation pipeline: evaluate arguments, validate the
signature, admit budget, invoke, validate the result, transfer it into session
ownership, then publish. Preserve the distinct semantics of functions, filters,
and tests; tests must return boolean values.

Callbacks receive a restricted call context, not executor internals. Argument
views expire on return. Returned strings must be copied or explicitly transferred;
result builders allocate through the session's checked reservation interface.
Safe-string marking is explicit, never inferred from host origin.

Propagate callback failures with template name, source offset, operation, and
callee identity. An error does not roll back bytes already delivered to a
streaming renderer or external host side effects. Hosts needing transactions
must provide their own explicit transaction boundary.

Synchronous native callbacks are trusted code. Interpreter quotas cannot preempt
an arbitrary C callback and do not constitute a sandbox. Recursive callback
entry into the same session is rejected unless a supported nested-call operation
preserves the existing execution frame and quotas.

Validation: binding and expansion, duplicate keywords, invalid return types,
ownership transfer on failure, safe/unsafe strings, context visibility, callback
failure diagnostics, and nested-call admission.

## Stage 4: Capability-based data adapters

Reuse CMeta/CSTL capabilities before adding explicit adapters. Separate sequence
length/index/iteration, mapping lookup/key iteration, object attribute lookup,
and callable invocation. Do not normalize every object into a generic map.

Lookup has three outcomes: FOUND, ABSENT, ERROR. An enabled Jinja lookup-order
policy may try another capability only after ABSENT. Permission, type, and
execution errors propagate immediately.

Borrowed host data remains valid and immutable throughout rendering. Adapters
that permit mutation must provide a snapshot or explicit version/invalidated-view
contract. Iterator state belongs to the render, is bounded, and is destroyed on
normal and failed exits. Methods use the same callable protocol as host functions.
Arbitrary Python descriptor execution is not implicitly emulated.

Validation: empty and missing values, non-string keys, stable iteration order,
invalid metadata, failed lookups, iterator cleanup, borrowed-view invalidation,
and namespace construction from supported mapping adapters.

## Stage 5: Compiled-template cache

Cache immutable compiled artifacts, not initialized modules. The key includes
template name, loader source version/generation, and compile/extension identity.
Without source versioning, require explicit invalidation; do not guess freshness.

Bound both entries and retained bytes. Eviction drops the cache reference, not
references held by active renders. Loader calls and compilation occur outside
cache synchronization if concurrent cache access is introduced. Publication checks
that the source/configuration generation still matches; stale work is not published.

A cache hit must still consume the render's logical dependency/source admission
and instance budgets. Warm and cold execution must not differ in whether a render
can bypass resource limits. Record source size in the compiled artifact for this
purpose; cache ownership accounting is separate from per-render admission.

Keep environment lifetime requirements explicit: active renders and borrowed
template environment pointers prevent environment destruction. Do not introduce
an environment/template reference cycle.

Cross-render initialized-module caching is a separate design. Current modules
contain cells, closures, and potentially mutable exports. Moving them into the
environment would violate render isolation. This stage intentionally does not
claim to implement persistent module caching.

Validation: key separation, explicit invalidation, changed source, eviction with
active readers, failure during compile/publication, byte and entry limits, and
identical render budget enforcement for cache hits and misses.

## Stage 6: Extension tags and translation

Register supported statement/block shapes before compilation. The parser owns
expression parsing, nesting, lexical scopes, and source locations. Extensions
lower into validated internal instructions; they cannot take over the executor
or recursively invoke public render to reset budgets.

Compile trans into a message, plural-selection expression, and placeholder
arguments consumed by a translation callback. Preserve parameter evaluation and
escaping semantics. Debug is opt-in, uses bounded context views, supports
redaction, and does not expand arbitrary host objects automatically.

Reject unregistered tags during compilation. Define the supported extension
grammar explicitly rather than promising unrestricted parser replacement.

Validation: nesting, malformed endings, lexical visibility, source diagnostics,
plural selection, placeholder binding, escaping, disabled debug, redaction,
callback errors, and shared resource limits.

## Migration, rollback, and evidence

Deliver stages independently, keeping existing entry points operational. Public
API additions need approval and C/C++ header compatibility coverage. Each stage
updates README and the compatibility matrix with implemented behavior and explicit
remaining limits. No stage is complete solely because adjacent tests pass.

Use failing targeted tests before a behavior fix, then the existing environment,
native-function, and CMeta regression groups. Add focused tests for each new
contract and memory-safety checks when resource ownership changes. Performance
claims require measured typical and saturated workloads, including peak retained
memory; no speedup is assumed from the design alone.

Rollback removes only the affected stage's changes. No persistent data migration
is required, and no parallel legacy executor is retained as a fallback.

### Stage 2: VALUE span storage implementation

The executor now indexes retained collection values through a render-owned span
store rather than a single allocation sized to the entire VALUE quota. Each
snapshot reserves a contiguous span before recursive evaluation; extending the
CSTL span directory does not relocate published VALUE addresses. Logical slot
counts remain owned by the executor, and the directory only indexes those slots.

Unpublished context construction is the sole growable-span path. It uses a
bounded CSTL vector and refreshes the context entry pointer after each append.
After construction, later contexts and snapshots use separate spans. All spans
are destroyed at render cleanup; nested VALUE payloads remain borrowed.

Dictionary snapshots reserve both keys and values. Sequence repetition,
concatenation, and dictionary-item iteration now explicitly reserve their output
instead of assuming a previously allocated global backing array. Capacity errors
remain distinct from allocation failure.

Lookup is O(log span count), with O(1) access to the newest span. Snapshot data is
allocated for its requested size; context and directory vectors retain bounded
capacity slack. This is not yet a total retained-byte budget, and no performance
improvement is claimed without measurement. The integration has not been built
or tested in this increment. Remaining validation includes nested imports with
context, closures retaining earlier snapshots, dictionary item iteration,
concatenation/repetition, exact quota boundaries, and allocation failure cleanup.

### Stage 2: shared requested-byte ledger

VALUE spans now use the allocator-bound CSTL Vec interface. One render-owned
ledger charges vector owners, directory storage, snapshot buffers, alignment
headers/slack, growth overlap, and temporary element copies. Activation/cell
storage borrows the same ledger. Standalone cell stores retain a local ledger;
the renderer binds its shared ledger before creating the first activation.

Admission precedes allocation. Failed allocation publishes neither storage nor
charges; failed first-span construction releases its temporary directory owner.
Deallocation returns the original requested size. Ledger lifetime extends beyond
all borrowing storage owners. Object-count quotas remain independently enforced.

The renderer currently uses an uncapped internal byte ledger: no public total-byte
quota is exposed while strings, node/binding/call workspaces, closures, template
instances, and other render allocations remain outside this ledger. Requested
bytes exclude opaque CRT allocator overhead and external borrowed payloads. The
updated Salts SDK supplies cstl/allocator.h and cstl/vec_alloc.h; legacy vec_t ABI
is unchanged. This is a storage accounting increment, not completion of the
render-wide memory budget design.

### Stage 2: output lifetime and conversion scratch

The string-rendering entry point now owns the ledger through final output-copy
creation. The executor borrows that same ledger; streaming entry points retain
an executor-local ledger. Nested templates never create a replacement ledger.
Output storage uses allocator-bound CSTL Vec, including its owner, capacity,
terminator, alignment overhead and growth overlap. A failed append preserves the
old bytes and propagates CAPACITY separately from OUT_OF_MEMORY.

Numeric-text normalization uses an exactly sized, receipt-owned buffer. It is
released after conversion, including invalid-digit and parsing-error exits.
The final malloc-compatible result copy is admitted while output storage is
still retained; successful publication transfers that allocation to the caller
and removes its charge without freeing it. Failure leaves the caller output
NULL. Existing public layouts and caller free() semantics are unchanged.

This increment changes private storage only. Capture/repr/format strings and
loader-triggered compilation still need allocation coverage before a public
total-byte quota can be enabled. Loader and host callback allocations require
an explicit ownership boundary, not an assumed interpreter-wide sandbox.
Validation remains pending for this increment: string/environment/native/CMeta
regressions, embedded NULs, numeric normalization, output growth failure,
copy-overlap admission and release-to-zero checks.

### Stage 2: sealed captures

Capture slots now own allocator-bound output buffers rather than unmetered tstr
allocations. Their directory, individual owners, terminating NULs, spare capacity
and overlapping growth allocations all borrow the render's byte ledger. A failed
capture append preserves the old content and returns the allocator's precise
CAPACITY or OUT_OF_MEMORY status; it does not retry with unmetered storage.

Capture completion seals the buffer. Module bodies, block results and macro
results borrow that sealed buffer through render cleanup. Switching templates
does not change its owner or reset its charges. Temporary local handle copies
only borrow the capture slot's owner; cleanup destroys each slot exactly once.

The output-storage boundary tests and five adjacent regression targets passed
for the preceding output/conversion increment. Capture integration is not yet
validated. Remaining byte-accounting work includes repr/format scratch and
loader-triggered compilation; the public total-byte quota is still withheld.

### Stage 2: repr and formatting scratch

Repr, concatenation, XML attribute formatting and measured floating-point fields
now use the same allocator-bound text storage as captures and output. Formatting
gets mutable access only before publication; allocation failure preserves the old
buffer, and cleanup drops every temporary owner. Logical pending-string counts
remain separate from physical capacity and overlapping-growth charges.

The scalar-to-UTF-8 conversion used by numeric %c still calls the legacy tstr
encoder, then appends and releases its temporary. This is an explicit remaining
unmetered path, not an exception to a published total-byte guarantee. A proposed
allocation-free Unicode encoding interface needs approval before replacing it.
Compilation and trusted loader/host allocations still require their own boundary.
The capture integration passed all six adjacent regression targets; this scratch
increment still requires its focused resize test and adjacent regressions.

### Stages 2 and 5: compiled artifact byte identity

Compiled templates record their original source length before newline trimming,
and the sum of actual retained allocation requests. The retained-allocation entry
checks both multiplication and aggregate overflow before malloc/calloc semantics
are applied. Failed admission does not advance the counter and preserves its
precise failure status through compilation and named-template publication.

The count includes the artifact object, expression/instruction/function/parameter
arrays, all retained string regions, lexical-scope and cell-array capacities,
and the owned diagnostic name. Logical cell counts cannot reconstruct this total:
deduplication may leave allocated capacity greater than the number of cells used.
Only construction changes these fields; published artifacts keep them immutable.

This metadata is groundwork for byte-bounded compiled-cache ownership and equal
source admission on hits and misses. It is not post-allocation enforcement of a
render limit: parser, lowering and layout scratch still need pre-allocation
admission before loader-triggered compilation can honor that limit. No new public
quota or cache API is exposed by this increment. Planned checks cover exact empty
artifact size, original source length, named-artifact deltas and overflow paths.

### Stage 2: expression and statement-header allocation boundaries

The generated Lemon engine exposes only its workspace size, initialization and
finalization to its private caller. Source copies and that complete workspace
can use a borrowed CSTL allocator. The grammar keeps its fixed 192-slot stack;
generation fails if an unaccounted growable stack is enabled. No global or
thread-local allocator switch is used. Stack-local AST objects are separate from
heap requested-byte accounting.

Expression, filter, assignment, with-initializer, macro signature, call, block,
loop and template-reference parsing now have private allocator-aware entries.
Default arguments and speculative header-boundary expressions propagate the
same allocator. Original entries explicitly select the default allocator and
share the same parser implementation. Allocation failure never retries with a
different allocator; temporary copies and engines are released before return.

The expression-workspace increment passed nine parser/runtime regression targets.
The header increment adds failure injection at every allocation ordinal, zero
quota and exact peak checks across fifteen header/reference scenarios. Template
AST construction, binding analysis and lowering still need to receive the
allocator from the compiler session before render-wide admission is complete.

### Stage 2: allocator-owned template trees and binding analysis

Template parsing now offers a private allocator-aware entry. Its node vector
uses allocator-bound CSTL storage; the tree's storage owner copies callbacks and
borrows their context until destruction. Parsing work, node capacity and growth
overlap, statement headers, translation-header expressions and IF validation
share the supplied allocator. Default entry points use the same implementation.

Successful reparse publishes the completed replacement and releases the old tree
through its original allocator. Failed reparse leaves the original tree and its
allocator untouched. Analysis counterparts carry an explicitly supplied allocator
through root/loop frame analysis, undeclared-name discovery, macro capabilities,
and descriptor construction. Temporary work never becomes owned by the input AST.

The shared parser allocation boundary remains independent of executor structures.
It distinguishes CAPACITY from OUT_OF_MEMORY and never silently changes allocators.
Tests cover callback-copy lifetime, cross-allocator replacement, syntax/quota
failure preservation, exact construction peaks, and analysis release back to the
retained-tree baseline. Compiler lowering and layout still need to supply the
session allocator before loader-triggered compilation is fully admitted.

### Stage 2: allocator-bound compiled artifact ownership

Compiled artifact construction now copies an allocator and records every retained
allocation in a fixed, checked registry. The root object includes this registry
in its own requested-byte charge. Named semantic fields are views of the registry's
storage, not independent owners. Release walks the registry using each original
requested size, then releases the root through the same copied allocator.

The allocator context must outlive the compiled artifact. Failure leaves previous
registry entries owned and releasable, without resetting their charges or retrying
with default allocation. Default public compilation explicitly uses the default
allocator; connecting the compiler session and loader remains required before
this changes public render-budget enforcement. Focused tests cover root admission,
retained-array capacity failure, copied callbacks and the registry's fixed bound.
