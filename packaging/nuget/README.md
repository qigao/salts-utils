# SaltsUtils.Native

Prebuilt Release SDKs for qigao/salts-utils.

The package contains the prebuilt SaltsUtils SDK only; dependency SDKs are restored explicitly by the consumer.
The re2c host generator is a build-time tool and is not a runtime dependency of this package.

## Layout

- `sdk/linux-x64/`
- `sdk/linux-arm64/`
- `sdk/windows-x64/`
- `sdk/macos-arm64/`
- `sdk/android-arm64-v8a/`
- `sdk/ios-arm64/`
- `sdk/ios-simulator-arm64/`

Each directory is a normal CMake install prefix containing
`lib/cmake/SaltsUtils/SaltsUtilsConfig.cmake`.

The Linux arm64 profile is the focused native-SDK/DataBind profile used for
arm64 qualification. It intentionally excludes the optional capture feature;
missing capture headers/libraries fail fast rather than falling back to a
source-built `libyuv` dependency.

Consumers restore `SaltsUtils.Native` and `Salts.Native` explicitly,
then set `SALTS_ROOT` to the matching Salts SDK and point CMake at the matching
SaltsUtils SDK:

    export SALTS_ROOT=<nuget>/salts.native/<resolved>/sdk/linux-x64
    export SALTS_UTILS_ROOT=<nuget>/saltsutils.native/<resolved>/sdk/linux-x64

Use the matching RID on Linux arm64:

    export SALTS_ROOT=<nuget>/salts.native/<resolved>/sdk/linux-arm64
    export SALTS_UTILS_ROOT=<nuget>/saltsutils.native/<resolved>/sdk/linux-arm64

    find_package(SaltsUtils CONFIG REQUIRED PATHS "$ENV{SALTS_UTILS_ROOT}" NO_DEFAULT_PATH)

## Upgrading to 4.2

This release updates DataBind and Jinja to the Salts 2.1 CMeta reflection and
lifecycle contracts. Regenerate IDL bindings and rebuild consumers with the
matching Salts SDK; the previous generated interfaces are not preserved.
The SDK's Lua dependency is resolved through the vcpkg `unofficial-lua` CMake
config target. Use the project's manifest and matching target triplet.

The macOS SDK uses GCC 15 for C/C++; Apple platform sources use Apple Clang.
Cross-compilation validates Android and iOS builds, not execution on devices.
