# Salts Search — Phase A (implementation candidate)

Tracking: [algorithm-first epic #704](https://github.com/qigao/salts-utils/issues/704), [A0 #705](https://github.com/qigao/salts-utils/issues/705), [A1 TRE #706](https://github.com/qigao/salts-utils/issues/706), [A4 acceptance #709](https://github.com/qigao/salts-utils/issues/709).

## Current scope

**A0** literal matcher, **A2** bounded line delineator, **A3-stage1** Glob byte automaton, and **A3-stage2** memory-only ignore-rule evaluator have implementation candidates in stacked Drafts. All remain non-installed and do not imply A4 acceptance. The functions live in a private, non-installed header `src/search_literal.h`; they are deliberately not published as an SDK ABI before A4. No filesystem APIs, indexes, async backends, Rust or fd/rg/Everything subprocesses are involved.

`salts_search_literal_compile` copies a bounded binary pattern into an owned immutable object and computes both critical factorizations. `salts_search_literal_find` reports the first exact half-open byte span at/after the requested offset. Empty needle matches at the supplied valid starting offset. A failed call never returns a partial match. A compiled object can be read concurrently by independent searches but cannot be mutated or destroyed until all searches settle.

### A2 line scanner implementation candidate (stacked on A0)

`search_lines.[ch]` consumes arbitrary caller-borrowed byte chunks into a
caller-owned, fixed-capacity line arena and invokes one non-reentrant callback
per **complete** logical line. All absolute byte offsets and one-based line
numbers are `uint64_t`; no file IO or allocation is performed by the scanner.
Binary NUL is either preserved raw or rejected with an explicit typed status.
The default newline policy splits LF and CRLF while treating lone CR as data;
optional universal mode also splits lone CR. CRLF across calls is tracked
without over-counting the line-capacity budget; an empty file or terminal
newline does not invent another line. A callback may stop; only genuinely
consumed bytes are counted. Limit, binary and overflow errors are terminal and
never reported as search success.

This component is a line-delimiting adapter, **not** a second regex engine.
One test uses the same A0 literal matcher to find overlapping matches spanning
input chunk boundaries; TRE can be connected to the callback only after A1.
The full A2 contract for higher-level context-before/after output and encoding
conversion remains **open**; this PR adds no public claiming of full rg parity.

### A2 stage 2: bounded before/after context collection

`search_context.[ch]` is a private, memory-only C11 consumer of A2's
`salts_search_line` callbacks and the exact A0 Literal Matcher. It is **not**
a second Regex or line parser. A caller-provided ring of `before_count`
line records and fixed-size byte storage owns only retained before-context;
incoming lines and output events are borrowed for each callback.

When a line matches, the collector emits up to `before_count` preceding
lines not already emitted, then one MATCH event; it continues to emit up to
`after_count` subsequent nonmatching lines. Overlapping context windows
are deduplicated, and `group_break` identifies noncontiguous result groups.
All event line numbers and absolute byte offsets are `uint64_t`; original
binary/NUL bytes are preserved without decoding. A callback STOP is terminal,
with an explicit status distinct from capacity/invalid/closed; if a before
line exceeds the fixed ring's per-line capacity it returns LIMIT, never a
partial success. The match pattern remains a borrowed immutable A0 owner;
the caller must keep it and the history storage alive through the scan.

This only implements fixed-literal line context. Matching through the unique
TRE Regex Provider depends on #706, and complete A2 coverage still requires
conformance fixtures for more complex binary/encoding policies and grouping
with file sources. No FS, SQLite, mmap, Rust or external process is involved.

### A3 stage 1: compiled byte-oriented Glob NFA (stacked candidate)

`search_glob.[ch]` compiles one immutable token program supporting literal
bytes, `?`, `*` within a path component, specially bounded `**/` and trailing
`/**`, bracket character classes/ranges/negations, escapes and ASCII-only
case-insensitive mode. It uses epsilon closure with two **caller-owned** state
vectors, O(path_bytes * token_count) worst-case time and O(token_count) scratch,
without recursion, regex compilation, filesystem access or backtracking.
Relative normalized slash paths are checked for NUL, `.` and `..` traversal;
patterns that target only components must not contain an interior slash.
Compiled programs own their tokens and may be shared across threads when each
scan supplies independent state scratch.

**Stage-1 boundary:** this Glob NFA does not parse ignore files. Stage 2
below adds in-memory ignore rule scopes, negations and precedence by reusing
the same compiled Glob program. Hidden file policy, symlink traversal,
Git repository boundaries, Unicode folding and platform filesystem admission
remain open in [A3 #708](https://github.com/qigao/salts-utils/issues/708).
A3 stays Draft; no complete fd/rg compatibility claim is made.

### A3 stage 2: bounded in-memory hierarchical ignore rules

`search_ignore.[ch]` takes caller-provided **complete bytes** of an ignore file,
an explicit rule source (`.gitignore`, `.ignore`, `.rgignore`, Git global/info
or explicit `--ignore-file`), and a normalized root-relative owner directory.
It compiles every nonempty rule using the **same** Stage-1 Glob NFA; it does
not open a filesystem, execute Git, use a separate Regex parser, or discover
the repository's global ignore configuration. The caller retains bounded
rule slots; each successful rule owns its immutable scope and Glob tokens.
A malformed rule, NUL byte, capacity exhaustion or syntax failure aborts the
entire input file and frees all rules added by that call; previous rules are
unmodified. `out_error_line` identifies the first offending line.

Rule semantics implemented in this candidate:

- Directory-only `build/` matches folders named `build` at every depth;
  a trailing slash alone does not anchor to the owner directory.
- Comments, escaped leading `#` or `!`, `!` negation, escaped trailing spaces,
  CRLF (including final trailing CR without LF), slash-rooted, basename, recursive and directory-only patterns.
- Explicit source priority: `.rgignore` > `.ignore` > `.gitignore` >
  `.git/info/exclude` > Git global > explicit ignore file. Within one source
  kind, deeper directory scopes override parent scopes; equal scopes use
  last matching rule. These priorities are intended to match ripgrep's
  documented ignore-file tiers, **not** CLI `--glob` overrides.
- A query checks each ancestor directory before evaluating the leaf.
  If an ancestor is excluded, later child-specific negative rules cannot
  restore the leaf. Re-inclusion of a parent must itself be effective.
- Input path and directory scope must already be normalized, slash-separated,
  root-relative and NUL-free; `.`/`..` components are rejected rather than
  cleaned. ASCII-only case-insensitive matching is optional; Unicode
  normalization and full case folding are not claimed.
- Caller allocates query scratch at least `2*(max_pattern_bytes+1)` bytes.
  No lookup allocations or recursive backtracking occur. A frozen rule set
  can be matched concurrently using separate scratch buffers.

**Still open for complete fd/rg compatibility:** discovery and reload of
real `.gitignore`/`.ignore`/`.rgignore` files by the Salts File Walker, Git
repository-boundary detection and global configuration ownership, CLI glob
overrides, hidden-file policy, explicit symlink/OS path rules, full Unicode
folding and comprehensive differential tests on full directory trees.
These are separate from this memory-only algorithm gate; no filesystem
search or FileCatalog is being implemented in this stage.

Supporting tests: `test_salts_search_ignore` covers nested and cross-source
precedence, exception/parent exclusion, escaped markers/trailing spaces,
CRLF/NUL, rollback, stable 64-bit-friendly indexing and bounded resources.

### A3 stage 3: one traversal policy for CLI glob, Ignore and Hidden

`search_policy.[ch]` adds a **memory-only** traversal decision, never a
new filesystem walker or regex parser. It reuses the exact Stage-1 Glob
NFA and Stage-2 Ignore rule set. It evaluates each ancestor directory
before its child and publishes `VISIT`, `SKIP_FILE` or `PRUNE_DIRECTORY`
with one typed reason and winning rule index. The caller supplies bounded
immutable Override rules, path input and scratch; all query operations
are allocation-free. Each compiled Override owns only its Glob tokens.

- CLI `-g 'pattern'` whitelists; CLI `-g '!pattern'` excludes (inverse of
  `.gitignore` negation). Last matching CLI rule wins and takes priority
  over Ignore and hidden defaults.
- Once positive CLI Glob rules exist, an unmatched **file** is excluded,
  but an unmatched **directory** remains traversable. This prevents
  false negatives from pruning `src/` for a search for `*.c`.
- An excluded directory (whether by Ignore, explicit Glob, or hidden
  default) is pruned before any child can re-include itself. A matching
  positive rule for that directory can override its Ignore/hidden state.
- `include_hidden=false` hides each basename beginning with `.` unless
  an earlier Ignore whitelist or explicit override matches that entry.
  Unicode/case conversion beyond ASCII is not claimed.
- The basename-only rule fix in #714 ensures whitelist on a parent
  directory does **not** incorrectly allow every hidden descendant.

Native file discovery, Git repository detection, global config load,
real symlink traversal policy, and full-tree Git/rg conformance are still
not implemented. They belong to the later filesystem-adapter gate and
must not be inferred from these in-memory tests.
### A1 exact TRE Tagged NFA adapter (candidate; not yet accepted)

`search_regex.[ch]` binds **one** upstream [TRE](https://github.com/laurikari/tre)
0.9.0 C engine, pinned by `qigao/vcpkg-cache` at
`laurikari/tre@71bfcaf0af3994384987c6c2679ed7d078ffe189`.
The current candidate uses a **frozen minimal cache-contract backport**
`qigao/vcpkg-cache@3086b284d35eb77a2818e0a3efb39508c8d7d544`,
which has passed infrastructure compatibility CI in
[vcpkg-cache Draft #249](https://github.com/qigao/vcpkg-cache/pull/249).
This changes the dependency pin **only in this Draft candidate**, not master.

- Required `find_package(unofficial-tre 0.9.0 CONFIG REQUIRED)` links the
  single static `unofficial::tre::tre`; missing binary caches must fail
  configured CI. No second regex parser, dynamic plugin or builtin TRE 0.8.
- Uses binary-safe `tre_regncompb` and `tre_regnexecb`, rejects
  `tre_have_backrefs` and `tre_have_approx` **before any execution**
  while preserving TurboScript's existing shared-package fuzzy behavior.
- POSIX **leftmost-longest** is explicit: `a|ab` on `ab` returns `ab`.
  This is deliberately **not** rg default leftmost-first. An actual
  prioritized TNFA proof is still required for claimed rg default parity.
- Compile max 4096 bytes; each query defines a strict subject cap not larger
  than `INT_MAX`, guarding upstream TRE's internal 32-bit offset truncation.
  Exact input offsets/typed error statuses; no silent truncated successful
  search. The first-stage API only finds the first match in one complete
  caller-provided line slice. It does **not** support repeated suffix-rescan,
  multiline or arbitrary unbounded regex budget.
- TRE's internal allocations and CPU budget still require dedicated
  worst-case TNFA proof/fault-injection and exact-head native tests. This
  Draft cannot be considered A1 accepted solely because source compiles.

### A4 preparation: one query-selected matcher and line-context integration

`search_matcher.[ch]` unifies the already implemented C11 Two-Way
Literal and pinned TRE 0.9 exact Regex algorithms under a single
compile/find-first/destroy contract. A required kind selects LITERAL
or REGEX once at compile time. There is no alternate Regex provider,
and failed execution never falls back to the other matching algorithm.

All matches return the same bounded half-open byte offsets and typed
NOT_FOUND/INVALID/LIMIT/UNSUPPORTED/NO_MEMORY/ENGINE_ERROR statuses.
The maximum subject length is explicitly capped at INT_MAX for both kinds.

The A2 `search_context` adds a matcher-based constructor while keeping
its existing literal-only constructor. Both share the same bounded
line scanner, before/after context ring and result stream. TRE receives
the complete original line without substring slicing, preserving
start/end anchoring, alternation and byte offsets. Tests compare raw
byte spans for Literal/TRE, verify POSIX leftmost-longest selection,
NUL bytes, limits and complete chunked line input with before/after
context produced by TRE.

This remains a private non-installed API. Multiline regex, multi-match
enumeration, Unicode normalization, and full ripgrep leftmost-first
parity remain outside this gate. Android/iOS target TRE package cache-only
recovery is separate: the earlier iOS CI source-built a missing target
TRE archive although native source/build and CTests passed.

### A4 stage2: exact first-match highlight spans (Draft candidate)

`salts_search_context_event` now carries `has_match_span` and
`match_byte_start`/`match_byte_end` as **absolute, half-open byte
offsets** for MATCH events. The offsets come from the original Two-Way
or unique TRE exact matcher result on the **complete line**, added to the
64-bit `line.byte_start` with overflow checks. They are not derived
from C string termination or decoded characters: NUL, UTF-8 byte input,
large offsets above 4 GiB and zero-width matches are unambiguous.

BEFORE/AFTER events have `has_match_span=false` and zeroed offsets;
an empty match has `has_match_span=true` and equal start/end. Tests
exercise both legacy Literal context and the unified TRE/Literal entry,
including anchors, alternating regex precedence, UTF-8 raw bytes,
empty-line `^$`, and near-UINT64_MAX source positions.

**Scope boundary:** This is the FIRST match on each result line, not
full multi-match highlighting. Literal overlapping matches and true
multiple TRE spans still require separate A4 acceptance. Never
enumerate Regex matches by reslicing the remaining suffix, as that
changes anchor/word-boundary context. No alternate regex parser,
fallback or filesystem I/O is introduced.

### A4 stage3: bounded Literal all-hit enumeration (Draft candidate)

`salts_search_matcher_enumerate_literals` enumerates exact occurrences
on the *original* unchanged caller-owned byte slice using the SAME
compiled C11 Two-Way matcher. The caller explicitly selects whether
hits may overlap. Every hit has ascending half-open byte offsets, and
a zero-length pattern emits one hit at each byte boundary including EOF.
The implementation advances by at least one byte for zero-width hits,
so there is no infinite loop.

A strict `max_hits` caps delivered callbacks. When another hit exists
beyond the cap the function returns `LIMIT`, **not an apparently
complete success**. A callback may stop, returning `STOPPED` with the
already delivered hit included in `out_emitted`. No per-hit allocation
or another matching backend is used. The existing independent reference
tests cover overlapping `aba` in `ababa`, NUL, empty patterns,
budget exhaustion, callback stop, and randomized small byte slices.

This API intentionally returns `UNSUPPORTED` for TRE Regex. Its
correct multi-hit iteration cannot use repeated substring suffix scans:
they change anchors and word-boundary state. A source-position-aware
multi-hit extension must be implemented and qualified **within TRE**;
no PCRE2/system regex or hidden fallback is authorized. A2 context
continues to carry first match per matching line until the true
multi-match result protocol is independently accepted.

### A4 stage4: original-subject TRE multi-hit (Draft candidate)

Upstream TRE 0.9.0's binary-safe `tre_regnexecb` searches from offset 0
and returns only the first match. Slicing the subject and calling it
again changes `^`, `$`, word-boundary and newline assertions. The
single canonical TRE port now has an **additive patch** in
[vcpkg-cache Draft #252](https://github.com/qigao/vcpkg-cache/pull/252):
`tre_regnexecb_from(regex, full_bytes, length, min_start, ...)`.
It runs the **same exact Tagged NFA**, consuming the original bytes and
blocking initial-state admission before `min_start`. Original TRE APIs
and TurboScript approximate/backreference behavior are unchanged.

A4's `salts_search_regex_find_from` wraps this single C provider.
`salts_search_matcher_enumerate_regex` uses the unchanged whole input
on every iteration, returns absolute-by-line half-open byte spans, and
permits overlapping or nonoverlapping results. Each zero-width match
advances the allowed start position by one byte; an EOF match can be
emitted once. The same `max_hits` and callback STOPPED contract applies
as with Literal, with explicit LIMIT if a further hit exists. There
is no alternate regex engine, no suffix-based retry and no filesystem I/O.

The shared TRE port revision is now **0.9.0#1** and the pinned frozen-v8
compatibility candidate is
`qigao/vcpkg-cache@62f7de15195134b19c02ab45b6c883b74f96c88a`,
which contains byte-identical TRE port blobs to canonical Draft #252.
The old cache contains unpatched 0.9.0 packages; the patched port
changes its ABI, requiring producer cache publication and exact native
link/execution before acceptance. This is a **Draft-only dependency
pin**, not master or a published SDK.

Correctness gates include `^a` on `aa` (no spurious match at 1),
`\\bfoo` on `xfoo` (no invented word boundary), newline-mode `^a`
after a real newline, POSIX longest `a|ab`, embedded NUL, zero-width
at EOF, overlap, max-hits exhaustion and explicit rejection of forced
backtracking/approx flags in the new API.

**Execution workspace bound (Draft):** the unique TRE port's new
`tre_regnexecb_from()` now checks arithmetic for TNFA workspace size before
allocation, and limits the combined per-invocation matching workspace and
capture tag allocation to **8 MiB**. Both private `find_first()` and
`find_from()` now use that *one* bounded exact executor, so the first-match
and line/context path cannot bypass it by calling legacy `tre_regnexecb()`.
Overflow/overbudget returns `REG_ESPACE`
(which the existing private adapter currently maps to `NO_MEMORY`). This
bound does not limit *pattern compilation*, total query CPU time, cumulative
allocation across repeated hits, or independent concurrent callers; those
remain separate A4 gates. The same TRE patch now also aligns the four
TNFA pointer-bearing workspace regions to **pointer width**, rather than
using `long` (4 bytes on Windows LLP64). The allocation size includes all
four maximum alignment paddings in the checked 8 MiB budget. The existing
`test_salts_search_regex` includes an executable 1,024-capture overbudget
regression; its **new exact-head CI** is required before acceptance.
Legacy TRE APIs retain their matching semantics and the single shared
static provider retains approximate/backtrack support.

**Per-enumeration scan budget (Draft):** the existing private matcher
has an additive `salts_search_matcher_enumerate_regex_bounded()` entry
with `max_scans` to cap the number of full-original-subject TRE
executions independently of `max_hits`. The terminal unsuccessful
lookup and the extra hit-count probe consume scans. If there is no
budget to prove enumeration complete, return `LIMIT` (with the exact
number of callbacks already emitted), **never** a truncated success.
`max_scans=0` conservatively returns `LIMIT` even on a nonmatching
subject. The previous private enumeration signature and its behavior
remain unchanged; consumers must explicitly opt in to the scan budget.

**Performance remains OPEN:** repeating an original-subject TNFA scan
may still cost O(max_scans × subject_length × per-state work) within
the new bounded entry. A scan-count limit is **not** a hard CPU-time,
regex-compilation memory, cancellation, or aggregate concurrent-owner
limit. Hard TRE execution workspace is separately covered by the
existing 8 MiB per-call budget. Adversarial runtime, broad randomized
replay and concurrency/fuzz must still be proven before A4 can close. This implementation is not claimed
leftmost-first (ripgrep default) or one-pass linear multi-regex search.

## Algorithm and complexity

A0 uses the **Two-Way** string matching algorithm (Crochemore–Perrin): maximal suffix in two lexicographic orders yields the critical position and period. For periodic patterns it retains the matched prefix as *memory* after shifts; for aperiodic patterns it uses the guaranteed-safe long shift. The prefix equality test chooses between them. Pattern preprocessing is O(m), each find is worst-case O(n+m) comparisons with O(1) per-call workspace, not including the O(m) owned needle bytes.

Single-byte patterns use standard C `memchr`. The planned SIMD prefilter in #705 depends on the canonical Salts `cmeta_simd_scan` restoration in [salts-core #71](https://github.com/qigao/salts-core/issues/71), rather than copying a second architecture-specific scanner into this module. **No SIMD speedup claim yet.**

### Dependency / gating status

- [x] A0 scalar Two-Way implementation and deterministic C11 differential tests (implementation candidate).
- [ ] A0 canonical Salts SIMD scan integration and scalar/SIMD parity.
- [x] A1 candidate single TRE static provider wrapper and POSIX priority admission, awaiting exact-head binary package/CTest verification.
- [ ] A1 full regex resource/leftmost-first parity and cache-only cross-RID admission.
- [x] A4 prep: query-selected Literal/TRE matcher + bounded A2 context (CI #41058232362 SUCCESS).
- [x] A4 stage2 candidate: first-match absolute byte highlight span on existing callbacks (Draft, CI pending).
- [x] A4 stage3 literal all-hit enumerator (overlap/no-overlap, empty, bounded callbacks; Draft, CI pending).
- [x] A4 stage4 source candidate: TRE original-subject min-start plus bounded multi-hit wrapper (Draft, exact-head CI pending).
- [ ] A4 TRE multi-hit native/ABI, hard resource bounds, broad fuzz/perf/concurrency and rg leftmost-first semantics.
- [x] A2 line-delimiting primitive + deterministic C11 tests (stacked draft, not accepted).
- [x] A2 memory-only bounded before/after literal context collector (stacked Draft; exact-head CI pending).
- [ ] A2 encoding conversions, unique TRE regex integration and further streaming conformance.
- [x] A3 stage1 compiled Glob NFA with bounded caller scratch + C11 differential tests (stacked Draft, not final).
- [x] A3 stage2 memory-only Gitignore source/scope/negation evaluator and bounded C11 tests (Draft; final CI pending).
- [x] A3 stage3 memory-only CLI glob/hidden traversal policy, typed prune decisions and bounded C11 tests (stacked Draft).
- [ ] A3 real ignore discovery, Git repository boundary/global config, symlinks and full-tree differential conformance; A4 full C11 acceptance and CI.
- [ ] B0 Salts FS/CFlow file/mmap integration (blocked by A4 and upstream migration).

Do not create installer or consumer projects/workflows to test this module. Register its test in the existing `BUILD_TESTS` CTest graph and keep the work **Draft** until the exact-head CI and full A0 acceptance are verified. No fallback engine or extra search parser is allowed.
