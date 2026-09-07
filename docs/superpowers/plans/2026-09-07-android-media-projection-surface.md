# Android MediaProjection Surface Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 Salts Capture 增加稳定的 Android MediaProjection surface adapter，并让 TurboMedia 删除重复的私有 screen backend。

**Architecture:** Salts 的 `salts_capture_t` 保持唯一 native owner；Android 专用头只负责配置、JNI Surface 转换和 frame-count 查询。TurboMedia Java 继续拥有 MediaProjection/VirtualDisplay，JNI 只调用已安装的 Salts 公共 API。

**Tech Stack:** C11、Android NDK AImageReader/ANativeWindow/JNI、Salts Capture、TinyTest、CMake Presets、Java MediaProjection。

**Spec:** `docs/superpowers/specs/2026-09-07-android-media-projection-surface-design.md`

## Global Constraints

- 不公开 `android_screen_ctx_t`、`platform_ctx` 或 `ANativeWindow *`。
- 不增加 compatibility alias、默认路径搜索或 runtime fallback。
- Java 是 MediaProjection/VirtualDisplay 唯一 owner；Salts 是 ImageReader/capture 唯一 owner。
- JNI control-plane 调用必须串行；callback payload 只在 callback 返回前有效。
- Android 专用配置的 width、height、framerate 非正数时 fail fast。
- TurboMedia CMake 只删除旧 source，不新增 helper function 或探测分支。

---

### Task 1: 用 Android contract test 固定公共 API

**Files:**
- Create: `capture/include/salts_capture_android.h`
- Modify: `capture/tests/test_capture_android.c`

**Interfaces:**
- Consumes: `<salts_capture.h>` 的 `salts_capture_t`、错误码与通用生命周期。
- Produces: `salts_android_screen_capture_config_t`、`salts_android_screen_capture_create()`、`salts_android_screen_capture_get_surface()`、`salts_android_screen_capture_get_frame_count()`。

- [ ] **Step 1: 先写失败测试**

在 `test_capture_android.c` 包含尚不存在的 `<salts_capture_android.h>`，加入两个行为测试：

```c
it("rejects invalid Android screen dimensions and clears the output") {
    salts_android_screen_capture_config_t config = {0, 720, 30};
    salts_capture_t *capture = (salts_capture_t *)(uintptr_t)1;

    int result = salts_android_screen_capture_create(&config, &capture);

    check_equal(result, SALTS_CAPTURE_ERR_FORMAT);
    check_null(capture);
}

it("creates one Salts-owned Android screen capture") {
    salts_android_screen_capture_config_t config = {640, 360, 24};
    salts_capture_t *capture = NULL;
    uint64_t frame_count = UINT64_MAX;

    int result = salts_android_screen_capture_create(&config, &capture);
    check_equal(result, SALTS_CAPTURE_OK);
    check_not_null(capture);
    if (!capture) return;

    check_equal(salts_android_screen_capture_get_frame_count(
                    capture, &frame_count), SALTS_CAPTURE_OK);
    check_equal(frame_count, 0);
    salts_capture_destroy(capture);
}
```

- [ ] **Step 2: 验证 RED**

Run: `cmake --fresh --preset android-arm64-v8a-release-win`

Run: `cmake --build --preset android-arm64-v8a-release-win --target capture_android_test`

Expected: compile fails because `salts_capture_android.h` does not exist.

- [ ] **Step 3: 声明最小公共接口**

创建 Android-only 头；`__ANDROID__` 外不包含 JNI 头且不声明平台 API：

```c
#include <salts_capture.h>

#if defined(__ANDROID__)
#include <jni.h>

typedef struct {
    int width;
    int height;
    int framerate;
} salts_android_screen_capture_config_t;

SALTS_CAPTURE_API int salts_android_screen_capture_create(
    const salts_android_screen_capture_config_t *config,
    salts_capture_t **out_capture);
SALTS_CAPTURE_API int salts_android_screen_capture_get_surface(
    JNIEnv *env, salts_capture_t *capture, jobject *out_surface);
SALTS_CAPTURE_API int salts_android_screen_capture_get_frame_count(
    const salts_capture_t *capture, uint64_t *out_frame_count);
#endif
```

- [ ] **Step 4: 重新构建，确认进入 link RED**

Run: `cmake --build --preset android-arm64-v8a-release-win --target capture_android_test`

Expected: link fails with undefined `salts_android_screen_capture_*`, proving the test consumes real exported functions.

### Task 2: 实现单 owner Android adapter

**Files:**
- Modify: `capture/src/android/capture_android.c`
- Modify: `capture/src/android/capture_screen_android.c`
- Modify: `capture/include/salts_capture_android.h`
- Modify: `capture/README.md`

**Interfaces:**
- Consumes: private `android_screen_create/get_surface/get_frame_count` and common capture lifecycle.
- Produces: Task 1 的三个公共函数；通用 `salts_screen_capture_create()` 继续保持 1280x720 默认行为。

- [ ] **Step 1: 实现共享 constructor**

在 `capture_android.c` 用一个 `static` helper 完成参数校验、capture/platform 分配、private backend 创建、callback 连接和输出提交。Android 专用 create 要求明确尺寸；通用 create 以 1280x720 和既有 framerate 调用同一 helper。

- [ ] **Step 2: 实现 Surface 转换与 frame-count 查询**

包含 `<android/native_window_jni.h>`。Surface API 在转换前清空 `*out_surface`，验证 env、screen capture type 和 native backend；frame-count API 在验证前清零输出，并通过现有 atomic counter 读取。

- [ ] **Step 3: 验证 GREEN**

Run: `cmake --build --preset android-arm64-v8a-release-win --target capture_android_test`

Expected: compile and link succeed.

- [ ] **Step 4: 更新所有权文档**

在 `capture/README.md` 写明 Java/Salts 双 owner 边界、JNI local reference、VirtualDisplay-before-destroy 顺序、frame callback borrowed 生命周期和无 fallback 约束。

- [ ] **Step 5: 运行上游回归**

Run: `cmake --fresh --preset win-release-user`

Run: `cmake --build --preset win-release-user`

Run: `ctest --preset win-release-user --output-on-failure`

Expected: 36 tests, 0 failures.

- [ ] **Step 6: 提交上游改动**

```text
feat(capture): expose Android MediaProjection surface adapter
```

### Task 3: 在旧 SDK 上证明 TurboMedia dependency RED

**Files:**
- Modify: TurboMedia `tests/test_android_capture.c`

**Interfaces:**
- Consumes: SaltsUtils Task 1 的 Android 专用公共头和 create/query API。
- Produces: TurboMedia 对安装 SDK 的 compile/link contract。

- [ ] **Step 1: 添加 consumer contract**

在 Android-only test 中包含 `<salts_capture_android.h>`，使用 `{640, 360, 24}` 创建 capture，断言初始 frame count 为 0，并通过通用 destroy 释放。

- [ ] **Step 2: 验证 RED**

Run: `cmake --fresh --preset android-arm64-v8a-release-win`

Run: `cmake --build --preset android-arm64-v8a-release-win --target turbo_media_test_android_capture`

Expected: 旧 `$SALTS_UTILS_ROOT` 缺少 `salts_capture_android.h`，configure/build 明确失败，不选择其他 SDK。

### Task 4: 安装上游 SDK 并迁移 TurboMedia JNI

**Files:**
- Modify: TurboMedia `media/mobile/android/src/jni/turbo_media_jni.c`
- Modify: TurboMedia `media/mobile/android/CMakeLists.txt`
- Delete: TurboMedia `media/mobile/android/src/capture/capture_screen_android.c`
- Modify: TurboMedia `media/README.md`
- Modify: TurboMedia `media/mobile/android/README.md`

**Interfaces:**
- Consumes: 已安装的 `<salts_capture_android.h>` 和 `Salts::Capture`。
- Produces: 保持原 Java native method signatures 的 JNI adapter；不再链接任何 private `android_screen_*` symbol。

- [ ] **Step 1: 安装匹配 profile 的 SaltsUtils**

Run: `cmake --build --preset install-android-arm64-v8a-release-win`

Expected: `$SALTS_UTILS_ROOT/include/salts_capture_android.h` 与更新后的 `Salts::Capture` 被安装。

- [ ] **Step 2: 让 dependency contract 变绿**

重新 fresh configure/build `turbo_media_test_android_capture`；Expected: 新公共头和符号成功编译链接。

- [ ] **Step 3: 将 JNI handle 迁移为 `salts_capture_t *`**

删除 private forward declarations 和 native-window includes。`nativeCreate` 调用 Android adapter，`nativeGetSurface` 返回 adapter 创建的 local `Surface`，`nativeStart/Stop/Destroy` 使用通用 lifecycle，`nativeGetFrameCount` 使用只读 query。所有失败保留现有 JNI false/null/zero 语义。

- [ ] **Step 4: 删除重复 backend**

从 `turbo_media_android` sources 删除 `src/capture/capture_screen_android.c`，再删除该文件。不得新增 CMake helper、存在性检查或 fallback source。

- [ ] **Step 5: 验证 Android build/install**

Run: `cmake --fresh --preset android-arm64-v8a-release-win`

Run: `cmake --build --preset android-arm64-v8a-release-win`

Run: `cmake --build --preset install-android-arm64-v8a-release-win`

Expected: JNI library 和 test target 编译链接，安装产物不包含 private backend source contract。

- [ ] **Step 6: 更新 consumer 文档并提交**

文档删除 #25 临时 bridge 描述，记录 Java 先 release VirtualDisplay、后 stop/destroy Salts capture 的顺序。

```text
refactor(android): consume Salts screen surface adapter
```

### Task 5: 双仓库最终验证与 PR

**Files:**
- Verify only: 两仓库全部改动与 `.codegraph/` 状态。

**Interfaces:**
- Consumes: Task 2 上游 API 与 Task 4 consumer。
- Produces: 两个顺序依赖、可独立审查的 PR；TurboMedia PR 关联并关闭 #25。

- [ ] **Step 1: 同步 CodeGraph 并运行静态边界扫描**

SaltsUtils 扫描 public header、backend 和 tests；TurboMedia 扫描确认生产代码无 `android_screen_*`、`android_screen_ctx_t` 和重复 backend 文件。

- [ ] **Step 2: 运行两仓库 Windows full regression**

各自执行 fresh configure、full build、full CTest。Expected: SaltsUtils 36/36、TurboMedia 88/88。

- [ ] **Step 3: 审查差异**

检查 public API、JNI local-reference ownership、callback/destroy race、Android link、安装导出、CMake 无 fallback，以及 `git diff --check`。

- [ ] **Step 4: 创建顺序 PR**

先创建 SaltsUtils PR；TurboMedia PR 明确依赖其 merge/install，并使用 `Closes #25`。两个 PR 都列出已执行平台；没有设备 instrumentation 结果时明确标为未验证。

