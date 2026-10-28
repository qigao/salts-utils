# C11 native workflow subset

This source graph is a **selection of canonical implementations**, not a
fallback, fork, second provider or compatibility implementation.

Configure `-DSALTS_UTILS_NATIVE_C11_WORKFLOW_SUBSET=ON` with one installed
Salts 3 / SaltsCore and the canonical Crypto package. Only these targets are
built and installed:

- `Salts::QueryVM`
- `Salts::JsonParser`, `Salts::JsonCSerdeAdapter`
- `Salts::CYaml`, `Salts::CYamlJsonAdapter`
- `Salts::BehaviorTree`
- `Salts::Process`

The application CFlow executor and CMeta type/Configurator/Interceptor are
owned exclusively by the imported Salts package. The OS Process/NativeIO
owners remain SaltsCore/SaltsUtils as declared by those packages.

The JSON object-key hash is a parser-private exact implementation of its own
key-index fingerprint, replacing the previous incidental `hash_bytes`
link against removed `Salts::CSTL`. No generic hash map is built or exported.
The full SaltsUtils graph is unchanged when the native subset is disabled.

No legacy C++ WorkflowExecutor, script bindings, DataBind, Capture, Serial,
or unrelated parser is built in this source subset. Missing real installed
provider targets remain fatal; do not introduce alias libraries or source
copies to bypass clean-prefix failures.

Native consumers (such as merged Praktor C ABI v3) still require exact
Linux/Windows/macOS build, C SDK consumer and complete-source sanitizers
before production acceptance.
