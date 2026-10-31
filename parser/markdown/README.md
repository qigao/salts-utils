# Canonical C11 Markdown grammar migration

**Status:** Canonical parser installed and downstream cutover merged in
[SaltsUtils #724](https://github.com/qigao/salts-utils/pull/724) and
[Stun #372](https://github.com/qigao/stun/pull/372). Remaining acceptance tracker:
[SaltsUtils #722](https://github.com/qigao/salts-utils/issues/722);
Stun downstream cutover: [Stun #368](https://github.com/qigao/stun/issues/368).

This module originates from qigao/stun `shared/markdown`, source master
`78995ecf8c5365c7f001b4501cb29a109971ba9c`.

Only one production Markdown parser is allowed. `Salts::MarkdownParser` owns
source bytes and RichDoc grammar; `Salts::MarkdownHtml` consumes the validated
RichDoc and performs SAFE (production) / SPEC (trusted test-only) HTML rendering.
Stun consumes the installed upstream targets and has removed its duplicate
grammar and parser-only test sources.
No runtime cmark/MD4C/Mistune fallback.

## Test ownership

Move all parser-only C11 tests, complete immutable CommonMark 0.31.2 652-example
corpus, SPEC 652-ID and SAFE 576-ID SHA-256 baselines, independent cmark-gfm
468-case strict output gates and 360 GFM compositions, OOM/invalid-input,
Unicode17, reference/URL tests, and oracle scripts into this repository.

Stun keeps *only* `ui_core_test.c` (UI state, Rect, CMeta; move any parser
assertions first), `markdown_tango_candidate_test.c`, real
`flexUI/tests/test_markdown_widget.cpp`, and UI/Tango/renderer integration.
Stun's `verify_single_markdown_parser.py` becomes a downstream single
imported-parser guard. It must not retain duplicate compiled grammar files.

## Source-span warning

`salts_md_get_document_view().document.blocks[].byte_begin/end` index into the
**decoded semantic text** arena; `raw_markdown` preserves the exact original
input independently. They are NOT original Markdown byte offsets. Until a
separately qualified original-byte span mapping exists, TurboAgent MUST NOT
publish these semantic offsets as source-file evidence. It can still use the
indexed semantic text for FTS5 with explicit provenance.

## Hard gates

- Run *real* native parser C11 CTests and independent full corpus gates on
  Linux/Windows/macOS; preserve strict EXPECTED HTML and corpus hash. The
  original Stun checkpoint is CI #39017661257, commit `65e9b55b`.
- `Salts::Unicode` remains imported from the canonical Salts SDK. No local
  Unicode provider, dummy header, test-only parser replacement or missing
  dependency fallback.
- Keep SDK packaging and headers in existing SaltsUtils build graph; do not
  create standalone installer/consumer projects or new probe CI workflows.
- Only **after upstream exact-head qualification** migrate Stun actual runtime
  and remove the old parser source and parser-generic test sources. Keep the
  STUN UI wrappers only if their old signatures/ownership are verified.
- Only after the accepted installed upstream SDK and Stun cutover can
  TurboAgent #306/#307 use Markdown parsed structural sections.

## Optional byte provenance (#725)

`salts_md_parse_with_source_map()` runs the same grammar and owns a separately
bounded semantic-to-raw map. `salts_md_get_source_map()` borrows that map until
document destruction. Ordinary `salts_md_parse()` and all existing public
struct layouts retain their behavior and allocate no provenance arrays.

Each semantic byte is covered once. LITERAL entries support exact byte
subranges; TRANSFORMED entries associate an entire decoded entity, escaped
punctuation or normalized code payload with its entire original lexeme.
Transformed intervals are atomic: their offsets cannot be interpolated.
UNKNOWN entries explicitly reject exact-source attribution. Map capacity
exhaustion or allocation failure rejects the whole unpublished document.

The map records origins at semantic writes and grammar admission, rather than
searching equal text afterward. This distinguishes repeated text and retains
original CRLF and UTF-8 byte coordinates. Node sidecars include proven raw
and content intervals for document/text, direct inline tokens, root paragraphs,
ATX headings, root Setext headings and root fences. Other container spans,
metadata and normalized scratch-buffer origins remain unproven. In particular,
escaped table-cell scratch text can yield UNKNOWN; no text-search recovery is
attempted. `semantic_complete` says only that semantic entries have known
origins, not that every node or metadata field is mapped.

The alternative of storing raw offsets in `salts_md_block` would break its
semantic ABI; a second parser or substring search would guess provenance.
The separate opt-in arrays preserve that ABI at an explicit memory cost bounded
by node and map-entry budgets. Consumers can stop requesting maps without a
data migration. Format version 1 describes the map representation, not a
historical document or grammar identity.

The caller must still bind retained SourceRevision bytes, parser revision and
extraction options before publishing historical evidence. This API does not
perform version reads, assign global revision IDs, map metadata, or qualify
TurboAgent FTS/source evidence. Those gates remain open in
[SaltsUtils #725](https://github.com/qigao/salts-utils/issues/725).

Native tests verify exact lexemes, entity/escape expansion, repeated text,
CRLF, CJK/emoji, semantic/SAFE/SPEC parity, UNKNOWN and failure-atomic budgets.
The existing C11 grammar suite also runs with provenance enabled.
