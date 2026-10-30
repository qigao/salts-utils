# Salts Search — Phase A (implementation candidate)

Tracking: [algorithm-first epic #704](https://github.com/qigao/salts-utils/issues/704), [A0 #705](https://github.com/qigao/salts-utils/issues/705), [A1 TRE #706](https://github.com/qigao/salts-utils/issues/706), [A4 acceptance #709](https://github.com/qigao/salts-utils/issues/709).

## Current scope

**A0** literal matcher and **A2** bounded line delineator have implementation candidates in stacked drafts. Both remain non-installed and do not imply A4 acceptance. The functions live in a private, non-installed header `src/search_literal.h`; they are deliberately not published as an SDK ABI before A4. No filesystem APIs, indexes, async backends, Rust or fd/rg/Everything subprocesses are involved.

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

## Algorithm and complexity

A0 uses the **Two-Way** string matching algorithm (Crochemore–Perrin): maximal suffix in two lexicographic orders yields the critical position and period. For periodic patterns it retains the matched prefix as *memory* after shifts; for aperiodic patterns it uses the guaranteed-safe long shift. The prefix equality test chooses between them. Pattern preprocessing is O(m), each find is worst-case O(n+m) comparisons with O(1) per-call workspace, not including the O(m) owned needle bytes.

Single-byte patterns use standard C `memchr`. The planned SIMD prefilter in #705 depends on the canonical Salts `cmeta_simd_scan` restoration in [salts-core #71](https://github.com/qigao/salts-core/issues/71), rather than copying a second architecture-specific scanner into this module. **No SIMD speedup claim yet.**

### Dependency / gating status

- [x] A0 scalar Two-Way implementation and deterministic C11 differential tests (implementation candidate).
- [ ] A0 canonical Salts SIMD scan integration and scalar/SIMD parity.
- [ ] A1 TRE pinned C library, exact Tagged NFA and match-priority gate (not started in this draft).
- [x] A2 line-delimiting primitive + deterministic C11 tests (stacked draft, not accepted).
- [ ] A2 context-before/after output and any requested encoding conversion; A3 Glob/Gitignore; A4 full C11 conformance and CI.
- [ ] B0 Salts FS/CFlow file/mmap integration (blocked by A4 and upstream migration).

Do not create installer or consumer projects/workflows to test this module. Register its test in the existing `BUILD_TESTS` CTest graph and keep the work **Draft** until the exact-head CI and full A0 acceptance are verified. No fallback engine or extra search parser is allowed.
