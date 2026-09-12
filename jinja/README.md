# Jinja CMeta

`Salts::JinjaCMeta` is a C11 Jinja template module reading immutable application
data through CMeta descriptors. Its parser, compiled instructions, runtime,
and output sink are independent of Mustache. Jinja links CMeta/Core publicly
and Unicode/CSTL privately; it does not link either Mustache or QueryVM.

The exact implemented feature matrix, known semantic differences, error
categories, resource protocol, and pinned Jinja oracle are documented in
[`JINJA_COMPATIBILITY.md`](JINJA_COMPATIBILITY.md). The current public API is
an incomplete Jinja implementation, not a selectable legacy compatibility profile.

`batch(linecount, fill_with=None)` supports lazy grouping, keyword arguments,
Unicode character iteration, shared iterator aliases, and optional final-row padding.
For example, `{{ [1,2,3]|batch(2,0)|list }}` renders `[[1, 2], [3, 0]]`.
Input consumption and retained rows obey the existing render resource limits.

`slice(slices, fill_with=None)` splits a materialized input into columns, unlike
`batch` row grouping. `{{ range(5)|slice(2)|list }}` renders `[[0, 1, 2], [3, 4]]`.
It snapshots input on first consumption; iterator aliases share the column cursor.

`name is filter` and `name is test` query this engine's available builtins and
the environment's registered filter/test names, not the complete upstream
registry. Unknown names return false. Unsupported filter invocations still fail
compilation, even inside a conditional branch.

`format` and string `%` share Python-style percent formatting:
`{{ '%s:%04d'|format('item', 7) }}` renders `item:0007`, and
`{{ '%(name)s' % {'name': 'Ada'} }}` renders `Ada`. Positional tuples, named
fields, flags, width/precision (including `*`), and `%%` are supported.
Text width and precision count Unicode characters. A safe format string preserves
Markup escaping; plain strings remain plain. Numeric `%` still computes a remainder.
Conversions use the existing int64/uint64/double representation and string budget;
malformed fields or mismatched arguments return `JINJA_CMETA_ERR_RENDER`.

## Build and Link

```cmake
find_package(SaltsUtils CONFIG REQUIRED)
target_link_libraries(jinja_app PRIVATE Salts::JinjaCMeta)
```

```c
#include <jinja_cmeta.h>
```

The installed target supplies `include/jinja`; the public header is installed
as `include/jinja/jinja_cmeta.h`.

## Runtime Resource Configuration

The additive `jinja_cmeta_runtime.h` header provides an opaque
`JINJA_CMETA_RUNTIME_CONFIG` and `jinja_cmeta_render_ex` /
`jinja_cmeta_render_string_ex`. Existing public structures and entry-point
signatures are unchanged.

Cell and activation limits apply to the complete render, including loaded
templates and retained macro closures. Configuration defaults are 262,144 cells
and `JINJA_CMETA_DEFAULT_MAX_NODES` activations. Zero is an explicit prohibition,
not a request for defaults. Invalid resource identifiers fail without mutation;
unrepresentable capacities return `CAPACITY` before template execution.

Existing render entry points now use the fixed default cell limit rather than
multiplying the entry template's cell count by `max_nodes`. They retain the
resolved `max_nodes` activation limit. Explicit runtime configuration overrides
those two limits only; node, active-binding, and other existing limits still
apply. Cells are allocated with activations on demand, not preallocated to the
configured maximum. Larger workloads can explicitly raise the cell limit.

VALUE storage and total retained-byte accounting have not yet migrated to this
configuration. The configuration is not a total-memory or sandbox guarantee.

## Compile Options

`jinja_cmeta_compile(source, options, error)` accepts a compile-only configuration;
pass `NULL` for defaults or initialize `JINJA_CMETA_COMPILE_OPTIONS` with
`JINJA_CMETA_COMPILE_OPTIONS_INIT`. Zero-filled options are invalid.
The former two-argument signature has been removed; callers must be rebuilt.

Options cover six UTF-8 delimiters, line statement/comment prefixes,
`trim_blocks`, `lstrip_blocks`, `keep_trailing_newline`, and `newline_sequence`
(`"\n"`, `"\r\n"`, or `"\r"`). Defaults remove one trailing physical newline and
normalize physical newlines to LF. Explicit string escapes and application data
are unchanged. Source and option views are borrowed only until compile returns;
the returned template owns its compiled bytes. Errors retain original source byte offsets.

Compilation rejects duplicate block names across the whole parsed template,
including dead branches and macro bodies. Names match exactly, without Unicode
normalization; diagnostics identify the first redefinition's original opening tag.
Block rendering and inheritance use the compiled block metadata and shared
render-local template instances.

## Named Templates and Loaders

`jinja_cmeta_env_create` copies compile options and a synchronous loader table;
initialize options with `JINJA_CMETA_ENV_OPTIONS_INIT` or pass `NULL` for defaults.
`jinja_cmeta_env_compile` compiles supplied source with an owned diagnostic name.
`jinja_cmeta_env_load` obtains source through `load`, compiles it, and calls
`release` exactly once after every successful load, even if compilation fails.
Failed loads retain their own cleanup responsibility. Missing names return
`JINJA_CMETA_ERR_NOT_FOUND`; an absent loader or loader I/O failure is
`JINJA_CMETA_ERR_LOADER`. Named loads use the environment's bounded compiled
template cache; there is no filesystem adapter or automatic retry.

Names are exact, explicit-length UTF-8 keys, including empty names and embedded
NULs. Errors own a bounded name prefix, its byte length, and a truncation flag;
offsets remain original template byte offsets. `JINJA_CMETA_ERROR` changed layout:
rebuild every caller, including benchmarks, against the current header and DLL.

Use an environment synchronously on one thread without callback reentry.
Release its templates before destroying it; loader userdata belongs to the caller.
Runnable C usage and loader callbacks are in
[`test_jinja_environment.c`](test/test_jinja_environment.c); C++ usage is in
[`test_jinja_cmeta_header_cpp.cpp`](test/test_jinja_cmeta_header_cpp.cpp).

Runtime `include`, `import`, `from`, `extends`, and ancestor `super` calls use
one render session. Default imports reuse a module within that render; imports
with context create independent instances. Module exports and `self` block
references have distinct value types. Modules stringify to their captured body
and have a named `TemplateModule` repr.

Loaded instances, closures, and captures share cumulative resource limits.
Environment-level compiled-template caching and host extension registration are
not implemented. The remaining work and ownership contracts are recorded in
[the extensibility design](EXTENSIBILITY_DESIGN.md).

## Supported Syntax

Block headers accept Jinja's optional trailing colon, for example
`{% if true: %}X{% endif %}`. This also applies to `elif`/`else`, `for`,
`autoescape`, `filter`, block capture, and macro/call headers, with the same
rules under custom delimiters and line statement prefixes. Unicode whitespace
around the colon is accepted; diagnostics retain their original byte offsets.
Colons are not accepted after ordinary assignments, `with`, `print`, `do`, or
closing tags. String, dictionary, and slice colons remain expression syntax.

`{% print expression, expression %}` emits each expression in order, without a
separator, using the same safety and autoescape rules as interpolation. An empty
`{% print %}` emits nothing. Parenthesized tuples remain a single value:
`{% print 1,2 %}|{% print (1,2) %}` produces `12|(1, 2)`. A trailing comma
without another expression is a syntax error. The statement uses the existing
expression/instruction budgets; no separate runtime or Mustache dependency is added.

`{% do expression %}` evaluates without formatting or emitting its result. It
accepts the existing expression syntax, including tuples, and retains side
effects, short-circuiting, errors, and expression resource limits. For example,
`{% set xs={'a':1,'b':2}|items %}{% do xs|first %}{{xs|list}}` produces
`[('b', 2)]`. The tag is built in here; upstream requires the
[`jinja2.ext.do` extension](https://jinja.palletsprojects.com/en/stable/extensions/#expression-statement).
The environment can register filters, tests, scalar globals, and custom tags.
Custom tags use expression-call syntax such as `{% audit(user, level) %}`;
their callback result is evaluated and discarded, while callback failures abort
the render. This does not add arbitrary Python methods or host-object mutation.

Missing path leaves remain Undefined; continuing lookup through a missing
segment, such as `missing.name` or `user.missing.name`, returns a render error.

Simple `{% set name=expression %}` assignments store native values in the current
render, including Undefined, safe strings, collections, ranges, and shared items
iterators. Parenthesized names are accepted. `if` shares its enclosing scope;
each loop iteration, loop `else`, and `autoescape` block isolates local writes.
Assignments do not mutate the application CMeta object or persist across renders.
Binding slots have a separate `max_nodes` bound; same-scope replacement and scope
exit reuse slots, while referenced payloads remain alive until render cleanup.
Loop metadata takes precedence over an outer variable named `loop`; assigning
`loop` within a for block or using it as a for target is a syntax error.
Tuple targets support swaps (`{% set a,b=b,a %}`), nested tuples, duplicate
names (last write wins), and empty tuples. RHS evaluation and all unpack shape
checks precede binding publication. Unpacking accepts the existing list/tuple,
dictionary key, range, Unicode character, borrowed sequence, and items iterator
values; cardinality/type mismatches return `JINJA_CMETA_ERR_RENDER`. Iterators
share their original cursor and consume one extra item to check exhaustion.
Target trees have the existing 64-node parser bound, independently of RHS nodes;
temporary values use the bounded render collection workspace. LoopContext and
macro values are supported; host callable registration remains unimplemented. Builtin `range`,
`dict`, `namespace`, `cycler`, `joiner`, and bound methods support aliases,
container storage, computed call targets, and `is callable`.

`cycler('odd', 'even')` retains its options across loops: `current` reads without
advancing, `next()` returns then advances, and `reset()` returns to the first option.
`joiner(', ')` returns an empty string on its first call and its original separator
afterwards. Aliases share state; options and separators retain their value identity
and safety flag. Both helpers live until rendering ends, with no host-owned handles.

`namespace()` accepts a native dictionary, a sequence of key/value pairs, and
keyword overrides. Namespace aliases share identity across loop scopes; attribute
assignment, tuple targets, and block capture targets are supported. For example,
`{% set ns=namespace(n=0) %}{% for i in range(3) %}{% set ns.n=ns.n+1 %}{% endfor %}{{ ns.n }}`
outputs `3`. Attribute writes to ordinary dictionaries or CMeta objects fail.
External CMeta struct/map constructor inputs are not supported. Builtin callable
aliases are supported; noncallable targets fail at execution after eager arguments.
Each render owns the objects; object count and fields per object are each bounded
by `max_nodes`. Writes retain dictionary snapshots in the existing VALUE workspace:
an update with `n` resulting fields costs `2*n` slots, plus one slot per object.
Repeated updates can exhaust that cumulative budget even without adding fields.

`{% with a=1,b=outer %}...{% endwith %}` introduces a local scope. Each initializer
reads the enclosing scope, not earlier bindings in the same header. Name/tuple
targets, duplicate names (last write wins), and empty headers are supported.
Bindings disappear on exit; namespace and iterator values retain shared identity.
Initializers use the existing expression syntax and resource limits; trailing
commas, capture headers, and attribute targets are rejected. Pending leaf bindings
are bounded by 64 and active bindings by `max_nodes`.

`{% filter trim|escape %}...{% endfilter %}` captures a block and applies the
existing filters. Filter arguments see the block's local assignments. Nested
filter/set captures share the bounded capture storage. The final string is written
verbatim to the parent sink, without another autoescape pass; non-string final
results return `JINJA_CMETA_ERR_RENDER` instead of implicit formatting.

Block `{% set text %}...{% endset %}` captures output without sending it to the
external sink. Nested captures and tuple targets are supported. Optional filter
chains (`{% set text|trim %}`) use the existing filters and see assignments in
the capture's local scope; only the final result is assigned to the parent scope.
Autoescape marks the captured input and final result safe, converting a non-string
filter result to a string. With autoescape off, filters can return native values.
Each capture owns a `tstr` until render cleanup; closed buffers are never modified.
Capture handles are separately bounded by `max_nodes`, and all captured payloads
share a cumulative `max_string_bytes` limit, separate from expression-string and
final-output budgets. Failure returns the original status and does not leak the
unfinished capture; earlier external output cannot be rolled back.

```jinja
{{ user.name }}
{{ (user.name) }}
{{ true }}
{{ none }} {{ None }}
{{ not(false) }}
{{ -42 }}
{{ 7 / 3 }} {{ 5.5 // 2 }} {{ 2 ** -2 }}
{{ (+8) | safe }}
{{ 'plain text' }}
{{ "a|b" | safe }}
{{ user.age >= 18 }}
{{ user.name == 'Ada' }}
{{ missing or 'fallback' }}
{{ active and user.name }}
{{ missing is undefined }}
{{ user.name is string }}
{{ user is mapping }}
{{ user.html | safe }}
{% if active %}active{% elif user.pending %}pending{% else %}inactive{% endif %}
{% if not archived %}visible{% endif %}
{% if not (not user.active) %}active{% endif %}
{% if true %}always{% elif false %}never{% endif %}
{% if 42 %}nonzero{% elif 0 %}never{% endif %}
{% if 'text' %}nonempty{% elif '' %}never{% endif %}
{{ true == 1 }} {{ 2 >= 1 }} {{ not 1 == 2 }}
{{ user.name if active else 'offline' }}
{{ user.name ~ ':' ~ user.age }}
{{ user.name + '!' }}
{{ user.name|length }} {{ [1, 2]|count }}
{{ missing|default('fallback') }} {{ missing|default([1, 2])|length() }}
{{ users[-1]['name'] }} {{ 'Aé😀'[1] }}
{{ users[::-1][0].name }} {{ 'Aé😀'[::-1] }}
{{ [0, 1, 2, 3][1:4:2] }} {{ range(1, 9, 2)[::-1] }}
{{ [1, user.age, [3]][1] }} {{ (1, 2) }} {{ {'name': user.name}['name'] }}
{% for item in users or [] %}{{ loop.index }}:{{ item.name }}{% else %}empty{% endfor %}
{% for key in {'a': 1, 'b': 2} %}{{ key }}{% endfor %}
{% for item in users %}{{ loop.cycle('odd', 'even') }}:{{ loop.changed(item.age) }}{% endfor %}
{# comment #}
{%- if active -%}trimmed{%- endif -%}
```

Conditions combine dotted paths, `true`/`false` or `True`/`False`, `none`/`None`, signed decimal,
binary, octal, or hexadecimal `int64`,
decimal/exponent doubles, strings, bounded numeric arithmetic, and supported comparisons with balanced parentheses, repeatable unary
`not`, and `and` / `or`. An adjacent sign remains part of an integer literal; a spaced sign is a
unary operator. Interpolation accepts grouped dotted paths and literal expressions with
repeatable `not`, logical expressions, and
conditional expressions;
integer output is decimal, float output uses locale-independent Python/Jinja shortest-round-trip spelling,
and boolean expression output follows Jinja's `True`/`False` spelling. An integer outside the `int64` range fails compilation with
`JINJA_CMETA_ERR_CAPACITY`.
Contextual words `and`, `or`, `in`, `if`, `else`, and `is` can be names where an
operand is expected; their operator meanings remain unchanged. `not` remains unary.
Any positive number of `not` operators produces a boolean, including even counts.
Unparenthesized test arguments also accept `in`, `if`, and `not` as names, with
the usual postfix operations. `and`, `or`, and `else` end that argument position;
a following bare `is` is a syntax error. To apply a conditional to a no-argument
test result, group the test, for example `(value is defined) if flag else false`.
Path, boolean, integer, float, and string leaves support `==`, `!=`, `<`, `<=`, `>`, and `>=`;
booleans compare as `0` or `1`, and comparison binds before unary `not`. Same-type literal
comparisons are folded while compiling; comparisons containing a path or unlike literals are
evaluated during rendering. Runtime paths support CMeta booleans, signed/unsigned integers, floats, and
strings. Numeric comparisons remain exact across signedness and at integer/double boundaries; scalar
NaN comparisons are unordered, so only `!=` is true. Container equality, membership, dictionary keys
and `loop.changed` use identity-or-equality for their elements: the same NaN can find its own key or
element, while independently produced NaNs stay distinct. Lexicographic ordering continues past an
identical element. Native container aliases still traverse their contents to validate deferred borrowed
fields; all runtime comparisons and dictionary-key checks share the render's value traversal limits.
These limits bound repeated visits, but do not eliminate shared-container DAG re-traversal. Strings are decoded first and
compared by explicit-length UTF-8 byte order, so escapes and embedded NUL participate without
locale collation or Unicode normalization. Two missing paths compare equal; a missing path and a
defined scalar, or unlike scalar types, compare unequal. Ordering those unlike/undefined values,
or using enum, object, or sequence operands, returns `JINJA_CMETA_ERR_RENDER`. Comparisons
may be chained or used as parenthesized operands. A chain evaluates operands once from left to
right and stops at its first false step; a lone `=` is a syntax error. Compiled comparison steps,
path bytes, and decoded string bytes are owned by the template and do not borrow the source.
`in` and `not in` use the same comparison precedence. String membership performs exact decoded
UTF-8 substring matching. Borrowed CMeta sequences support membership for the current scalar
element types; scanning is allocation-free and bounded by `max_nodes`. Invalid non-empty sequence
metadata returns `JINJA_CMETA_ERR_METADATA`, an oversized scan returns `JINJA_CMETA_ERR_CAPACITY`,
and unsupported operand types return `JINJA_CMETA_ERR_RENDER`.
Postfix `[]` lookup may be chained. String keys select CMeta struct fields; boolean or `int64`
keys select borrowed sequence elements and UTF-8 string scalars, with negative indices counted
from the end. String indexing uses Unicode scalar boundaries, not bytes or grapheme clusters, and
returns the original UTF-8 bytes without normalization. Missing fields, out-of-range indices, and
inapplicable keys on a defined base produce default Undefined; indexing an already Undefined base
returns `JINJA_CMETA_ERR_RENDER`. Invalid CMeta or runtime UTF-8 metadata returns
`JINJA_CMETA_ERR_METADATA`. A dot may follow an item lookup or grouped expression when its base is
a CMeta struct, so `users[0].name` and `(user).name` compose with the same field validation and
Undefined behavior. Slices, generic CMeta map/sequence traversal, Python-style item/attribute
fallback ordering, and descriptors/methods remain unsupported.
List, tuple, and dict literals support nesting, trailing commas, truthiness, postfix lookup,
membership, comparisons, the current type tests, and `for` iteration. Lists and tuples use
structural equality and same-type lexicographic ordering; they are distinct types. Dict equality
is insertion-order independent, membership and iteration operate on keys, and repr preserves the
first insertion position while a duplicate key's final value wins. Dict keys are limited to the
current scalar values and recursively hashable tuples; list or dict keys return
`JINJA_CMETA_ERR_RENDER`. The 64-node expression bound permits at most 63 list/tuple elements or
31 dict pairs in one literal. Comprehensions and unpacking remain unsupported. Literal collection
construction evaluates elements (dict keys and values) once in source order, including elements
not selected by a later lookup or comparison. Subsequent reads use immutable render-owned snapshots.
Their backing workspace is allocated on first use and bounded by `max_nodes * expression_count`
values, with checked byte arithmetic and `JINJA_CMETA_ERR_CAPACITY` on exhaustion. Snapshot payloads
borrow only immutable template/CMeta data or stable nodes from the same synchronous render.
Container repr detects list/tuple/dict/namespace identities on the active ancestor path,
emitting `[...]`, `(...)`, `{...}`, or `<Namespace {...}>` for a back edge. Sibling aliases
are printed in full, and shallow copies retain their distinct identity. The existing local
repr depth check remains; at most 64 container frames may be active across lazy loop-length
and filter-macro reentry. An ancestor marker needs no additional frame. Exhaustion returns
`JINJA_CMETA_ERR_CAPACITY`; these internal limits do not reinterpret `max_value_depth`.
`string`, concatenation, and `join` isolate each conversion's unpublished bytes, including
filtered LoopContext repr that reenters a macro. Completed inner strings remain valid until
render cleanup; their bytes never become part of the outer conversion. Pending construction
and retained slice payload share `max_string_bytes`; captures and final string output retain
their existing independent budgets. Failed construction publishes no outer string.
Each individual tstr-backed construction, capture, or final output is additionally bounded by
`min(UINT32_MAX/2, SIZE_MAX/4)` bytes, with `CAPACITY` before unsafe allocation or growth.
A larger configured budget is not itself invalid. Successful nested execution through a macro,
recursive loop, or filter restores the caller's diagnostic cursor; a failed child preserves
its innermost failing instruction's original source byte offset. Converting a final string-sink
failure to `CAPACITY` or `OUT_OF_MEMORY` also preserves that offset. Locations identify an
instruction, not each subexpression. Unicode non-printable repr remains tracked in issue #26.
Ordinary loops expose read-only `loop.index0`, `index`, `revindex0`, `revindex`, `first`, `last`,
`length`, `depth0`, and `depth`. Nested loops resolve the nearest loop; dict length counts unique
keys. Direct metadata lookup consumes bounded provider nodes, while metadata used inside a native
expression remains stack-local until the final result node. `loop.previtem` and `loop.nextitem`
borrow neighboring values from the same render-scoped iterable; a missing boundary neighbor is
Undefined, while continuing attribute lookup through it is a render error. Dict neighbors follow
the unique first-insertion key order. Exact loop aliases remain bound to the nearest iteration item
inside nested conditional sections. Positional postfix call syntax is parsed for correct error
classification. `loop.cycle(...)` eagerly evaluates its arguments and selects by `index0`; an empty
cycle returns `JINJA_CMETA_ERR_RENDER`. `loop.changed(...)` compares its argument tuple with the
previous call in that loop, shares history between call sites, and isolates nested loops. Its
render-owned snapshots are bounded by `max_nodes`. Stateful `if`/`elif` conditions are evaluated
once per branch context; their lowered inverse sections reuse the decision even when the body
changes loop history. The bounded decision cache belongs to the render, not the compiled template.
`range(stop)`, `range(start, stop)`, and `range(start, stop, step)` accept integer/bool positional
arguments and return a distinct lazy range value. It supports iteration, loop metadata, positive/
negative indexing, `start`/`stop`/`step`, truthiness, sequence/iterable tests, numeric membership,
sequence-based equality and Python-style range repr. Range storage and indexing do not grow with
length; construction uses checked full-width integer arithmetic. Zero step, invalid arguments,
ordering comparisons and non-callable context bindings shadowing `range` are render errors.
Iteration remains bounded by the render node budget and provider index width.
`range.count(value)` and `range.index(value)` accept one positional value and share the numeric
membership predicate. Count returns 0/1; index returns the zero-based position or a render error
when absent. An index beyond the supported int64 result domain returns a capacity error.
These methods also work through loop aliases and computed range receivers without materializing
elements. Calls accept keyword syntax, including a trailing comma, but these positional-only
builtins reject keywords at rendering after evaluating arguments. Logical short circuit skips
unexecuted calls. Repeated keywords and positional arguments after keywords are syntax errors.
First-class bound methods, recursive loops and other call targets remain unsupported.
Numeric arithmetic supports unary `+` / `-` and binary `+`, `-`, `*`, `/`, `//`, `%`, and `**` over
literal or CMeta bool/signed/unsigned integer operands representable as `int64`, plus CMeta float
operands. Decimal/exponent float literals and decimal/base-prefixed integer literals accept Jinja
underscore separators; integer prefixes are case-insensitive `0b`, `0o`, and `0x`. Leading-dot and
trailing-dot float forms are syntax errors. Multiplication,
floor division, and modulo bind before addition/subtraction; power is left associative, matching
Jinja rather than Python. `/` always yields float; `//`, `%`, and `**` yield float when numeric
coercion requires it. Floor division and modulo use Jinja/Python signs. Integral-only operations
retain checked `int64` results. Checked overflow and unsigned inputs above `INT64_MAX` return
`JINJA_CMETA_ERR_CAPACITY`; zero divisors and non-numeric operands return
`JINJA_CMETA_ERR_RENDER`. Negative integral powers yield float. A negative base with a fractional
exponent is rejected because the current CMeta profile has no complex value kind. Sequence/string
`+`, arbitrary-precision integers, complex results, and `~` remain unsupported. Intermediate arithmetic
values stay on the render stack and only the final expression reserves a provider node.
`and` and `or` use Jinja's precedence (`comparison`, then `not`, then `and`, then `or`), evaluate
left to right, short-circuit the unused branch, and return the selected operand. Current literals,
dotted paths, comparisons, grouping, and undefined values may be operands. A logical expression
used by `if`/`elif` is converted to one boolean section value, while interpolation preserves a
selected scalar value; undefined renders as an empty string. Each logical expression has a fixed
64-node parser limit; comparison chains share this node limit and a separate 64-step fixed bound.
Conditional expressions use Jinja's lower-than-`or` precedence and return the selected operand.
`value if condition else fallback` evaluates the condition once and only the selected branch;
`value if condition` returns undefined when false. Repeated `if` clauses follow Jinja's
left-to-right construction, while an `else` branch recursively contains another conditional
expression. A conditional expression used directly as an `if`/`elif` statement condition must be
parenthesized, matching Jinja syntax. Conditional nodes share the same 64-node expression bound,
own their compiled paths and strings, and reserve only the final provider node during rendering.
The `none` and `None` literals render as `None`, are false in conditions, compare equal only to
each other, and remain distinct from undefined. Zero-argument `is` / `is not` supports
`defined`, `undefined`, `none`, `boolean`, `true`, `false`, `integer`, `float`, `number`,
`string`, `mapping`, `sequence`, and `iterable`. Tests bind after unary arithmetic and before
binary arithmetic/comparison, evaluate their operand once, and reserve only the final provider
node. `number` includes booleans as in Jinja, while `integer` excludes them. CMeta structs/maps
are mappings; strings, bytes, structs/maps, sequences, and the Jinja borrowed sequence view are
sequences; sets join those values for `iterable`. Default undefined is both sequence and iterable,
matching the pinned Jinja environment. These type classifications do not imply that generic CMeta
map/sequence/set traversal is implemented. Test arguments, user registries, dotted test names, and
ungrouped test chaining remain outside this profile; valid unavailable forms return
`JINJA_CMETA_ERR_UNSUPPORTED`, while malformed `is` syntax returns `JINJA_CMETA_ERR_SYNTAX`.
Single- and double-quoted UTF-8 string literals support `\\`, `\'`, `\"`, `\a`, `\b`, `\f`,
`\n`, `\r`, `\t`, and `\v`, plus fixed-width `\xHH`, `\uHHHH`, and `\UHHHHHHHH` escapes.
Fixed-width escapes require ASCII hexadecimal digits and must decode to a Unicode scalar value;
truncated or non-hex escapes, UTF-16 surrogates, and values above `U+10FFFF` return
`JINJA_CMETA_ERR_SYNTAX`. Octal escapes consume one to three digits and encode a Unicode scalar.
Unknown ASCII escapes preserve the backslash. A backslash before a non-ASCII scalar produces
literal `\x`, `\u`, or `\U` hexadecimal text, matching Jinja's ASCII backslash-replacement step.
Literal CR/CRLF/LF normalize to the configured newline sequence before unescaping.
Backslash followed by a physical newline emits no bytes only with the default LF
sequence; with CRLF/CR it remains literal. Named Unicode escapes use the shared
Unicode name database; an unknown name or unclosed quote is a syntax error.
Strings may contain `|`; an empty string is false and a non-empty string
is true. Adjacent quoted literals (including mixed quotes and intervening ASCII whitespace)
form one compile-time string before postfix operations. Escape digits never cross quote boundaries;
the combined decoded bytes are copied into the compiled template and charged against render limits.

Decoded lengths and render limits count UTF-8 bytes. Escaped NUL is preserved by the streaming
`jinja_cmeta_render()` API because renderer callbacks receive explicit lengths. Use that API for
binary-safe output; `jinja_cmeta_render_string()` exposes a NUL-terminated C string without a
separate length and therefore cannot make bytes following an embedded NUL conveniently observable.
Quoted strings may contain `}}` or `%}` without closing the surrounding tag.
Bracket nesting is tracked so dictionary closers can directly precede `}}`.
Comments do not interpret quotes and end at the first `#}`; raw regions use their
separate lexical rules. Unterminated quotes/tags and mismatched brackets are syntax errors.

Interpolation is not HTML-escaped by default. `safe`, `escape`/`e`, and `forceescape`
are expression filters: `safe` marks text, `escape` preserves already-safe text,
and `forceescape` always escapes. `is escaped` inspects the safety marker.
`{% autoescape expression %}...{% endautoescape %}` temporarily enables or disables
escaping and restores the enclosing state. Safety survives indexing, slicing,
repetition, `trim`, `center`, and `string`; character iteration and `first` discard
it, while `last` preserves it. String `+` escapes unsafe parts when either part is
safe; `~` does so only with autoescape enabled. Container repr preserves `Markup(...)`.
Escaped strings share the bounded render workspace with other generated strings.
These rules follow [Jinja's escaping semantics](https://jinja.palletsprojects.com/en/stable/templates/#html-escaping).
`safe` does not sanitize HTML and must only be applied to trusted content.
`JINJA_CMETA_RENDERER.write` receives final bytes and must not escape them again.
String rendering also bounds the final retained result by `max_string_bytes`.
Interpolation, statement, and
comment delimiters accept Jinja's `-` whitespace-control marker on either side.
Unsupported expressions or filters fail during `jinja_cmeta_compile()` with
`JINJA_CMETA_ERR_UNSUPPORTED`.

Macros and call blocks use the public compile/render path, including defaults,
argument expansion, varargs/kwargs, caller, recursive calls and lexical closures.
Macro properties and LoopContext aliases are supported. Compilation bounds each
template to 64 functions and 64 cumulative parameters/expressions; runtime calls
share the depth budget, while active write bindings and retained activations obey
`max_nodes`. See [the feature matrix](JINJA_COMPATIBILITY.md) for remaining gaps.

## Numeric Tests

`is odd` and `is even` accept supported integers, booleans, and floating-point
values. Integer parity is evaluated without conversion to floating point.
Nonintegral floats and NaN/infinity match neither test. Nonnumeric operands,
including Undefined, produce a render error.
Supported zero-argument tests also accept empty parentheses, such as `is odd()`
and `is defined()`. Extra positional arguments produce a render error;
an empty tuple argument such as `is odd(())` is not mistaken for an empty call.

`is divisibleby(3)`, `is divisibleby(num=3)`, and `is divisibleby 3` accept one numeric argument,
including booleans and floats. Integer divisibility preserves borrowed unsigned
precision and handles the minimum signed integer without division overflow.
Zero divisors, nonnumeric values, and wrong arity produce a render error.
Operands and positional arguments are evaluated in source order before checking
arity; surrounding logical short circuit still applies. Unknown keywords and
positional/keyword collisions produce a render error after argument evaluation.
Repeated keywords and positional arguments following keywords produce syntax
errors. Upstream may fold repeated keywords in constant-only tests; this
optimization-dependent behavior is not implemented. Argument unpacking is not
yet supported. Shorthand arguments allow attribute, item, slice, and supported
positional-call chains, such as `2 is in range(3)` or `6 is divisibleby [3][0]`.
Leading parentheses delimit the test's argument list; arithmetic and logical
operators following a shorthand chain apply outside that argument.
Test results may continue through supported filter chains, for example
`3 is odd|string|length`. A filter after a shorthand argument applies to the
test result; parenthesize the argument to filter it instead. Tests and filters
can alternate, such as `3 is odd|string is string`. Consecutive tests require
an argument form (including `()`) on the preceding test or explicit grouping:
`1 is integer() is true` works, while `1 is integer is true` is a syntax error.

Comparison tests `eq` / `equalto`, `ne`, `lt` / `lessthan`, `le`,
`gt` / `greaterthan`, and `ge` accept one positional argument. They use the
same value comparison rules as expression operators, including numeric
precision, NaN, Unicode strings, and supported native containers. Keywords,
wrong arity, and ordering of incompatible types produce render errors.
This does not add comparison support for arbitrary CMeta custom objects.

`is in(sequence)` / `is in(seq=sequence)` and their `is not` forms use the
ordinary membership lookup path. Supported containers include strings, native
lists/tuples/dicts/ranges, borrowed sequence views, and items iterators. Iterator
search consumes through the first match, or through exhaustion. Default Undefined
is an empty iterable for both membership operators and tests; None remains an
error. Borrowed sequence scans retain the existing node-budget limit.

## Raw Blocks

`{% raw %}...{% endraw %}` emits its contents as literal text, including incomplete
expressions and comments. Raw blocks do not nest: the first valid `endraw` closes
the block. Whitespace-control markers follow Jinja's raw-tag rules, including
Unicode whitespace. Raw text is not autoescaped. A missing closing directive is a
syntax error at the opening tag. The re2c raw scanner handles overlapping braces
and reads explicit-length buffers without requiring a trailing sentinel byte.

## Unicode Input

The complete template must be strict UTF-8. Compilation reuses Salts
`vstr_utf8_invalid_offset()` and reports the first invalid byte offset as
`JINJA_CMETA_ERR_SYNTAX`; the template lexer then scans delimiters over that
already-validated byte sequence. Raw text and unescaped string-literal content
preserve their UTF-8 bytes, including non-BMP scalars and combining sequences.
No Unicode normalization, grapheme segmentation, case folding, or locale
collation is performed.

All template capacities, source offsets, and output limits count bytes rather
than Unicode code points. Currently, identifiers are restricted to
`[A-Za-z_][A-Za-z0-9_]*`, while syntax and trim whitespace is restricted to
ASCII space, tab, line feed, vertical tab, form feed, and carriage return.
Those decisions are independent of the process C locale. Unicode identifiers
and Unicode whitespace supported by upstream Jinja remain unimplemented
and fail compilation with `JINJA_CMETA_ERR_UNSUPPORTED`.

`safe` bypasses HTML escaping and must only be used for content already trusted
or sanitized for the destination context. It does not validate or sanitize the
value.

Compilation is bounded by `JINJA_CMETA_MAX_BLOCK_DEPTH`,
`JINJA_CMETA_MAX_CONDITION_BRANCHES`, and
`JINJA_CMETA_MAX_TEMPLATE_BYTES`, and `JINJA_CMETA_MAX_INSTRUCTIONS` (65536).
The compiled byte pool is limited to 32 MiB. Exceeding a bound returns
`JINJA_CMETA_ERR_CAPACITY`; no partial template is returned.
The total number of compiled expression nodes shares the
`JINJA_CMETA_MAX_CONDITION_BRANCHES` limit; each parsed expression is additionally limited to
64 AST nodes.

Native conditions evaluate once without introducing a sequence/object section
context. A normal completed loop skips its `else`; only an initially empty loop
enters it. Literal text is never reparsed, including delimiters formed across
trimmed comments. CMeta booleans use Jinja's `True` / `False` spelling.
Node budgets count the current runtime's retained nodes, not the former
Mustache adapter's intermediate contexts. `max_render_depth` limits executed
lexical control depth; zero selects `JINJA_CMETA_MAX_BLOCK_DEPTH`.

`max_value_visits` independently limits cumulative runtime value comparisons and
dictionary-key validation visits across expressions, loops, and macro calls.
Zero selects `JINJA_CMETA_DEFAULT_MAX_VALUE_VISITS` (1,048,576). Membership operators
and `is in(...)` count the root probe as well as any element comparisons; key
validation visits tuple children recursively. Each entered comparison/key-check
frame consumes one visit, including identity-matched leaves. Short-circuited work,
compile-time folding, `sameas`, and the arithmetic `range.count/index` methods do
not consume these visits.

`max_value_depth` limits simultaneously active comparison/key-check frames,
counting roots and leaves. Zero selects `JINJA_CMETA_MAX_VALUE_DEPTH` (64);
values above 64 return `JINJA_CMETA_ERR_INVALID_ARGUMENT` before output.
Exhausting either traversal limit returns `JINJA_CMETA_ERR_CAPACITY`. Counters
belong to one render and reset only for the next render; tuple-key validity no
longer depends on the number of compiled expressions. These are not total
instruction, output-byte, or sandbox limits. String comparison byte costs still
follow the existing source-specific bounds: borrowed strings use
`max_string_bytes`, while source literals use the compiled-template byte limits.

The value traversal fields extend the public options structure. Recompile callers
against the matching headers and library; there is no old-layout ABI adapter.

## Installed Consumer Verification

After `cmake --build --preset install-win-release-user`, the standalone fixture
under `test/install_consumer` uses only installed headers and `Salts::JinjaCMeta`.
Set `PKG_ROOT` and `VCPKG_INSTALLED_DIR` to the matching root profile values,
enter the Visual Studio developer environment, then run from that directory:

```text
cmake --preset installed-win-release-user
cmake --build --preset installed-win-release-user
ctest --preset installed-win-release-user
```

The fixture exercises actual default/explicit escaping and native iteration,
not just header inclusion or compilation.

## CMeta Data Contract

The root value is described by `cmeta_data_desc`. Struct fields use
`cmeta_data_struct_shape`; bool, integer, float, string, and enum fields use
CMeta's semantic descriptors. Contiguous collections use a borrowed
`JINJA_CMETA_SEQUENCE_VIEW`, with their field descriptor pointing to
`jinja_cmeta_sequence_data()`.

Borrowed objects, descriptors, string bytes, and sequence elements must remain
immutable and address-stable until rendering returns. Every render has explicit
bounds; zero-valued options select the defaults:

```c
JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
options.max_nodes = 256;
options.max_string_bytes = 64 * 1024;
options.max_render_depth = 32;
options.max_value_visits = 64 * 1024;
options.max_value_depth = 32;
```

`jinja_cmeta_render()` streams output and retains bytes emitted before a
failure. `jinja_cmeta_render_string()` returns a `malloc`-owned string only on
success; release it with `free()`.

## Tests

`items` currently creates a one-shot iterator for native dictionary expressions
and Undefined. Use `dict_expression|items|list` to materialize tuples, or iterate
with `for pair in dict_expression|items` and access `pair[0]` / `pair[1]`.
`first` consumes one tuple; copying the iterator value shares the cursor.
`==` and `!=` compare iterator identity without consuming it. `in` / `not in`
consume through the first equal tuple, or exhaust the iterator on a miss.
Iterator handles can also be native dictionary keys; ordering is a render error.
The iterator itself is truthy, has no length, and is not a sequence. Invalid
non-mapping input fails when consumed. Generic CMeta mappings and generator object
representation remain unsupported; direct iterator output returns a render error.
The compatibility matrix records the remaining iterator and scope gaps.

```powershell
cmake --build --preset win-release-user --target `
  test_jinja_cmeta test_jinja_cmeta_header_cpp

ctest --preset win-release-user `
  -R "^test_jinja_cmeta(_header_cpp)?$" `
  --output-on-failure
```

The tests under `jinja/test/` cover syntax lowering, CMeta descriptors,
ownership and capacity failures, escaping, C header behavior, and C++ header
compatibility.

The Python oracle is intentionally separate from the normal CTest dependency
graph. It explicitly emits UTF-8 regardless of the Windows console code page.
Create an isolated environment and run the pinned corpus explicitly:

```powershell
python -m venv build/jinja-oracle-venv
build/jinja-oracle-venv/Scripts/python -m pip install `
  -r jinja/test/oracle/requirements.txt
build/jinja-oracle-venv/Scripts/python `
  jinja/test/oracle/jinja_oracle.py `
  jinja/test/oracle/cases.json --verify
# 或使用拆分后目录:
# build/jinja-oracle-venv/Scripts/python `
#   jinja/test/oracle/jinja_oracle.py `
#   jinja/test/oracle/cases --verify
```

The Jinja2Cpp reference-suite feature inventory and its mapping into independent
TinyTest/oracle cases are recorded in
[`test/JINJA2CPP_TEST_MAPPING.md`](test/JINJA2CPP_TEST_MAPPING.md). No upstream
GoogleTest source is compiled into this module.

With `BUILD_BENCHMARKS=ON`, target `benchmark_jinja_cmeta` records separate
compile and render baselines. Its byte counts describe the template input for
compile and emitted output for render; they are not compatibility assertions.
