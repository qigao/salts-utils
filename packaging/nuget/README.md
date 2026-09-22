# SaltsUtils.Native

Versioned prebuilt Release SDKs for qigao/salts-utils.

Package version `2.0.0` is built against and depends on `Salts.Native 1.1.0`.
The re2c host generator is a build-time tool and is not a runtime dependency of this package.

## Layout

- `sdk/linux-x64/`
- `sdk/windows-x64/`
- `sdk/macos-x64/` or `sdk/macos-arm64/`
- `sdk/android-arm64-v8a/`

Each directory is a normal CMake install prefix containing
`lib/cmake/SaltsUtils/SaltsUtilsConfig.cmake`.

Consumers restore `SaltsUtils.Native 2.0.0` and its exact `Salts.Native 1.1.0` dependency,
then set `SALTS_ROOT` to the matching Salts SDK and point CMake at the matching
SaltsUtils SDK:

    export SALTS_ROOT=<nuget>/salts.native/1.1.0/sdk/linux-x64
    export SALTS_UTILS_ROOT=<nuget>/saltsutils.native/2.0.0/sdk/linux-x64

    find_package(SaltsUtils CONFIG REQUIRED PATHS "$ENV{SALTS_UTILS_ROOT}" NO_DEFAULT_PATH)
