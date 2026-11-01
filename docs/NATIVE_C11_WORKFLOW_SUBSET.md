# C11 native workflow subset

This source graph is a **selection of canonical implementations**, not a
fallback, fork, second provider or compatibility implementation.

Configure `-DSALTS_UTILS_NATIVE_C11_WORKFLOW_SUBSET=ON` with exact installed
SaltsCore and CMeta SDKs. The selected graph exposes these capabilities:

- `Salts::QueryVM`
- `Salts::JsonParser`, `Salts::JsonCSerdeAdapter`
- `Salts::CYaml`, `Salts::CYamlJsonAdapter`
- `Salts::BehaviorTree`
- `Salts::Process`

The application CFlow executor and CMeta type/Configurator/Interceptor are
owned exclusively by the imported CMeta package. OS Process/NativeIO owners
remain in SaltsCore; SaltsUtils only adds the selected higher-level adapters.

The JSON object-key hash is a parser-private exact implementation of its own
key-index fingerprint, replacing the previous incidental `hash_bytes`
link against CSTL (whose canonical target is now `CMeta::CSTL`). No generic
hash map is built or exported.
The full SaltsUtils graph is unchanged when the native subset is disabled.

No legacy C++ WorkflowExecutor, script bindings, DataBind, Capture, Serial,
or unrelated parser is built in this source subset. Missing real installed
provider targets remain fatal; do not introduce alias libraries or source
copies to bypass clean-prefix failures.

Native consumers (such as merged Praktor C ABI v3) still require exact
Linux/Windows/macOS build, C SDK consumer and complete-source sanitizers
before production acceptance.
