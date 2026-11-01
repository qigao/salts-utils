# CI

SaltsUtils 使用与 CMeta、SaltsCore 相同的单工作流 LLVM 矩阵风格。唯一入口是
[`ci.yml`](../.github/workflows/ci.yml)：每个 RID 在同一 job 内完成工具链准备、依赖恢复、
CMake configure、完整 build graph 和 CTest，不通过构建产物把 build/test 拆成不同 job。

## 平台矩阵

| RID | Runner / compiler | CTest |
| --- | --- | --- |
| `linux-x64` | Ubuntu x64 / LLVM | 是 |
| `linux-arm64` | Ubuntu arm64 / LLVM | 是 |
| `windows-x64` | Windows x64 / LLVM-MinGW UCRT | 是 |
| `windows-arm64` | Windows arm64 / LLVM-MinGW UCRT | 是 |
| `macos-x64` | macOS Intel / LLVM | 是 |
| `macos-arm64` | macOS arm64 / LLVM | 是 |
| `android-x64` | Ubuntu host / Android NDK LLVM | 否，仅交叉编译 |
| `android-arm64` | Ubuntu host / Android NDK LLVM | 否，仅交叉编译 |

工具链和 vcpkg contract 直接使用固定提交的 `qigao/vcpkg-cache` actions。三条编译器
路径都显式设置 `obfuscator: "false"`。Windows 不激活 MSVC 环境；target triplet 分别为
`qigao-x64-windows-llvm-mingw` 和 `qigao-arm64-windows-llvm-mingw`。

SaltsCore.Native 与 CMeta.Native 通过仓库的 curl-only restore helper 恢复。Android target
配置前先用同一 runner 的 LLVM 构建 host Lemon，再把该可执行文件显式传给交叉编译图。

## CI 边界

- CI configure 决定完整 build graph；build 步骤不点名 target。
- 原生平台测试统一由 `ctest --preset ci-native` 执行。
- Android 只做交叉编译，不在 runner 上执行 target binaries。
- 工作流不包含 installer、独立 consumer、NuGet pack、release publisher 或 SDK artifact
  传递。普通 CMake install/export 仍是库的产品能力，不是 CI qualification 阶段。
- PR 与 master push 只在实际模块目录变化时自动触发；CI、配置、文档或测试文件本身不单独
  扩大触发范围。需要验证纯 CI 改动时使用 `workflow_dispatch`。

## 本地对应命令

在已经设置 `SALTSCORE_ROOT`、`CMETA_ROOT`、`RE2C_ROOT`、vcpkg contract 和 LLVM
toolchain 环境后：

```sh
cmake --preset ci-native
cmake --build --preset ci-native
ctest --preset ci-native
```

Android 使用 `ci-host` 先构建 host tools，再使用 `ci-android` 配置和构建 target graph。
