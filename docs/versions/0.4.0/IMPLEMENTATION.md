# Saki 0.4.0 工程实施设计

> 状态：0.4.0-dev 实现中；行为见 [SPEC](./SPEC.md)，证据见 [TASKS](./TASKS.md)。

## 1. 数据流

```text
Codex / Claude Code hooks
  → 明确来源、白名单分类、身份 HMAC
  → Unix datagram v2 SourceEvent
  → SessionRegistry（32 项）与脱敏检查点
  → 最新提交主项 + 最多 3 个侧栏项 / 旧设备同一主项
  → 共用 codec + ProtocolSession + USB/BLE supervisor
  → peer 候选解析 → transport 仲裁 → 完整 UI 队列 → 主屏 + 侧栏
```

`host/src/saki_host/adapters/` 分离 Codex、Claude Code 分类，公共 `SourceEvent` 包含固定枚举、
HMAC 标识、本地时间及可选的脱敏短目标 goal。`identity.py` 用安全文件描述符读取本机 32 B 密钥，拒绝符号链接、
错误所有者和过宽权限。默认状态目录是 `~/Library/Application Support/Saki`。

`cli hook` 捕获输入/依赖/IPC 错误，正常观察路径不输出内容或非零退出。`hooks_config.py`
幂等合并 settings，保留 permissions、env 和其他 hooks；替换前检查并发修改。备份为原配置
的私有副本，因此可能包含用户既有凭据，只保留在原配置目录，不打印、不进入仓库。
wrapper 使用安装时绝对解释器路径，shell quoting 支持空格目录，不依赖当前工作目录。

旧项目 wrapper `scripts/saki-hook.zsh` 仍可用；用户级与项目级安装二选一，避免双重触发。
安装器管理带 `saki-observer:<source>` 标记的条目，不删除其他客户端 hook。

## 2. SessionRegistry 与持久化

`SessionRegistry` 拥有独立快照、run、revision、时间线、状态优先级进入时间、提交序号、TTL 和 freshness。
来源事件先校验、去重，再原子更新一个记录。内存最多 32 项，辅助去重缓存各 256 项。
每次投影均包含所有可见项，代替草案的单项 dirty round-robin；因此高频来源不会只占一个
单项发送槽。只有通过校验的 PROMPT 递增 submitted_order，visible()[0] 为最大提交序号项；
其余项按注意优先级取三项。focus() 与新固件使用相同主项；普通状态更新不抢焦点。

SourceEvent 时间戳在 hook handler 启动时生成，能拒绝较早启动后迟到的本机进程；不等价于
来源因果顺序。Codex 有 turn_id 时隔离旧 run，Claude 的本地 run 不宣称解决跨轮迟到 Stop。
`task_goal.py` 只在 PROMPT 处理 task_title，结果为空时处理 prompt。输入在 hook 内存中读取；
函数依次删除匹配的成对标签块和三反引号代码块、逐行跳过空行/特定前缀、剥去 Markdown
链接目标并保留标签、替换匹配的网址、丢弃命中凭据正则的行、替换匹配的绝对路径，再进行
通用 redact_text、控制字符/空白清理和简短控制请求过滤，最后截取首句并限制为 48 字符/96 B。
这些是词法规则：不会概括语义，也不保证识别所有代码、上下文、路径或秘密。相对路径、普通
文件名和未命中规则的用户文字可原样保留；短 prompt 可完整成为 goal。

只在 PROMPT 接受新目标，其余事件与已识别的简短“继续”保留记录原目标。SourceEvent 的
构造/IPC 解码会再次调用同一函数检查目标的规范形式；这不是独立的敏感信息检测。
未知字段仍拒收，旧事件缺 goal 按空值兼容。goal 通过本地 IPC 进入 registry 和 0600 检查点，
再用现有 task.title 发送到设备；HMAC 只用于身份，不对标题做匿名化。原始 hook 对象和
独立的完整 prompt 字段不落盘或转发，但这不意味着派生标题中不存在 prompt 原文。
常规服务日志不输出 goal；显式 hook --stdout 的诊断事件包含它。线协议和固件无需改变。
resume 只更新观测信息，不覆盖原显示状态；检查点单独保存 display_kind、run、revision 和 submitted_order；恢复后从最大序号继续。
旧检查点默认序号 0，按记录创建顺序选择回退主项。

服务 maintenance 每秒检查 TTL/stale，有新代次时将不可变脱敏检查点交给线程原子写盘。
新 hook 不等刷盘。恢复先在临时字典完成全部校验，再替换仓库；无有效文件时正常空启动。
恢复项保持冻结直到新可信活动确认；异常仅输出固定错误文案，磁盘失败仍保留内存状态。
`sessions list` 读取最近检查点；`sessions forget` 经原有 0600 socket 请求清理，不调用 Agent。

## 3. 线协议决策

采用能力协商的 **v1 sessions 单帧完整集合**，复用 2,048 B 上限、session、seq、ACK 与重试。
未采用多帧 begin/item/commit，也不加入增量修订合并。代价是每次携带最多四项，文本需按
预算缩短；收益是重连只需一个原子提交，无缺项候选、事务超时和增量复活语义。

`display.py` 冻结投影，迭代压缩 UTF-8 显示文本，绝不缩短身份/来源/状态。典型 2+2 帧为
1,382 B；最大整数、汉字、反斜杠、引号与控制字符转义组合由测试验证编码上限。
Schema 在 `protocol/schema/v1/sessions.schema.json`，共享脱敏样本为
`protocol/fixtures/v1/valid/sessions-mixed.json`。

`ProtocolSession.enable_multi_session` 二次 hello 并验证 mode 回显；旧设备只走 apply_status。
所有连接共享 codec、监督锁，候选提交期间新事件进入 mailbox。每次提交后合并 250 ms，
持续输入仍按独立 heartbeat deadline 发 ping。ACK 只确认冻结的 mailbox generation；提交期间
到达的事件留到下一次完整投影。断线或提交失败不将该代次标已发送。

## 4. 固件缓冲和提交

`saki_display_snapshot_t` 固定 4 项、run/revision、总数与连接元数据；`stale` 影响独立计时。
每个 USB/BLE protocol engine 拥有解析候选及上一成功帧的精确字节，重试比较不用短哈希。
主程序将两个 engine 与最后完整显示记录分配到必需的 PSRAM；这些缓冲只在任务中访问，
不用于 DMA/ISR，避免侵占 BLE/UI/JSON 临时分配所需的内部 RAM。分配失败即停止启动。
候选不直接引用 active/UI 内存。解析深度 16，拒绝 NUL 和尾随内容后才分配 cJSON 树。

transport manager 统一检查链路优先级、session/seq 和模式隔离。UI 队列按值复制完整集合，
只有入队成功才推进 manager 状态。main 保留最后完整显示用于离线冻结；UI 消费自己的
副本，旧候选不会修改已显示内容。大集合避免任务栈复制。Unity 旧用例复用静态 parser
实例，防止每条测试独占增加后的大缓冲。

UI 复用原摘要/详情、计时、进度、BLE/K2 与亮度层。多项时主区 240 px、侧栏 80 px，
三项各占 62 px 高；中文 fallback 行高 20 px，来源/短 ID/状态三行各留足高度。
只有一项时回到全宽，侧栏按新集合或本地切换更新。侧栏显示集合中除当前主屏以外的项。
`saki_session_view_t` 在独立 UI policy 中维护最新主项 ID/run、临时选择 ID 和 15 秒 deadline。
普通更新和侧栏排序变化保留选择；主项 ID/run 变化、超时或选中项移除都返回最新主项。
同一选择契约编入原生 ASan/UBSan 回归和 Unity。新布局的中英文边界、触摸和叠加层仍待真机。

## 5. 来源依据与实测范围

官方契约参考：[Codex hooks](https://learn.chatgpt.com/docs/hooks)、
[Claude Code hooks](https://code.claude.com/docs/en/hooks)。
Claude Code 2.1.270 已用真实 API 完成隔离 probe，验证 SessionStart、UserPromptSubmit、
PreToolUse、PostToolUse、Stop、SessionEnd，共 6 个安全语义事件、1 个 Session。
未读取 transcript，未保存原始 hook/模型输出。取消、批准和失败映射尚未全量真实覆盖；
真实四会话混合与 Codex 客户端端到端验收留在 TASKS，不由合成用例替代。

`scripts/test-claude-hooks.py` 是显式付费验证入口，默认普通检查不调用模型。它使用临时目录、
只读一个测试文件、0.25 USD 预算和 90 秒总超时；从本地环境/已配置 env 私下取得认证和
路由，不打印其内容。输出只有结果码、事件类别和计数；测试目录退出即删除。

## 6. 验证入口

- `scripts/check.zsh`：Host/Schema/文档与固件原生契约、Ruff、whitespace。
- 原生契约直接编译实际 model/protocol/transport 与指定 ESP-IDF 的 cJSON，启用 ASan/UBSan。
  同一 C 契约也编入 Unity 固件。缺 clang 或指定 IDF 时该原生测试明确 skip，不冒称通过。
- `scripts/build-firmware-profile.zsh dev` / `release`：两种显示固件构建。
- `scripts/build-firmware-tests.zsh`：Unity 构建；构建不是板上执行。
- 真机继续使用既有发现、烧录、服务管理和 BLE 工具；独占 CDC 前停 Host、之后恢复。

目标板已获烧录授权时，停止常驻 Host 后可运行：
`host/.venv/bin/python scripts/test-device-sessions.py --transport usb --duration 600`
（将 usb 改为 ble 复用已绑定设备）。先跑短测试确认握手，再执行 10 分钟压力验证；结束
恢复 Host。脚本只发送合成事件并输出安全统计，视觉和触摸需另行实机核验。
