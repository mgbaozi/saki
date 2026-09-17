# Saki 0.4.0 产品与通信规格

> 状态：已归档；0.4.0 未单独发布，相关能力已并入 0.4.5。
> 基线：0.3 USB 优先、安全 BLE fallback；不改变配对、安全和分区策略。
> 目标：ESP32-S3 BOX3，320×240，16 MiB Flash / 8 MiB PSRAM。
> [工程设计](./IMPLEMENTATION.md) · [任务与证据](./TASKS.md) · [用户指南](./USER_GUIDE.md)

本文件保留多来源/多 Session 开发基线。未执行的完整真机矩阵已统一迁入 1.0 发布前门槛，
不再作为 0.4.0 在制验收。

## 1. 范围

多 Agent 指 **Codex 与 Claude Code 两种 coding agent 产品**；多 Session 指各产品的多个
用户任务/对话。例如 2 个 Codex 与 2 个 Claude Code 任务共用一个 Mac Host 和一条权威链路。
本版不提供 subagent 树、Agent 调度、远程批准、多 Mac 写入、Wi-Fi 或对话查看。

Host 跟踪两来源合计最多 32 项，设备显示最多 4 项。容量是实现上限；320×240 可读性、
运行时内存和 BLE 性能仍须真机确认，不能以构建成功代替验收。

## 2. 身份、来源和隐私

- source 固定为 `codex` / `claude_code`。模型、标题、工作目录和 PID 不参与身份判断。
- 原生 Session ID 经本机 32 B 密钥 HMAC-SHA256 转成 32 位十六进制标识，包含来源命名空间。
  两来源原生 ID 相同仍为不同 Session；密钥不变时身份跨 Host 重启稳定。
- transport `session` 是 Host 进程 UUID；display Session 是任务身份；run 是任务的一轮请求。
  Codex 有 `turn_id` 时使用其 HMAC，否则和 Claude Code 一样使用本地轮次。
- hook 提取事件类型、身份、白名单工具分类及简短任务目标。仅在 UserPromptSubmit 时，
  在 hook 进程内存中处理 task_title，未得到有效目标时处理 prompt；从首个通过规则检查的行
  提取首个短句，最多保留 48 个字符且不超过 96 UTF-8 B，不调用模型或读取历史对话。
- 目标提取是规则过滤与截取，不是语义概括或完全匿名化。未命中过滤规则的文字会原样保留；
  短请求可能完整成为标题。因此不能将该功能描述为“不读取 prompt”或“设备不接收用户原文”。
  原始 hook 对象和独立的完整 prompt 字段不写入 Saki 检查点或转发到设备；派生 goal 会经过
  本地 IPC、保存在 Host 的 0600 检查点中，并作为 task.title 经 USB/BLE 发送到屏幕。
- 当前规则移除匹配的成对标签块和三反引号代码块，跳过特定前缀行；Markdown 链接保留标签，
  匹配的网址和绝对路径替换为占位，命中常见凭据规则的行被丢弃。相对路径、文件名、姓名、
  邮箱及未识别的凭据等仍可能保留；规则匹配不能保证过滤所有敏感信息。
- 不从 cwd、工具参数/结果、回复或模型自由文本提取目标，不读取 transcript 文件；身份字段
  使用 HMAC 标识。常驻 Host 的常规同步日志不输出目标，安装的 hook 静默运行；显式使用
  `hook --stdout` 会输出含 goal 的归一化事件，其输出同样可能含用户原文片段。
- 工具/结束/resume 事件保留任务目标，“继续”等简短控制请求也保留；新的有效目标可替换标题。
  目标独立于身份和提交顺序。未提供或规则未提取到有效短句时保留原目标，首次无目标才回退来源加短 ID。
  旧 IPC/检查点缺少 goal 时仍可载入；设备继续使用现有 task.title，侧栏仍以短 ID 区分任务。
- `SubagentStart/Stop`、带父 Session/Thread 标识的事件不建用户任务；不读取 transcript 推断关系。
- 无身份的新 hook 拒收。旧 v1 IPC 在尚无来源事件时保留单任务兼容；一旦进入来源模式，
  无身份快照不能覆盖已识别任务。Host 重启后才可重新使用纯旧 IPC 模式。
- 安装后的 hook stdout 为空、exit 0；最多读取 64 KiB，handler 1 秒 deadline，IPC 最多
  4 KiB、发送等待不超过 50 ms，不等待扫描、设备、ACK 或磁盘刷盘。

## 3. 生命周期

| 来源事件 | 状态/行为 |
| --- | --- |
| SessionStart | 创建 idle；同 Session resume 保留现有轮次及显示状态 |
| UserPromptSubmit | 新 run，starting，计时归零 |
| PreToolUse | working；已识别交互提问工具为 waiting_user |
| PostToolUse | thinking，清除等待 |
| PermissionRequest | waiting_approval |
| Stop | completed，文案“本轮回复结束”，不代表整个项目完成 |
| SessionEnd | 保留最后状态，冻结计时，60 秒后移除 |
| Claude PostToolUseFailure | thinking，通用工具失败摘要；不判整轮 failed |
| Claude StopFailure | failed |
| Codex Interrupt / 结构化取消结果 | cancelled |

`Notification` 不作为批准信号，避免迟到提醒把已完成的批准重新显示。Claude Code 缺少可靠
用户取消事件时不推测 cancelled。StopFailure、批准、交互输入等映射有脱敏合成测试；真实
API probe 只验证了开始、请求、工具开始/结束、Stop、SessionEnd。网络沙箱类仅通知的批准
提示可能无法显示，来源无轮次/顺序元数据时只能尽力处理迟到事件。

每 Session 独立计时。starting/thinking/working 与两种等待状态持续计时；终态、关闭、
过期或离线冻结。Stop 后可信工具事件可以恢复同轮；新请求开始新轮。Host 拒绝当前记录
之前的 monotonic 时间戳、已知旧 run、重复 event ID 和重复同 run 请求。没有原生 run ID
时，不保证辨别跨轮迟到 Stop。未知 Session 的工具/结束事件不能重新创建被清理的记录。

## 4. 容量、投影与恢复

主屏固定显示最近收到有效 `UserPromptSubmit` 的 Session，按 Host 接收顺序记录提交序号。
工具事件、等待批准、Stop、resume 或新建 idle Session 都不抢占主屏；重复/拒收请求不改变焦点。
主项移除后回到剩余 Session 中最近提交的一项；尚无提交记录时选最近创建的一项。
主项始终占投影第 0 项，另外最多 3 项按 waiting_approval → waiting_user → failed → 活动 →
idle → completed/cancelled 排序。同级按进入优先级时间、脱敏 ID 稳定排序。
隐藏待处理计数包含投影外的批准、输入和失败项；它们不会挤掉主项。

- completed/cancelled、明确关闭：60 秒 TTL，重复 Stop 不延长 TTL。
- idle：10 分钟 TTL；failed 和两种等待状态受保护，不自动当作成功或回收。
- 活动状态 120 秒无事件：标记过期、冻结，保留原主状态，不猜测失败/完成。
- Host 满 32 项先淘汰已关闭或最旧 completed/cancelled；全为受保护项时拒绝新身份并计数。
- `sessions forget <id|all>` 只清理 Saki 显示记录，不对 coding agent 执行停止/取消。
- event ID、旧 run、删除记录缓存各最多 256 项；删除后只有新开始/新请求能重建身份。
- 脱敏检查点限 64 KiB，0600、原子替换，最多 32 项。恢复均标过期/待确认，保留状态、
  run、修订、提交序号、冻结耗时和剩余终态 TTL；最长待确认恢复窗口 24 小时。损坏文件不部分载入。
  旧 v1 检查点缺少提交序号时按记录创建顺序回退，下一次有效提交建立新顺序。
  去重缓存不跨进程保存，缺失因果元数据的恢复仍是尽力处理。

## 5. 线协议：v1 能力协商与单帧完整集合

设备 hello 声明 `multi-session`。Host 再发送 `mode:"multi-session"` hello，设备必须回显模式，
之后才发送新 `sessions` 消息。旧 status 的单快照语义保持原样。

`sessions` 使用既有 v1 信封 `id/session/seq`，另含：

| 字段 | 契约 |
| --- | --- |
| sessions | 0–4 个完整 Session 快照；第 0 项为默认主屏，其余为侧栏；空数组清理显示 |
| total | Host 总数，0–32，不小于可见数 |
| hidden_attention | 隐藏待处理数，不超过 total 减可见数 |
| capacity_rejected | uint32 饱和拒收计数 |
| 每项 source | codex / claude_code；纯旧 IPC 降级投影使用 legacy |
| 每项 task.id | 来源模式为 32 位小写十六进制，不允许重复 |
| 每项 run_id | 1–32 位 ASCII 字母、数字、下划线或连字符 |
| 每项 revision | uint32 修订，Host 管理；设备顺序权威为整组 seq |
| 每项 fresh | false 冻结该项耗时；不改变主状态 |
| 每项其余字段 | 复用 state/task/activity/progress/elapsed_ms/agent；省略可选项清空 |

每次消息替换整个列表；没有 begin/item/commit 分帧事务或增量插入，因此无缺项候选与单项
旧修订合并。Host 在序号分配前冻结本次投影；并发新事件进入下一次提交，不能修改在途帧。

UTF-8 NDJSON 帧最多 2,048 B（不含 LF），回复最多 1,024 B；最多 16 层 JSON，禁止 NUL、
尾随非空白和非法 UTF-8。超预算时 Host 按 UTF-8 边界压缩显示文本，保留身份、状态及计数；
四份旧 status 最大文本不能直接拼接。典型混合 4 项为 1,382 B，极限转义测试保持上限。

解析在 peer 候选缓冲完成，通过验证和全局 transport 仲裁后一次入 UI 队列，才更新 seq。
半帧、非法项、重复身份、越界、低优先级 busy、旧序号或入队失败均不能覆盖当前列表。
ACK 的 `applied:true` 表示新提交；完全相同字节/序号/请求 ID 重试可返回
`applied:false,committed:true,last_seq:<原序号>`。普通旧消息或冲突重试为 committed false。
Host 只接受对应序号的明确确认；重试保留原 ID/seq，最多重发两次。

USB/BLE 共用 Host codec、监督锁和设备全局 seq，USB 优先及 2 秒 grace 沿用 0.3。
新链路提交前不关闭健康旧链路。同一 Host session 一旦提交多 Session 集合，不能用另一次
legacy hello/status/clear 绕过写入隔离；换 Host session 后可重新选择兼容模式。

| Host / 固件 | 行为 |
| --- | --- |
| 0.3 / 0.3 | 原有单状态协议 |
| 0.3 / 0.4 | 原有 hello/status/clear，单任务页面 |
| 0.4 / 0.3 | 不发送 sessions；最近提交的单焦点，空集合 idle |
| 0.4 / 0.4 | 能力与模式确认后提交完整列表 |

## 6. UI

默认直接展示最新提交 Session 的完整状态、来源、标题、摘要、耗时和进度。多于一项时，
主区宽 240 px，右侧 80 px 显示最多三个其他 Session；每项用状态色条、产品简称、脱敏短 ID
及简短状态文字表示，不再显示整屏列表。侧栏 Claude 是 Claude Code 的显示简称。
只有一项或空集合时恢复全宽；空集合显示“暂无会话”。底栏显示可见/总数、隐藏 `!N` 和 `FULL`。
侧栏等待为琥珀色、失败红色、完成绿色；过期/待确认项短 ID 后加 `*`，主屏显示“过期”。
断线保留完整集合并冻结所有计时，离线色优先。

点击侧栏临时查看该项，原主项进入侧栏；点击顶栏或 15 秒无交互返回最新提交项。
临时查看按 display ID 保持，普通更新不延后 deadline；被移除或出现新主项/主项新 run 时立即
返回默认主屏。活动卡片仍可切换摘要/详情。暗屏首触仅唤醒，BLE/K2 模态层优先。
新布局的中英文边界、触摸和叠加层仍需真机验收；此前列表布局的协议测试不能代替此项。

## 7. 验收与发布门槛

`scripts/check.zsh`、dev/release/Unity 构建必须通过。真实 2 Codex + 2 Claude Code、四种
版本组合、真机 0/1/2/4 项中英文、触摸、暗屏、断线和 K2 覆盖层仍须联调。
USB→BLE / BLE→USB 各至少 10 次；两链路各 4 项 1 Hz 至少 10 分钟，记录 ACK 延迟、
切换、重试、丢弃与栈/heap。目标 USB 最新快照到 ACK P95 <1.5 秒、BLE <3 秒；完成 hello
至整组确认 USB <3 秒、BLE <6 秒。全量即一帧，但物理重连耗时仍须分开记录。
release 内部 heap 最低 ≥32 KiB，Saki 栈余量 ≥1 KiB，app 分区余量 ≥20%。构建静态容量
不能替代运行时最低值；不满足则修复后复测。0.3 未关闭 transport 验收继续作为发布前置。

真机烧录/Unity 执行需要用户明确允许；测试后恢复正常固件和 Host。24 小时/10,000 次
soak、Mac 睡眠唤醒、LCD 光学延迟为可选工程项，未执行不能宣称通过。
