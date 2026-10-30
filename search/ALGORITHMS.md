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

- Comments, escaped leading `#` or `!`, `!` negation, escaped trailing spaces,
  CRLF, slash-rooted, basename, recursive and directory-only patterns.
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

## Algorithm and complexity

A0 uses the **Two-Way** string matching algorithm (Crochemore–Perrin): maximal suffix in two lexicographic orders yields the critical position and period. For periodic patterns it retains the matched prefix as *memory* after shifts; for aperiodic patterns it uses the guaranteed-safe long shift. The prefix equality test chooses between them. Pattern preprocessing is O(m), each find is worst-case O(n+m) comparisons with O(1) per-call workspace, not including the O(m) owned needle bytes.

Single-byte patterns use standard C `memchr`. The planned SIMD prefilter in #705 depends on the canonical Salts `cmeta_simd_scan` restoration in [salts-core #71](https://github.com/qigao/salts-core/issues/71), rather than copying a second architecture-specific scanner into this module. **No SIMD speedup claim yet.**

### Dependency / gating status

- [x] A0 scalar Two-Way implementation and deterministic C11 differential tests (implementation candidate).
- [ ] A0 canonical Salts SIMD scan integration and scalar/SIMD parity.
- [ ] A1 TRE pinned C library, exact Tagged NFA and match-priority gate (not started in this draft).
- [x] A2 line-delimiting primitive + deterministic C11 tests (stacked draft, not accepted).
- [ ] A2 context-before/after output and any requested encoding conversion.
- [x] A3 stage1 compiled Glob NFA with bounded caller scratch + C11 differential tests (stacked Draft, not final).
- [x] A3 stage2 memory-only Gitignore source/scope/negation evaluator and bounded C11 tests (Draft; final CI pending).
- [ ] A3 automatic rule discovery, hidden/CLI glob/repository policy and full differential conformance; A4 full C11 acceptance and CI.
- [ ] B0 Salts FS/CFlow file/mmap integration (blocked by A4 and upstream migration).

Do not create installer or consumer projects/workflows to test this module. Register its test in the existing `BUILD_TESTS` CTest graph and keep the work **Draft** until the exact-head CI and full A0 acceptance are verified. No fallback engine or extra search parser is allowed.
