# Android MediaProjection Surface Adapter Design

## 背景与目标

TurboMedia 的 Java `ScreenCapture` 负责申请 MediaProjection 权限并创建
`VirtualDisplay`，但当前 JNI 直接链接 SaltsUtils 私有的 `android_screen_*`
符号。已安装的 `<salts_capture.h>` 只提供通用 screen capture 生命周期，无法把
Salts 拥有的 `AImageReader` surface 安全交给 Java。

本改动为 Salts Capture 增加 Android 专用公共 adapter，使 Salts 成为唯一 native
capture owner，并允许 TurboMedia 删除重复的 Android screen backend。关联消费端
issue：`qigao/turbomedia#25`。

## 非目标

- 不把 MediaProjection 或 VirtualDisplay 的所有权移入 native 层。
- 不向消费端暴露 `android_screen_ctx_t`、`platform_ctx` 或 `ANativeWindow *`。
- 不修改非 Android 平台的 screen capture 行为。
- 不提供旧私有符号、运行时探测或 fallback。

## 候选方案

### A. 公开 `ANativeWindow *`

拒绝。`AImageReader_getWindow()` 返回的 window 由 reader 管理，调用方不得
`ANativeWindow_release()`；把裸指针公开会扩散平台类型并使 release 责任不清。

### B. 扩展通用 `salts_screen_capture_config_t` 并返回 `void *` surface

拒绝。新增 width/height 会改变通用结构布局，`void *` 无法表达 JNI local-reference
语义，还会把 Android 生命周期混入所有平台。

### C. Android 专用头 + 既有 `salts_capture_t` owner

采用。`<salts_capture_android.h>` 仅在 Android 暴露有类型的 JNI adapter；创建后
仍使用通用 callback/start/stop/destroy API。这样没有第二套 native 状态，也不需要
兼容层。

## 公共接口

```c
typedef struct {
    int width;
    int height;
    int framerate;
} salts_android_screen_capture_config_t;

SALTS_CAPTURE_API int salts_android_screen_capture_create(
    const salts_android_screen_capture_config_t *config,
    salts_capture_t **out_capture);

SALTS_CAPTURE_API int salts_android_screen_capture_get_surface(
    JNIEnv *env,
    salts_capture_t *capture,
    jobject *out_surface);

SALTS_CAPTURE_API int salts_android_screen_capture_get_frame_count(
    const salts_capture_t *capture,
    uint64_t *out_frame_count);
```

接口只在 `__ANDROID__` 下声明。`create` 成功后 `*out_capture` 由调用方唯一拥有，
最终由 `salts_capture_destroy()` 释放。所有失败路径先把输出清零：非法参数或错误
capture 类型返回 `SALTS_CAPTURE_ERR_FORMAT`，分配失败返回
`SALTS_CAPTURE_ERR_NOMEM`，ImageReader/surface 创建失败返回
`SALTS_CAPTURE_ERR_DEVICE`。

`get_surface` 返回当前 JNI native method 的 local reference。按照 Android NDK
契约，转换所得 Java `Surface` 持有自己的 native-window reference，并由 Java 对象
生命周期自动释放。ImageReader 返回的原始 `ANativeWindow` 仍由 ImageReader 管理，
Salts 不对它调用 `ANativeWindow_release()`：

`ANativeWindow_toSurface()` 从 Android API 26 可用，因此 SaltsUtils Android
构建的最低平台提升为 API 26。公开接口直接使用官方 API，不使用私有 API、
反射、动态符号或运行时 fallback；以更低平台自定义构建会在编译期失败。

Android cross build 查找 Salts 时只使用 `SALTS_ROOT` 指向的已安装 target SDK。
`NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH` 防止 Android toolchain 把这个显式绝对路径
再次重定位到 sysroot，也禁止从 host 或系统默认路径选择另一份 Salts 包。

- https://developer.android.com/ndk/reference/group/media#aimagereader_getwindow
- https://developer.android.com/ndk/reference/group/native-activity#anativewindow_tosurface
- https://developer.android.com/ndk/guides/jni-tips#local-and-global-references

## 所有权、线程与生命周期

| 对象 | 唯一 owner | 可用线程/角色 | 失效点 |
|---|---|---|---|
| `salts_capture_t` 与 ImageReader | Salts Capture | Java/JNI control thread 串行调用 | `salts_capture_destroy()` 返回 |
| MediaProjection / VirtualDisplay | TurboMedia Java | Android main/control thread | Java `stop()` / `release()` |
| JNI `Surface` local reference | 当前 JNI frame，返回后由 Java 对象持有 | 创建它的 JNI 调用 | JNI frame 结束；Java 对象另持 native ref |
| I420 callback frame | Salts callback producer | ImageReader callback 内只读 | callback 返回 |
| frame count | Salts capture | producer 原子递增，control thread 原子读取 | capture destroy |

控制面顺序：

1. `salts_android_screen_capture_create()` 创建 capture/ImageReader。
2. JNI 调用 `get_surface()`，Java 用返回值创建 VirtualDisplay。
3. VirtualDisplay 成功后调用 `salts_capture_start()`。
4. 停止时 Java 先 release VirtualDisplay 并停止 MediaProjection，使 producer 不再写入。
5. 调用 `salts_capture_stop()`，随后才允许 `salts_capture_destroy()`。

同一 capture 的 create/get-surface/start/stop/destroy 不并发调用，也不从 frame
callback 中 stop/destroy。frame callback 只收到 borrowed I420 view；跨 callback 保留
必须复制。

## 状态与失败语义

- 配置的 width、height、framerate 必须全部大于零；不猜测默认值。
- Android 专用 create 不自动退回通用 1280x720 create。
- 通用 `salts_screen_capture_create()` 保持现有默认 1280x720 行为，并复用同一内部
  constructor。
- `get_surface()` 只接受 Android screen capture；错误类型 fail fast。
- 输出参数在任何失败时保持 `NULL` 或 `0`。
- native start 失败时通用 capture 状态进入 `ERROR`；Java 必须 release 已创建的
  VirtualDisplay，之后可 destroy，不把失败当成 started。

## 迁移与回滚

先合并并安装 SaltsUtils adapter，再提交 TurboMedia consumer PR。TurboMedia 只需要：

- JNI handle 改为 `salts_capture_t *`；
- 使用 Android adapter 创建、取得 Surface、读取 frame count；
- 使用通用 start/stop/destroy；
- 从 CMake 和源码树删除重复 `capture_screen_android.c`。

回滚以提交为单位：先回滚 TurboMedia consumer，再回滚 SaltsUtils adapter，并恢复与
各提交匹配的 SDK。运行时不保留双实现或 fallback。

## 验证

- SaltsUtils Android arm64 configure/build/install，`capture_android_test` 编译链接。
- TurboMedia Android arm64 configure/build/install，native library 无私有
  `android_screen_*` 未解析符号。
- Android instrumentation 覆盖 permission、Surface、VirtualDisplay、start/stop、
  frame count 与 destroy。
- 两仓库 Windows Release clean build/full CTest，证明非 Android 行为不回归。
