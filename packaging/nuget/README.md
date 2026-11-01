# SaltsUtils.Native

Prebuilt Release SDKs for qigao/salts-utils.

The package contains the prebuilt SaltsUtils SDK only; dependency SDKs are restored explicitly by the consumer.
The re2c host generator is a build-time tool and is not a runtime dependency of this package.

## Layout

- `sdk/linux-x64/`
- `sdk/linux-arm64/`
- `sdk/windows-x64/`
- `sdk/macos-arm64/`
- `sdk/android-arm64/`

Each directory is a normal CMake install prefix containing
`lib/cmake/SaltsUtils/SaltsUtilsConfig.cmake`.

The Linux arm64 profile is the focused native-SDK/DataBind profile used for
arm64 qualification. Playback, Capture, Serial, and USB remain in the
separately restored SaltsCore SDK rather than being copied into this package;
missing SaltsUtils modules fail fast rather than falling back to a
source-built `libyuv` dependency.

Consumers restore `SaltsUtils.Native`, `SaltsCore.Native`, and `CMeta.Native`
explicitly, then set all three exact SDK roots:

    export SALTSCORE_ROOT=<nuget>/saltscore.native/0.1.0/sdk/linux-x64
    export CMETA_ROOT=<nuget>/cmeta.native/0.1.1/sdk/linux-x64
    export SALTS_UTILS_ROOT=<nuget>/saltsutils.native/<resolved>/sdk/linux-x64

Use the matching RID on Linux arm64:

    export SALTSCORE_ROOT=<nuget>/saltscore.native/0.1.0/sdk/linux-arm64
    export CMETA_ROOT=<nuget>/cmeta.native/0.1.1/sdk/linux-arm64
    export SALTS_UTILS_ROOT=<nuget>/saltsutils.native/<resolved>/sdk/linux-arm64

    find_package(SaltsUtils CONFIG REQUIRED PATHS "$ENV{SALTS_UTILS_ROOT}" NO_DEFAULT_PATH)

## Split SDK contract

This release consumes CMeta reflection/lifecycle targets from `CMeta.Native`
and foundation targets from `SaltsCore.Native`. Regenerate IDL bindings and
rebuild consumers with matching 0.1 SDK profiles.
The SDK's Lua dependency is resolved through the vcpkg `unofficial-lua` CMake
config target. Use the project's manifest and matching target triplet.

Native Windows, Linux, macOS, and Android builds use the configured LLVM
toolchains. Cross-compilation validates the Android build, not execution on a
device.
