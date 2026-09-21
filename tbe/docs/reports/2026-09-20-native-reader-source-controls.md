# Native Reader：可观察输入源控制

关联 #99；本切片在已合并 #102 的 `da35ac59da3be60c13b104354b3eb05df88a0bbe` 上推进。
它实现实施计划 Task 2 中的测试输入源，不代表 Task 1/2 或 direct-reader 功能整体完成。

## 已完成的前置事实

#102 的 exact head `f368b83556be8080265b967ac8d0685ac50e93f6` 经审查后合并。
原始 CI：enum `35518458752`、descriptor `35518458765` 均成功。
下载的 enum artifact `10607647063` SHA-256 为
`0ffd99ffddc638ff276e31675f7c10c44de4e1374f487d2ce0ebfe513e518cf7`。
其中 head.txt 与提交相符；完整 TBE 56/56；native-storage 18/18、190 个断言，
0 failed/skipped。四个 tagged STRING/BYTES × flat/nested Struct 用例均为
`clear_status=0 releases=1 tag=23063 zero=1`。这是旧前置提交的真实结果，
不是本切片的新 CI 或消费者验证结果。

## 新增测试输入源

`tests/native_storage/reader_probe.h` 只实现真实 CSerde `next` callback：
原样发出脚本 token/错误、记录调用次数与脚本位置、复用 transient 缓冲区。
每次源回调（包括 DONE 或源错误）都先使旧 transient slice 失效。
STABLE slice 保留源指针；它的实际寿命仍受脚本/源 owner 限制。

没有 DataBind decoder、替代类型图、隐式 framing、rollback、SQL owner hold、
reader rewind、CBind 包装器或 production 生命周期实现。fixture 的固定容量只是
测试源自身容量，溢出不能作为 DataBind budget 行为已经被验证的证据。

九个 TinyTest 控制用例覆盖：打开时零读取、连续两个根值与可观察的额外 EOF 读取、
transient STRING/BYTES 复用与嵌入 NUL、DONE 时失效、STABLE 源指针、源错误及禁止
重放、malformed token、不补齐截断 map、fixture 容量失败及显式使用新的零状态 reader。
截断用例调用的是上游真实 `cserde_reader_skip_value`，不是新的 DataBind 转换器。

该普通 CTest 目标位于现有 `tbe/all` 子树；复用现有完整 TBE CI，不改变 workflow、
依赖 pin、安装/导出逻辑，也不新增 Python 行为测试或 install/verify 框架。
原有 18 个 native-storage 用例保持不变。

## 审查基线与下一道门槛

直接阅读的 Salts 基线为现有 CI pin
`801202e58c2d86b35202414d4812e79a2fd25bae` 的 `cserde/reader.h`、`token.h`、
`status.h`、`cserde/src/reader.c`。真实 reader 要求零状态初始化，错误/DONE 为粘性状态，
不能由 fixture 偷偷重新初始化以掩盖消费错误。

本地无 codegraph 可执行程序，采用已知源码人工阅读与 ripgrep 检索。
本地无可用完整 Salts SDK，未声称本地完整构建、运行或 C/C++/Windows 验证通过。
新 CI 必须单独核对 exact head、测试实际执行和完整回归，不能借用 #102 的绿灯。

剩余 HIGH 契约仍在 #99：正式公共 ABI/符号及错误诊断扩展、工作区/owned payload 预算
与 provider 能力、schema-free graph 预检、原子发布和失败回滚。后续 direct-reader 测试
应复用这个已验证输入源，并分别报告接口缺失 RED 与真实行为 RED；不得发布空函数
或把这些源控制用例当作 DataBind 解码成功。消费者能力盘点及 native/dynamic 等价也未完成。

TurboDB #52、#28 的 cursor/reservation/cleanup、上游 CBind #305 退役均保持独立门槛。
本切片不改 TurboDB，也不删除任何 ownership RED 用例。
