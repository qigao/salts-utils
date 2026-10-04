# SaltsUtils.Native

Prebuilt Release SDKs for qigao/salts-utils.

The package contains the prebuilt SaltsUtils SDK only; dependency SDKs are restored explicitly by the consumer.
The re2c host generator is a build-time tool and is not a runtime dependency of this package.

## Layout

- `sdk/linux-x64/`
- `sdk/linux-arm64/`
- `sdk/windows-x64/`
- `sdk/macos-x64/` or `sdk/macos-arm64/`
- `sdk/android-arm64-v8a/`

Each directory is a normal CMake install prefix containing
`lib/cmake/SaltsUtils/SaltsUtilsConfig.cmake`.

Consumers restore `SaltsUtils.Native` and `Salts.Native` explicitly,
then set `SALTS_ROOT` to the matching Salts SDK and point CMake at the matching
SaltsUtils SDK:

    export SALTS_ROOT=<nuget>/salts.native/<resolved>/sdk/linux-x64
    export SALTS_UTILS_ROOT=<nuget>/saltsutils.native/<resolved>/sdk/linux-x64

Use the matching RID on Linux arm64:

    export SALTS_ROOT=<nuget>/salts.native/<resolved>/sdk/linux-arm64
    export SALTS_UTILS_ROOT=<nuget>/saltsutils.native/<resolved>/sdk/linux-arm64

    find_package(SaltsUtils CONFIG REQUIRED PATHS "$ENV{SALTS_UTILS_ROOT}" NO_DEFAULT_PATH)
