# DataBind Binary Format

This directory owns the canonical DataBind Binary wire/layout representation.
The public/runtime format identity and compiler/config spelling are `binary`;
the concise schema annotation namespace is `@bin.*`.

Binary wire helpers, version metadata and compiler-side layout overlay live here.
BinaryLayoutIR is validated by the compiler and emitted as BinaryLayoutPlan
providers. Runtime readers/writers expose CSerde tokens to MessagePlan/native
execution. Binary owns wire representation; CMeta providers own native storage
and lifecycle. Unsupported layouts fail before decoding or publishing output.
