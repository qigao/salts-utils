# DataBind Binary Format

This directory owns the canonical DataBind Binary wire/layout representation.
The public/runtime format identity and compiler/config spelling are `binary`;
the concise schema annotation namespace is `@bin.*`.

Binary wire helpers, version metadata and compiler-side layout overlay live here.
No TBE compatibility header or target is installed. Historical
`runtime/tbe_typed.*` remains intentionally outside this rename because it
still mixes native/lifecycle and Binary concerns; its semantic split is tracked
by #236.
