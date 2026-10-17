# IDL ComponentProvider → canonical Salts ServiceHost

This is the **tagged-SDK migration candidate**, stacked on SaltsUtils #667's
staged IDL/FR2 generation and compiler-link closure. It is not installed or
published until the matching upstream Salts release tag has completed its
six-RID ServiceHost ABI checks.

## Dependency and ownership

A consumer **must** provide `SALTS_CANONICAL_TAG` naming a published
`v2.3.0-rc.N` (N ≥ 18) or the final `v2.3.0` release. Restore uses the
**exact** NuGet `Salts.Native` version, verifies the installed RID
`salts-sdk-manifest.txt`, and requires imported targets:

- `Salts::Configurator` — single component configuration graph
- `Salts::ServiceHost` — single generation/Scope/Plugin-lease Owner
- `Salts::ComponentProviderABI` — exact native ProviderBinding protocol
- `Salts::Unicode` — one Unicode data/API owner shared by Salts and Jinja

No `Salts::Component`, `Salts::ComponentPlugin`,
`Salts::ComponentPluginABI`, `ServiceHostCandidate`, shadow ABI, source
checkout, or private copied runtime is accepted. SaltsUtils no longer builds
or installs its own Unicode runtime. Generated Provider DSOs include
`<salts/component_provider_abi.h>` and link only the Provider ABI/Plugin/CMeta;
the **host**, not a Provider DSO, links `Salts::ServiceHost`.

## IDL constraints and execution

The existing IdlContract → ServiceNativeIR / NativeSourceIR remains the only
type and ABI authority. An explicit opt-in staged `PLUGIN+COMPONENT` artifact
can emit 1–4 non-throwing FR2 operations for one Service and one Component.
`COMPONENT` and `PLUGIN` output files are *different alternative DSOs*, not
two `cmeta_plugin_query` functions linked into one binary.

Tests enabled with `DATABIND_ENABLE_SERVICEHOST_DSO_TESTS=ON` load a real
generated Plugin and call the original tag-installed `Salts::ServiceHost`
for an N→N+1 publication, borrowed Scope lifecycle, BUSY drain and module
lease 2→1→0. Missing Provider exports reject without publication or leaked
lease; the same FAILED candidate bundle can be retried. Typed-error Service
shapes must parse canonically but remain **unsupported** by this first FR2
projection and produce zero partial files.

## Release gate

The separate tagged-SDK conformance workflow can be run **only when a real
published tag is supplied**. It restores that exact Salts.Native package
with no wildcard and first executes the independent Linux installed-SDK
C11/C++17 real DSO gate. Windows MSVC, macOS arm64 and Android/iOS published
SDK gates remain open and must be added/qualified against the same actual
release tag before production cutover. An unset or unpublished tag cannot be interpreted as success
and is **not** replaced by a checkout of the untagged Salts master.

Current tag publication is upstream-controlled; this Draft does **not**
create or impersonate a release tag. The opt-in generator and this test-only
contract cannot be used to switch CHttp or produce a public SaltsUtils SDK
before true installed release qualification.

**DRAFT / DO NOT MERGE / DO NOT PUBLISH.**
