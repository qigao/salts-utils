# SaltsUtils.Native

Prebuilt Release SDKs for qigao/salts-utils.

The package depends on the latest published stable `Salts.Native`. Dependency
selection is performed by package restore; CMake validates the resolved SDK by
its exported targets and required capabilities instead of a release number.
The re2c host generator is a build-time tool and is restored the same way.

## Layout

- `sdk/linux-x64/`
- `sdk/windows-x64/`
- `sdk/macos-x64/` or `sdk/macos-arm64/`
- `sdk/android-arm64-v8a/`

Each directory is a normal CMake install prefix containing
`lib/cmake/SaltsUtils/SaltsUtilsConfig.cmake`.

Consumers restore the latest published `SaltsUtils.Native` and
`Salts.Native`, then set `SALTS_ROOT` to the resolved Salts SDK and point
CMake at the resolved SaltsUtils SDK:

    export SALTS_ROOT=<nuget>/salts.native/<resolved>/sdk/linux-x64
    export SALTS_UTILS_ROOT=<nuget>/saltsutils.native/<resolved>/sdk/linux-x64

    find_package(SaltsUtils CONFIG REQUIRED PATHS "$ENV{SALTS_UTILS_ROOT}" NO_DEFAULT_PATH)
