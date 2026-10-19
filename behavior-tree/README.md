# Salts C11 Behavior Tree

This directory introduces the reusable C11 `Salts::BehaviorTree` target for
Praktor and other Salts-based applications. It deliberately does not translate
the old C++ runtime line by line.

## Initial slice

The current C11 implementation provides:

- Bounded, caller-configured tree allocation, a single root and parent-before-child construction.
- Leaf callbacks plus Sequence, Fallback, cooperative Parallel/ReactiveSequence, Inverter, ForceSuccess/ForceFailure, bounded Retry/Repeat/KeepRunningUntilFailure, RunOnce, cooperative Timeout and non-blocking Delay.
- Predicate/selector-driven IfThenElse, Precondition, WhileDo and Switch.
- Host-context adapters for PipelineSequence, EntryUpdated and ConsumeQueue. They use typed callbacks; optional abort hooks clean adapter-owned state on interrupted pipeline/queue work. The library does not impose Praktor's blackboard or workflow value representation.
- SUCCESS / FAILURE / RUNNING transitions with per-control-node state retained across ticks. Retry attempts, Repeat cycles, KeepRunningUntilFailure iterations and WhileDo iterations are bounded by the tree configuration (default 64, hard maximum 4096).
- Full topology validation before leaf side effects, plus explicit cancel/reset/destruction; API errors abort active execution and halt descendants. Cancel halts active descendants and retains tree topology for reuse.
- Reentrancy guards: attempts to reset/destroy/mutate the same tree from tick/halt callbacks return `SALTS_EBUSY`.
- Monotonic cooperative Timeout/Delay. Timeout invokes leaf halt callbacks for active descendants; Delay never blocks the driver.
- A single synchronous driver. Callback users remain caller-owned and must outlive the tree; this runtime creates no hidden worker threads and never launches processes.
- Cooperative Parallel advances each branch at most once per parent tick and retains terminal results. Optional branch hooks let the host own isolated context selection, terminal merge/close and cancellation cleanup. ReactiveSequence reticks from the first child and halts children to the right when an earlier child blocks or fails.
- CTest coverage for state transitions, predicate/selector routing, queue/pipeline/change-detection adapters, cancellation through reset/halt, invalid topology, callback errors and capacity limits.

Tree node IDs are stable for the tree lifetime; the root depth is one. Retry's limit is maximum attempts including the first attempt; Repeat's limit is its terminal child-cycle count. RUNNING preserves the active child for later ticks. Pipeline prepare/complete callbacks run once on their specified transitions. If a child is interrupted, the optional pipeline abort runs after descendant halt/reset. EntryUpdated's change callback owns its comparison snapshot. ConsumeQueue begin/finish callbacks own queue and item data; an initially exhausted queue succeeds without ticking the child. An active item is aborted after descendant halt/reset if interrupted, but not after successful finish. User callbacks run synchronously on the single driver thread.

## Not yet implemented

This remains **partial parity**, not a completed replacement for Praktor's current `actions` runtime:

- Tree-local blackboard storage. The new Parallel branch callbacks let the host create, select, finish/merge and abort isolated branch contexts, but the library does not dictate a snapshot/merge policy or store blackboard values.
- Workflow-level cancellation/deadline integration and any asynchronous host-completion adapter beyond cooperative leaf polling and halt callbacks.
- SubTree expansion is left to Praktor's compile-time adapter, which should resolve and expand named trees while preserving independent node state.
- Full node-by-node differential tests against the current Praktor `actions` behavior. Any known semantic mismatch must be documented before removing the C++ dependency.

No YAML/DSL parser, process owner, native Plugin loader, or CFlow-backed scheduler is added here. Praktor continues to parse YAML and map it to typed nodes. Subprocess leaves should use `Salts::Process`; Salts Plugin/ServiceHost retains native module lifetime. CFlow remains a separate graph/execution abstraction.

## Build

The root SaltsUtils build installs the target as `Salts::BehaviorTree`:

```cmake
find_package(SaltsUtils CONFIG REQUIRED)
target_link_libraries(app PRIVATE Salts::BehaviorTree)
```

Public header: `salts/behavior_tree.h`.
