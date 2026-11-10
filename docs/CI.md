# Split SDK CI

The [root workflow](../.github/workflows/ci.yml) runs on pull requests, master
updates and manual dispatch. CMake user presets define the complete build
graph; CI does not reduce builds with target lists or execute test binaries
directly. Each native job runs its formal CTest suite, installs the SDK and
uploads the original TAR plus configure/CTest diagnostics.

## Dependency inputs

- `VCPKG_CACHE_VERSION` and its matching infrastructure checkout select exact
  release descriptors resolved by the shared `resolve-release`/`locked` CLI.
- `SALTS_BASE_VERSION` selects a published stable Base manifest and SDKs. The
  release API digest, manifest size/hash and producer qualification must agree.
- `CMETA_REF` selects an immutable CMeta source commit; `CMETA_VERSION` checks
  its installed package version. CMeta is built with its normal product preset
  and `BUILD_TESTS=OFF`. Its product depends only on the published Base SDK.
- Re2c/Lemon package versions and hashes remain in `cmake/*.lock.json`. CI
  explicitly supplies their original NuGet archives from the selected cache
  release; the shared restore helper verifies package identity and the paired
  Lemon executable/template. No authenticated-feed failure triggers a retry
  against another source.

The preparation job invokes CMeta's exact-ref SDK input entry point, passing
this repository's release descriptors. It downloads once and distributes the
same verified blobs, generated locks and infrastructure snapshot. Product
CMake configuration never resolves release versions or builds dependency ports.

## Matrix and host tools

The matrix matches Base's ten published profiles: Windows x64 MSVC and
LLVM-MinGW; Linux x64 and ARM64 Clang/GCC; Linux x64 Clang TSan; macOS ARM64
Clang; Android ARM64 and x64. Native jobs execute full tests. Android jobs
cross-build and install; they do not claim device execution. Base 0.1.4 does
not publish Windows ARM64 or macOS x64 SDKs.

Android first restores a separate Linux x64 host closure, installs the native
CMeta SDK, and builds/installs native `salts-idlc`. Target generation receives
that executable and its explicit host Base/CMeta roots through existing
`SaltsUtils_IDLC_*` arguments. Target roots remain separate and are never
replaced by host libraries.

## Artifacts and release boundary

`utils-sdk-<profile>-<commit>` contains the original installed SDK TAR. Its
manifest records source, profile, Base source/producer/hash, selected CMeta ref
and native-test scope. Diagnostics retain configure caches and formal JUnit
reports. A failed build or test prevents SDK publication for that lane; jobs
are not retried unchanged and missing inputs stop the workflow.

The historical manual NuGet publisher accepts historical immutable commit/tag
and preparation-run inputs. It is not invoked by this split CI. This migration
publishes no new CMeta/SaltsUtils release and never replaces historical tags or
assets. A future release requires a new package version, qualified exact-source
artifacts and a publisher updated for the split SDK layout.
