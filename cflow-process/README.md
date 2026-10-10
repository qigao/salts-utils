# CFlowProcess

`Salts::Process` combines the existing Core process owner with the
CFlow Actor over the canonical NativeIO pipe adapter. It is a separate target because Core already uses
CFlow internally; placing this adapter in CFlow would create a dependency cycle.

The adapter owns one `cmeta_process_t`, three parent-side asynchronous pipe
endpoints, one fixed-capacity NativeIO adapter, one manual Executor, one IO Actor,
and exactly `request_capacity` operation slots. It never exposes raw endpoints,
captures output into an unbounded buffer, or creates a second process state
machine.

## API contract

`cflow_process_start(process, options, config)` borrows `options` and `config`
only for the call. It rejects Core capture/pipe flags because the adapter must
remain the only standard-stream consumer. `backend_kind`, `request_capacity`,
`command_capacity`, `completion_batch_capacity`, and `completion` are required;
unsupported native pipe backends return `SALTS_ENOTSUP` without fallback.

`try_write_stdin`, `try_read_stdout`, and `try_read_stderr` borrow each buffer
until its terminal callback returns. Accepted operations receive a nonzero
request ID and exactly one `OK`, `EOF`, `CANCELLED`, or `FAILED` completion.
Reads and writes may complete with fewer bytes than requested. Full request or
command capacity is returned as a typed submit/cancel result; storage never
grows after start.

`cflow_process_close_stdin()` returns `SALTS_EBUSY` while an admitted stdin
write remains live. After it succeeds, the child observes EOF and later stdin
submissions return `CLOSED`. `cflow_process_get_stats()` exposes the Actor's
bounded admission counters together with endpoint ownership and close state.

Submission and cancellation inherit the IO Actor's MPSC contract. Exactly one
driver calls `cflow_process_run_ready()`, and callbacks execute on that driver.
Stop and join producers before lifecycle calls. `close()` closes admission,
requests cancellation, and terminates a live child; continue driving until
`is_quiescent()`, then call `destroy()`. No endpoint is closed before its
authoritative operation completion has been delivered and acknowledged.

## Complete lifecycle example

```c
#include <salts/process.h>

#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/thread.h>

#include <stdint.h>

static void completed(void *user, cflow_io_request_id request_id,
                      cflow_io_lease_id lease_id,
                      cflow_process_stream stream,
                      const cflow_io_completion *completion) {
    (void)user;
    (void)request_id;
    (void)lease_id;
    (void)stream;
    (void)completion;
}

int main(void) {
    cflow_process process = {0};
    cflow_process_config config = {0};
    cmeta_process_options_t options;
    const uint64_t deadline_ns = UINT64_C(5000000000);
    uint64_t started;
    int status;

    cmeta_process_options_init(&options);
#if defined(_WIN32)
    {
        static const char *args[] = {
            "-NoProfile", "-Command", "exit 0", NULL};
        options.program = "powershell.exe";
        options.args = args;
        config.backend_kind = CFLOW_IO_NATIVE_IOCP;
    }
#elif defined(__linux__)
    {
        static const char *args[] = {NULL};
        options.program = "/usr/bin/true";
        options.args = args;
        config.backend_kind = CFLOW_IO_NATIVE_EPOLL;
    }
#elif defined(__APPLE__)
    {
        static const char *args[] = {NULL};
        options.program = "/usr/bin/true";
        options.args = args;
        config.backend_kind = CFLOW_IO_NATIVE_KQUEUE;
    }
#else
    #error "CFlowProcess example requires a NativeIO pipe backend"
#endif
    options.flags = 0u;
    config.request_capacity = 4u;
    config.command_capacity = 4u;
    config.completion_batch_capacity = 4u;
    config.completion = completed;

    status = cflow_process_start(&process, &options, &config);
    if (status != SALTS_OK) return 1;
    status = cflow_process_close(&process);
    if (status != SALTS_OK) return 2;

    started = cmeta_hrtime();
    while (!cflow_process_is_quiescent(&process)) {
        size_t progressed = 0u;
        status = cflow_process_run_ready(&process, 32u, &progressed);
        if (status != SALTS_OK) return 3;
        if (cmeta_hrtime() - started > deadline_ns) return 4;
        if (progressed == 0u) cmeta_sleep_ms(1u);
    }
    return cflow_process_destroy(&process) == SALTS_OK ? 0 : 5;
}
```

Build consumers with:

```cmake
find_package(SaltsUtils CONFIG REQUIRED)
target_link_libraries(app PRIVATE Salts::Process)
```

The cross-platform capability matrix, state machines, ownership proofs, and
rollback boundary are recorded in
[`../docs/superpowers/specs/2026-08-28-cflow-pipe-rendezvous-subprocess-design.md`](../docs/superpowers/specs/2026-08-28-cflow-pipe-rendezvous-subprocess-design.md).

## Shell 命令执行

`<salts/shell.h>` 在同一个 `Salts::Process` target 中提供单条 shell 命令执行。
接口参考 Praktor 的 `ShellExecutor`，支持输入、双路输出收集、字节流回调、
环境、工作目录、超时和取消。它不是终端或持续会话，不提供 PTY、历史记录或 job control。

| 接口 | 行为 |
| --- | --- |
| `cflow_shell_options_init` | 初始化 30 秒超时、16 MiB 输入/输出限制、4 KiB I/O 块及平台后端 |
| `cflow_shell_start` | 复制输入并启动命令；失败时 handle 保持空状态 |
| `cflow_shell_run_ready` | 单线程驱动有限步数，补充每个流至多一个请求 |
| `cflow_shell_poll` | 只读查询；清理完成前返回 `SALTS_EBUSY` |
| `cflow_shell_cancel` | 关闭接纳并请求终止；随后仍须驱动到终态 |
| `cflow_shell_execute` | `start` 加同步驱动；保留 handle 以便读取输出 |
| `cflow_shell_stdout/stderr` | 获取借用的连续字节缓冲，长度由 result 提供 |
| `cflow_shell_destroy` | 仅在 quiescent 状态释放资源并清空 handle |

完整的可编译调用示例见 [`examples/shell_example.c`](examples/shell_example.c)，
异步、取消、回调和边界用法见 [`tests/cflow_shell_test.c`](tests/cflow_shell_test.c)。
调用者先零初始化 handle，再调用 options 初始化函数。`execute` 返回 `SALTS_OK`
表示取得了完整结果；命令成功还要求 `outcome == CFLOW_SHELL_EXITED` 且 `exit_code == 0`。
启动/驱动错误通过函数返回值传播，非零退出、信号、超时、取消、输出超限、I/O 失败
通过 result 区分。若驱动报错，非空 handle 仍由调用者拥有，不能提前释放其存储。

### 架构决策与兼容性

本层只持有输入、输出、三个 I/O 游标和关闭原因；OS 进程状态仍以 Core 为唯一事实源。
没有新增线程或依赖。相比复制 Praktor 的平台后端，这一方案复用现有进程树、
NativeIO 和 IO Actor 生命周期；相比只包装 `cmd -c`，它提供完整的有界 I/O 收尾。
普通 `cflow_process` 的公开接口不变。

需要同时更新 Salts Core 的 `SALTS_PROCESS_SHELL_COMMAND` 能力和 NativeIO 的
同步 pipe EOF 处理。新 flag 不改变 `cmeta_process_options_t` 的布局：显式开启时，
`program` 是命令正文且 `args` 必须为 NULL；未开启时仍是普通可执行程序和 argv。
旧 Core 会拒绝未知 flag，旧 SDK 头文件会使 shell 源码编译失败，不存在静默降级。
SDK 发布时必须先发布配套 Salts，再构建 salts-utils。

Windows 从系统目录定位 `cmd.exe`，用 `/d /s /c` 外层引号保留命令正文；POSIX 使用
`/bin/sh -c`。命令正文属于 shell 语法，参数数组调用应继续使用 `cflow_process`。
Windows 引号依据 [cmd 官方契约](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/cmd)。
同步 `ReadFile` 返回 pipe EOF 时，IOCP 显式排入一个零字节完成事件，和异步 EOF 共用
完成路径；参见 [IOCP 完成语义](https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports)。

**HIGH｜兼容性边界：** 显式 shell 模式的后台任务不能越过根 shell 的生命周期。
根退出后，Core 终止残留 Job/process-group 成员，再发布根的退出结果，避免后台任务
持有输出句柄导致永久等待。POSIX 使用 `waitid(WNOWAIT)` 保留根身份，完成组清理后
再 reap，避免对已复用的 PID/PGID 操作；普通进程模式的行为不变。
主动脱离 process group/job 的进程不属于该清理保证。

迁移只需改用新 header 和命令接口，无持久化格式变化；回滚时移除 shell 调用及
新增源文件即可恢复原 adapter。Core 的新 flag 是增量扩展，现有调用者无需修改。

### 数据、容量与关闭协议

- 数据单元是字节。输入在 start 时复制，原输入可立即释放；callback user 保持有效到 destroy。
- 所有 shell API 由同一线程调用，回调在该 driver 上执行且必须及时返回，不能重入生命周期 API。
  各个流内有序，stdout 与 stderr 之间不承诺全局顺序。
- 每个流最多一个未完成请求。`FULL` 或尚未释放的 lease 只推迟提交，下一次驱动继续；
  已接纳的缓冲保留到终态回调和 Actor 确认完成。
- `max_input_bytes` 是输入硬上限，`max_output_bytes` 是两路输出的共享硬上限，
  `io_chunk_bytes` 控制 I/O 请求长度。零容量或不可安全分配的极端容量在 start 时拒绝。
- 两路输出在 start 时通过 `tstr_reserve` 预留存储，数据路径不扩容。设输入长 I、
  输出限制 L、块长 C，当前 tstr 的预留量 R(L) 在 L 小于 1 MiB 时为 2L，
  否则为 L + 1 MiB；payload 预算为 `I + 2*R(L) + 2*C`，另加字符串 metadata、
  shell 结构、6 个 process/NativeIO request slots、8 个 command slots、底层 handles。
  默认输出预留约 34 MiB，可按实际命令调低上限。时间复杂度 O(I+O)，额外空间 O(I+L+C)。
- 输出恰好达到限制仍可成功；下一字节使结果成为 `OUTPUT_LIMIT`，保留已捕获的前缀，
  停止进程树并 drain。callback 收到的累计字节也不超过此限制。二进制 NUL 不影响长度。
- 输入全部写完后关闭 stdin，产生 EOF；stdout/stderr 持续读取至 EOF。正常完成要求
  Core 终态、输出排空、close 与所有 I/O 释放同时满足，不能仅看根进程退出。
- 取消或 I/O 失败时停止接纳、终止进程树、驱动已接纳请求到终态，再关闭端点和销毁。
  结果保留首个 I/O 错误；超时/显式取消优先于其引起的断管错误，输出超限单独标记。
  提前拒绝 stdin 是显式 I/O 失败，不被当作成功吞掉。

### 本地验证

Windows 在 `VsDevCmd.bat` 环境中，先通过 Salts 的 `install-win-dev-user` 安装对应
Debug SDK，再设置 `SALTS_ROOT` 为该安装目录。保持两个工程的 Debug/ASan profile 一致：

```powershell
cmake --preset win-dev-user
cmake --build --preset win-dev-user --target cflow_shell_test cflow_process_test cflow_process_header_cpp_test cflow_shell_example
ctest --preset win-dev-user -R '^cflow_(shell|process).*test$' --output-on-failure
```

Salts 侧运行 `test_cmeta_process`、`native_io_test` 与相邻的 `cflow_io_actor_test`、
`cflow_io_native_test`。正式测试覆盖双路尾部输出、256 KiB duplex、输入复制、二进制、
容量边界、单步驱动、环境与 cwd、超时、取消、后台子进程及 Windows 引号。
