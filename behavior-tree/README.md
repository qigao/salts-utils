# Salts C11 Behavior Tree

This directory introduces the reusable C11 `Salts::BehaviorTree` target for
Praktor and other Salts-based applications. It deliberately does not translate
the old C++ runtime line by line.

## Initial slice

The first implementation provides:

- Bounded, caller-configured tree allocation.
- A single root and parent-before-child construction.
- Leaf callbacks plus Sequence and Fallback control nodes.
- SUCCESS / FAILURE / RUNNING status transitions.
- Per-control-node cursor state retained across RUNNING ticks.
- Explicit whole-tree reset and destruction.
- A single synchronous driver. Leaf callbacks are borrowed, caller-owned
  contexts; the library creates no hidden threads and never launches processes.
- CTest coverage for sequence resumption, fallback advancement, terminal reset,
  invalid topology, and capacity boundaries.

Tree node IDs are stable for the tree lifetime. Root depth counts as one.
Construction rejects trees deeper than the configured bound and the public
`SALTS_BT_MAX_DEPTH` limit. Terminal Sequence/Fallback results reset the control
subtree, so re-entering a completed branch begins at its first child.

## Not yet implemented

This is a first vertical slice, not BehaviorTree.CPP parity yet. The following
remain later work tracked by [SaltsUtils #671](https://github.com/qigao/salts-utils/issues/671):

- Parallel and ReactiveSequence.
- Decorators such as Retry, Repeat, Timeout, Delay, Inverter, ForceSuccess and
  ForceFailure.
- Stateful nodes such as RunOnce, ConsumeQueue and EntryUpdated.
- IfThenElse, WhileDo, Switch, PipelineSequence and SubTree.
- Tree-local blackboard, snapshot/merge policy, external async completion,
  cancellation/deadline integration, richer validation and diagnostics.
- Full node-by-node differential tests against Praktor's currently used
  `actions` behavior.

No YAML/DSL parser, process owner, native Plugin loader, or CFlow-backed scheduler
is added here. Praktor continues to parse YAML and map it to typed tree nodes.
Subprocess leaves should use `Salts::Process`; ServiceHost/Plugin retains native
module lifetime. CFlow remains a separate graph/execution abstraction.

## Build

The root SaltsUtils build installs the target as `Salts::BehaviorTree`:

```cmake
find_package(SaltsUtils CONFIG REQUIRED)
target_link_libraries(app PRIVATE Salts::BehaviorTree)
```

Public header: `salts/behavior_tree.h`.
