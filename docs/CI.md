# CI 与发布

工作流参照相邻 Salts 仓库的 `ci.yml`、`native-build.yml`、`native-tests.yml`、
`sdk-package.yml` 和 `native-sdk-release.yml` 分工，保留 SaltsUtils 自身的 SDK、
Capture、宿主生成器及 sanitizer 配置。CI 只编排已有 CMake/CTest graph。

## 分工与事实源

| 入口 | 职责 |
|---|---|
| [ci.yml](../.github/workflows/ci.yml) | PR、master push、手动入口；提供统一 `CI result` 状态 |
| [select-ci-scope.ps1](../cmake/ci/select-ci-scope.ps1) | 根据实际 git diff 生成构建和测试矩阵 |
| [native-build.yml](../.github/workflows/native-build.yml) | 每个配置完整编译一次，上传构建产物；发布准备时安装 SDK |
| [native-tests.yml](../.github/workflows/native-tests.yml) | 恢复相同提交的产物和工具链，运行完整 CTest graph，不重建主工程 |
| [sdk-package.yml](../.github/workflows/sdk-package.yml) | 合并本轮七个平台 SDK，生成 NuGet 工件 |
| [native-sdk-release.yml](../.github/workflows/native-sdk-release.yml) | 校验既有 tag、精确 SHA 和成功准备 run，原样发布包 |

矩阵由 selector 单独维护；编译器、模块开关和依赖路径由 `CMakeUserPresets.json`
维护。普通文档修改不启动 native job；其他路径默认触发完整配置，新模块不需要追加
源码路径或 target 白名单。手动运行始终选择完整图。新增构建依赖或功能后，失败应在
所属 CMake/preset/模块中修复，不能通过排除对应测试来恢复绿色。

vcpkg 优先读取共享二进制缓存；未命中时按 manifest 和 baseline 正常从源码构建，
生成的缓存只写入本地目录。CI 不要求所有平台的新依赖事先发布到共享 feed；
认证、下载与编译失败仍立即终止构建。

iOS preset 通过 `cmake/vcpkg-ports/quickjs-ng` 修正工具打包：保持共享 port 的
QuickJS-ng 0.16.2 版本、源码摘要和库配置，但依照
[上游 iOS 安装规则](https://github.com/quickjs-ng/quickjs/blob/v0.16.2/CMakeLists.txt#L517)
不复制未生成的 `qjs/qjsc`。共享 port 来源为
[qigao/vcpkg-cache 6f23a1e](https://github.com/qigao/vcpkg-cache/tree/6f23a1e4a5fc51ed5377d92611b66769f87280e9/ports/quickjs-ng)。
其他平台继续使用共享 port；共享版本修正并经 iOS device/simulator 构建验证后，
删除该本地 recipe 和 preset 中的 overlay 设置。

普通 CI 覆盖 Linux x64、Windows x64、macOS arm64 Release 和 Linux ASan/UBSan
构建与测试，以及 Android arm64-v8a、iOS device/simulator arm64 交叉构建。
发布准备额外加入 Linux arm64 Release；该平台保持现有 Capture 关闭契约。
交叉编译成功不代表移动设备运行测试通过。Release 与 sanitizer 使用独立构建树。
macOS C/C++ 使用与 Salts 发布 SDK 一致的 GCC 15，保证 TinyTest 等库的 TLS ABI
一致；平台 Objective-C 源码使用 Apple Clang。构建与测试 host 都安装 GCC 运行库。

## 构建产物与生命周期

每个 build job 是其源码、编译产物和已解析依赖的唯一生产者。Salts/re2c 的 latest
只在该 job 中解析，包存放于工作区 `stage/nuget`，随构建归档交给测试 job。
测试 job 不再解析 latest；不同 profile 不共享可变构建树。
构建和测试 job 使用同一版本的共享 vcpkg setup action，以只读模式恢复临时目录中的
工具链、triplet 和缓存凭据。现有包配置测试会启动嵌套 CMake，并继承生产构建的
编译器、配置与 sanitizer 链接参数；仅恢复 vcpkg 安装树不足以运行这些测试。

恢复前核对 commit、工作区绝对路径、OS/架构和 profile。归档同时保存源文件时间戳、
构建树、vcpkg 安装树、SDK 以及公开 SDK 路径，不保存 token 或 NuGet 凭据配置。
runner image 版本作为 provenance 记录；ABI 或动态库加载失败仍由 CTest 报错。
Windows 恢复原 triplet，通过 preset 的 PATH 选择依赖；不手工复制 DLL。

与每个测试 job 重建相比，复用编译产物减少重复构建和依赖漂移，但增大 artifact
上传量，并要求同平台 job 使用相同工作区路径。与原先按 target 分散构建相比，完整图
能覆盖新模块，代价是单次构建与测试时间增加。编译归档保留 7 天，发布准备包保留
14 天；过期必须重新执行准备流程，不能拿其他提交的包替代。

## 发布与迁移

不再由 tag push 自动编译发布。先提交变更，确认根 CMake 与 vcpkg 的版本一致，然后：

1. 在默认分支手动运行 `ci.yml`，设置 `prepare_release=true`。
2. 等待整轮成功，记录该 run ID 和完整 40 位提交 SHA。
3. 为该 SHA 创建匹配版本的既有 `v<version>` tag，不移动已经发布的 tag。
4. 手动运行 `native-sdk-release.yml`，传入 `release_sha`、`tag`、`ci_run_id`。

发布 job 校验准备 run 来自本仓库、默认分支、`ci.yml` 的手动成功运行，并逐个核对包内
七个平台 manifest 的版本、提交与 Release profile。仅 publisher 持有发布权限；PR 和
普通 CI 不发布。包名仍为 `SaltsUtils.Native`，平台 install tree 保持 `sdk/<RID>`。

分支保护应使用新的 `CI result` 状态。需要回滚时，回滚整套 workflow、actions、selector
与对应 preset；不要混用新旧归档协议。旧准备 run 不可作为新发布协议的输入。

## 本地验证

使用 `actionlint` 检查 workflow，使用 PowerShell Parser 检查 selector 语法，使用
`cmake --list-presets`、`cmake --build --list-presets` 和 `ctest --list-presets` 检查入口。
Windows 在 VS 开发环境下设置 SDK、triplet 和共享 vcpkg cache 环境后执行：

```powershell
cmake --preset ci-win-release-user
cmake --build --preset ci-win-release-user --parallel 2
ctest --preset ci-win-release-user --no-tests=error --output-on-failure --timeout 180 --parallel 2
```

本地构建与语法检查不能替代 hosted runner 的跨 job 恢复、七平台构建及发布权限验证。
