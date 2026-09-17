# Saki 0.5.0 可扩展 Agent 接入与视觉体验规格

> 状态：规划完成，尚未实施；更新：2026-09-17。
> [工程实施计划](./IMPLEMENTATION.md) · [任务与验证计划](./TASKS.md) ·
> [用户指南草案](./USER_GUIDE.md)

## 1. 目标与基线

0.5.0 以已发布的 0.4.5 为基线，完成两条有顺序依赖的工作流：

1. 将 Codex / Claude Code 的来源特判收敛为显式 adapter 注册表，使 Agent 原生 hook 只在
   adapter 内出现，公共 Host 从 `SourceEvent` 开始只处理统一语义事件。
2. 在线协议和设备端建立通用来源标签能力，再基于同一语义状态完善图片、动效和可关闭的
   轻量趣味文案，避免视觉层继续加入产品名称特判。

本版不以“增加第三个 Agent”作为完成条件。扩展机制完成后，Codex 与 Claude Code 的现有
状态、身份、安装、恢复和显示行为必须保持兼容；第三方来源仍需单独规格、官方 hook 契约核对、
安全测试和发布声明，不能仅因存在注册表就宣称受支持。

## 2. 分层与归一化契约

```text
Codex / Claude Code 原生 hooks
  → AdapterSpec：事件白名单、字段提取、分类、安装元数据
  → SourceEvent：与产品无关的 11 种 EventKind
  → SessionRegistry：来源隔离、产品无关的状态机
  → display projection / protocol session
  → USB 或 BLE → firmware → UI
```

`SourceEvent.snapshot()` 继续是 `EventKind` 到 `AgentState`、通用摘要、进度和计时语义的唯一
映射点。设备不接收原生事件名，不理解 Codex `turn_id`、Claude Code `StopFailure` 或任意
来源 payload。以下边界在 0.5 保持不变：

- `SessionRegistry` 可以保存 source 并拒绝跨 source 串写，但不得按具体产品分支；准确语义为
  source-isolated、product-agnostic。
- 原生 Session / run / event 身份只在 adapter 内提取，随后以 source 作为 HMAC 命名空间生成
  32 位小写十六进制假名；不同来源的相同原生 ID 不能冲突。
- 原始 hook 对象、完整 prompt、工具参数/结果、transcript 和模型回复不进入 `SourceEvent`、
  IPC、检查点或设备帧。短目标继续沿用 0.4.5 的规则过滤契约及其已声明局限。
- 未注册 source、未知事件、非法身份、父/子会话事件和越界输入继续 fail closed；观察 hook
  正常错误路径必须静默、快速且不阻塞 Agent。

## 3. AdapterSpec 注册表

每个 `SourceKind` 必须且只能对应一个 `AdapterSpec`。注册表在导入或首次使用时检查缺项、
重复 source、重复 wire key、空事件列表和无效配置；不做运行时目录扫描或动态代码加载。

`AdapterSpec` 至少声明：

| 字段 | 契约 |
| --- | --- |
| source | 稳定的内部枚举；枚举成员同时声明 wire key 和最多 32 UTF-8 B 的用户可见名称 |
| default_settings_path | 该来源默认用户级配置路径；CLI 不再猜测二选一 |
| hook_events | 安装器实际注册的原生事件白名单 |
| event_kinds | 原生事件名到默认 `EventKind` 的来源内映射 |
| extract | 提取原生 session/run/event/tool/parent/goal 候选的纯函数 |
| classify | 只依据结构化白名单字段修正或拒绝事件的纯函数 |
| tool_kinds | 来源特有工具名到通用 `ActivityKind` 的覆盖表 |
| input_tools | 可可靠表示等待用户输入的工具名集合 |
| config_family | 选择受支持的 hook 配置写入策略 |

公共工具名可保留一份只读基础表，来源 spec 只声明覆盖和增量；来源之间事件名相同不表示
语义必然相同，公共归一化不得先套一张全局 `_EVENTS` 再让所有来源被动覆盖。

0.5 只要求支持现有 JSON command-hook 配置族。若未来 Agent 的配置格式、作用域或 hook
调用协议不同，应新增显式 installer strategy，而不是把其配置伪装成 Codex / Claude Code。
新增来源的核心运行时代码目标是“一个 `SourceKind` 成员 + 一个 spec 模块”；相称测试、文档、
真实客户端验证和可能的新 installer strategy 仍是必需工作。

## 4. 现有来源行为兼容

迁移到注册表不能改变以下已经发布的行为：

- Codex：`thread_id` 可作为 session 回退，`turn_id` 用于 run 隔离，`Interrupt` 为取消，
  `Stop.stop_reason` 只接受结构化 `failed` / `cancelled` 值。
- Claude Code：`PostToolUseFailure` 为可继续的工具错误，`StopFailure` 为整轮失败，迟到
  `Notification` 忽略；没有可靠 run/cancel 信号时不猜测。
- 两来源的交互提问工具进入 `waiting_user`，批准事件进入 `waiting_approval`；工具失败不能
  自动终结整轮。
- hook 安装继续按来源生成 `hook-<source>.sh`，保留其他 hooks、permissions、env 和私有
  备份；用户级与显式项目级作用域仍推荐二选一。

历史 `host/src/saki_host/codex_hooks.py` 不在生产路径中，并含有与当前结构化分类不同的旧
自由文本推断。0.5 删除该模块和专属遗留测试；仍有效的状态、脱敏和空白规范测试迁入当前
adapter / task goal 测试，不恢复基于错误文本猜测额度或终态的行为。

## 5. 通用来源线协议

设备通过 hello 新增 `generic-source` capability。只有设备声明该能力且 multi-session 握手
成功后，Host 才能发送 `codex`、`claude_code`、`legacy` 以外的 source。

支持该能力时，每项来源契约为：

- `source` 是 1–32 位 ASCII 小写标识，匹配 `^[a-z][a-z0-9_]{0,31}$`；`legacy` 保留给旧
  无身份 IPC 和兼容降级，不能注册为新 adapter。
- 非 `legacy` 项的 `task.id` 仍必须是 32 位小写十六进制 HMAC 假名。
- 非 `legacy` 项必须携带非空 `agent.name`，长度不超过 32 UTF-8 B；设备使用该字段显示名称，
  不再根据 source 白名单重写 Codex / Claude Code。
- 固件对 source 和 label 做有界、UTF-8/ASCII 安全解析；未知但合法的来源与已知来源使用同一
  状态模型、计时、排序和触摸逻辑。
- 单帧仍最多 2,048 B、0–4 项；Host 总容量仍为 32 项。Agent 种类不改变容量上限。

兼容降级：

- 0.5 Host 连接没有 `generic-source` 的 0.4.5 固件时，Codex / Claude Code 沿用原 wire key；
  未来来源投影为 `source:"legacy"`，设备显示通用 `Agent`，但状态、标题、计时和 HMAC task ID
  仍可工作。Host 不得发送未知 source 导致整份 sessions 帧被旧固件拒绝。
- 0.4.5 Host 连接 0.5 固件时会忽略新增 capability，继续发送既有来源，行为不变。
- 0.5 Host 与 0.5 固件同时使用时，发送真实 source 和 `agent.name`。

Schema、fixtures、Host 编码、固件解析、两端测试和兼容说明必须作为同一个协议变更更新；
不能只放宽 Python 而继续让固件拒绝整帧。

## 6. 通用设备标签与视觉语言

UI 不再对 `Claude Code` 或其他产品名称写特判。主屏和侧栏统一使用 `agent.name`，按可用宽度
做 UTF-8 安全省略；空值或无 `generic-source` 的兼容项显示 `Agent`。

底栏的隐藏待处理计数改为条件显示，不再固定输出 `!N`：

- `hidden_attention == 0` 时完全省略提示，例如只显示 `最新提交 2/2`；
- `hidden_attention > 0` 时显示明确文字 `待处理 N`，例如 `最新提交 2/5 · 待处理 3`；
- `capacity_rejected` 的容量告警保持独立条件，不得与待处理数共用 `!` 或让用户误认为错误数；
- 该变化只影响固件展示，不改变 `hidden_attention` 的协议字段、Host 计算或注意优先级。

底栏格式必须由有界 helper 统一生成，覆盖默认主项、临时查看、待处理和容量告警的组合，
不能在不同页面各自拼字符串。

视觉资源由设备根据既有九种 `AgentState`、`activity.kind`、fresh/connected 和多 Session
注意状态映射，不由 Host 发送角色素材或品牌资源：

- 九种状态和常用活动类型必须有一致的图片/图标映射；未知类型始终有中性 fallback。
- 图片只强化识别，不替代文字状态、真实摘要、进度、等待批准或断线信息；不能仅靠颜色区分。
- 第一版只维护一套视觉语言，优先使用适合 320×240 的静态小图或轻量帧动画。
- 资源缺失、加载失败或内存不足时回退现有文字/矢量界面，不得白屏、崩溃或丢失操作提示。
- 总览、详情、离线、BLE 配对、K2 提示和等待页面保持同一层级与触摸语义。

素材须有可复现转换脚本、来源哈希和许可证清单；生成的 LVGL 资源不手工编辑。0.5 release
app 按构建产物核对至少 20% app 分区余量；PSRAM、内部 RAM、任务栈和解码峰值属于运行时
真机证据，统一在 1.0 候选上验证，不作为 0.5 发布后悬挂的验收项。

## 7. 趣味文案

趣味文案仅在设备端按语义状态选择，Host 和协议不发送角色台词：

- 提供“关闭 / 轻量”本地设置并持久化；默认值在 0.5 规格评审时冻结。
- 同一 task ID、run ID、state、activity kind 组合固定一条文案，只在真实状态或焦点变化时
  重新选择，避免每帧随机和终态动画重复。
- `waiting_approval`、`waiting_user`、`failed`、断线和配对场景中，核心状态与操作信息始终
  优先；趣味文字只能是无歧义副标题。
- 文案短、无攻击性、无用户数据，必须检查 CJK 字体覆盖和中英文边界。
- 关闭后界面仍完整，设置不得改变 ACK、状态时序、SessionRegistry 或 transport 行为。

## 8. 非目标

- 不在 0.5 接入或宣称支持第三个 Agent，不提供用户安装任意 Python adapter 的插件系统。
- 不动态加载远程代码、素材、主题或文案，不加入账号、遥测、内容推荐或主题商店。
- 不查看对话、工具输出或 transcript，不由模型或云端生成图片、标题或趣味文案。
- 不修改 Host 32 项、设备 4 项、最新提交主屏、注意优先级、TTL 或来源身份隔离语义。
- 不加入 Wi-Fi、OTA、手机端、浏览器端、远程批准或设备远程管理。

## 9. 完成定义

0.5.0 完成必须同时满足：

1. 公共 Host 路径不存在 Codex/Claude 二选一分支；注册表完整性和现有来源行为回归通过。
2. 新旧 Host/固件三种兼容组合通过，未知合法来源不能让旧固件拒绝整组已知 Session。
3. 设备通用显示名称无产品特判，底栏仅在非零时显示明确的 `待处理 N`，1/2/4 Session 的
   布局、触摸和覆盖层策略有离线契约覆盖。
4. 九种状态有图片或明确 fallback；文案关闭/轻量模式不改变协议和核心状态语义。
5. `scripts/check.zsh`、dev、release、Unity 构建通过；release app 构建尺寸保留目标分区余量。
6. 素材许可证、来源哈希、生成方式、兼容限制和受测客户端版本有可复核摘要。

完整真机显示、交互、USB/BLE 恢复和运行时资源矩阵不属于 0.5 发布门槛，统一由
[路线图的 1.0 发布前真机门槛](../../ROADMAP.md#10-发布前真机门槛)关闭。
