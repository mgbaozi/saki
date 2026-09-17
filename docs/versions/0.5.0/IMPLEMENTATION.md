# Saki 0.5.0 工程实施计划

> 状态：规划完成，尚未实施；更新：2026-09-17。
> [SPEC](./SPEC.md) · [TASKS](./TASKS.md) · [USER_GUIDE](./USER_GUIDE.md)

## 1. 实施原则

0.5 分为“来源基础设施”和“设备视觉”两条工作流，严格按前者先行。先消除 Host、协议和 UI
中的产品特判，再在通用 `AgentState` / `ActivityKind` 上建立图片与文案；不得先新增更多
Codex/Claude 分支后再声称完成抽象。

保持现有 `SourceEvent`、IPC v2、SessionRegistry、检查点 v1 和 transport 仲裁的外部行为。
注册表是编译期显式白名单，不扫描目录、不导入用户文件、不暴露第三方插件执行入口。

## 2. Host adapter 结构

在 `host/src/saki_host/adapters/base.py` 增加不可变结构：

```python
@dataclass(frozen=True, slots=True)
class NativeHookFields:
    session_id: str
    run_id: str = ""
    event_id: str = ""
    tool_name: str = ""
    parent_id: str = ""
    task_title: str = ""
    prompt: str = ""

@dataclass(frozen=True, slots=True)
class AdapterSpec:
    source: SourceKind
    default_settings_path: tuple[str, ...]
    hook_events: tuple[str, ...]
    event_kinds: Mapping[str, EventKind]
    extract: Callable[[dict], NativeHookFields]
    classify: Callable[[str, dict, EventKind | None], EventKind | None]
    tool_kinds: Mapping[str, ActivityKind]
    input_tools: frozenset[str]
    config_family: HookConfigFamily
```

最终字段名可按类型检查调整，但必须保持以下职责：adapter 负责原生字段和事件，公共函数负责
边界校验、HMAC、目标过滤与 `SourceEvent` 构造。`SourceKind` 只保留稳定 key 及有界显示标签，
不把完整事件表和 installer 行为塞入枚举，避免形成新的集中式条件对象。

`codex.py` 与 `claude_code.py` 各自导出一个 `SPEC`。`adapters/__init__.py` 建立不可变
`ADAPTERS` 映射并验证：

- `set(ADAPTERS) == set(SourceKind)`；
- key、`spec.source` 和 wire value 一致且唯一；
- source label、事件表、默认路径和 config family 有界有效；
- `legacy` 不允许成为 `SourceKind`；
- 注册失败不回退到其他 adapter。

`normalize_hook()` 的顺序固定为：查 spec → 检查 payload object → 来源内 classify → 提取字段
→ 拒绝父事件/缺身份 → 工具与输入分类 → HMAC → 过滤目标 → 构造 SourceEvent。分类和提取
只接收原始只读 payload，不将其存储或交给 SessionRegistry。

## 3. hook 安装与 CLI

`hooks_config.py` 删除全局事件表和 Claude/Codex 二选一，改为接收 `AdapterSpec` 或由 source
查表。现有 JSON command-hook installer 继续负责：

- 按 `spec.hook_events` 安装精确事件集合；
- 生成 `hook-<source>.sh` 和 `saki-observer:<source>` marker；
- 幂等 install/check/uninstall，保留非 Saki 配置；
- 私有 wrapper、identity 和配置备份；
- 路径空格、并发修改、损坏 JSON 和权限错误的安全失败。

CLI 的 `--source` choices 由注册表生成，默认 settings 路径来自 spec。显式 `--settings` 仍覆盖
默认路径。`config_family` 为未来不同安装格式保留分发点；0.5 不实现没有实际来源契约的新格式。

## 4. SessionRegistry 与迁移

SessionRegistry 不做结构重写。继续以 HMAC session ID 为 key，并保留
`record.event.source != event.source` 的防串写检查。由于 SourceEvent 字段和 checkpoint v1
不变，0.4.5 检查点应原样恢复；新枚举只影响新版本能够解释的来源值。

测试中所有“恰好两个来源”的假设改为按 registry 参数化：避免 `index % 2`、三元 label 和
`next(other_source)` 在第三个枚举出现后产生错误。容量、主项、侧栏、TTL、stale 和排序测试
继续覆盖来源数量与 Session 数量相互独立。

## 5. 通用来源协议

协议变更作为一个原子工作包实施：

1. `messages.schema.json` 允许设备 hello 声明 `generic-source`；协议头文件增加 capability flag。
2. `sessions.schema.json` 将 source 从固定枚举改为有界 ASCII pattern，并以 `legacy` / 非 legacy
   条件约束 task ID 和 `agent.name`。
3. Host 在连接握手后保存 capability。构造 projection 时：支持则输出真实 source；不支持且
   source 不属于 0.4.5 白名单时输出 `legacy`，不改变内存记录和 HMAC task ID。
4. 固件 parser 对 source 做长度、字符和保留值检查；generic 模式使用已经解析的
   `agent.name`，旧模式继续只接受 0.4.5 白名单。
5. ACK、session/seq、完整集合、重复 task ID、run/revision 和 2,048 B 帧限制保持不变。

不能只通过 schema 放宽来源；原生 C 契约、Unity、Python schema/encoder 和兼容组合必须在同一
变更中通过。若握手 capability 或旧固件降级无法可靠实现，协议工作包不合并。

## 6. 通用 UI 标签

移除 `saki_ui.c` 对 `Claude Code` 的专用字符串分支。增加统一的来源标签布局策略：

- 主屏允许完整有界 label；侧栏按固定像素宽度做 UTF-8 安全省略，而不是按产品名替换；
- 空 label、legacy 和解析失败候选使用 `Agent`；
- label 更新不改变 task/run 选择，不触发无关终态动画；
- 中英文、ASCII 32 B 边界和四项侧栏先由 UI policy、原生测试和构建覆盖；真机统一在 1.0
  候选矩阵检查。

把底栏字符串从 `saki_render_sidebar()` 内联格式化移入有界纯 helper。helper 接收查看模式、
可见数、总数、`hidden_attention` 和 `capacity_rejected`，输出规则为：零待处理不追加字段，
非零追加 `待处理 N`，容量告警独立追加。单元测试固定以下组合：

- 主项 `2/2/0` → `最新提交 2/2`；
- 主项 `2/5/3` → `最新提交 2/5 · 待处理 3`；
- 临时查看与非零待处理；
- 非零容量告警单独出现及与待处理同时出现；
- 最大 32 项和输出缓冲边界。

协议及 Host 的 `hidden_attention` 计算不变；本工作只改善设备表达。

## 7. 图片资源与页面

实施前先记录当前 dev/release 的 app 大小和剩余分区，冻结单套资源格式与静态预算。启动 heap、
稳定 internal heap、PSRAM、UI task stack 和典型帧刷新属于 1.0 候选的集中真机基线；0.5 不以
构建成功声称这些运行时指标已经验证。

新增资源应通过脚本从有来源记录的原始素材生成 LVGL C 资源；脚本记录工具版本、输入哈希、
输出参数和许可证。生成文件加显式说明，不手工修改。映射层只接受 `AgentState`、
`ActivityKind` 和通用 attention/freshness，不接受 Codex/Claude source。

页面实施顺序：

1. 建立九状态静态 fallback 与资源缺失回退；
2. 替换主屏主要纯色占位，保持文字和进度层；
3. 更新 1/2/4 Session 侧栏、选中态和注意提示；
4. 加入有界转场，避免普通计时更新重复播放；
5. 回归断线、BLE 配对、K2、暗屏唤醒和详情触摸覆盖层。

低内存和资源失败路径必须回到可读界面。不得在运行中下载、解码无界图片或从 Host 接收素材。

## 8. 趣味文案设置

文案库在固件内按语义 key 组织。选择使用稳定、可复现的本地 hash，例如
`task_id + run_id + state + activity_kind`，状态不变时不得重新抽取。设备设置只保存模式枚举，
不保存任务文本或身份。

设置入口优先复用现有按键/触摸能力；具体手势须在实现前写入任务记录并排除与 K2 配对、亮度、
详情切换的冲突。若 320×240 无法提供清晰设置入口，本版允许只提供编译期默认加持久化 API，
但不能宣称用户可配置完成。

## 9. 历史清理

删除不在生产路径的 `host/src/saki_host/codex_hooks.py` 与 `host/tests/test_codex_hooks.py`。
删除前逐项分类旧测试：

- 状态映射和工具分类转入当前 adapter 测试；
- 空白实体规范转入 `task_goal` / privacy 测试；
- 基于自由文本推测额度、失败或取消的旧行为不迁移；
- 历史版本文档保留事实引用，不改写已发布记录。

## 10. 验证策略

离线验证：

- adapter registry 完整性、来源内同名事件不同语义、字段提取、工具覆盖和未知来源拒绝；
- 相同原生 ID 的跨来源 HMAC 隔离，IPC/checkpoint/device 无原始敏感字段；
- 每来源 hook install/check/uninstall 往返、其他设置保留和默认路径选择；
- 0.4.5 checkpoint 恢复、32 项容量和任意来源数量下的 4 项投影；
- generic-source schema、最大 frame、非法 source/label、旧固件 legacy 降级；
- 固件 parser、UI policy、资源缺失、稳定文案选择和设置持久化。

构建与后续真机：

- 每个实施里程碑运行 `scripts/check.zsh`；
- 协议或固件变化完成 dev、release、Unity 构建；
- 0.5 发布不以真机矩阵为门槛；新旧 Host/固件组合的协议行为使用 Python、原生 C 和 Unity
  构建验证，不把构建写成硬件通过；
- USB/BLE、1/2/4 Session、触摸、覆盖层、中英文 label、运行时内存和资源 fallback 统一进入
  1.0 候选真机矩阵；届时须经用户明确允许，Unity 后刷回正常固件并恢复 Host 服务。

## 11. 工作包

| 顺序 | 工作包 | 完成条件 |
| --- | --- | --- |
| M1 | AdapterSpec、注册表、现有来源迁移 | 公共 normalize 无产品分支，Codex/Claude 行为不变 |
| M2 | hooks_config 与 CLI 参数化 | 事件、路径和 installer 从 spec 取得，安装往返通过 |
| M3 | Session/隐私回归与历史清理 | checkpoint 兼容，删除旧 codex_hooks，无测试覆盖倒退 |
| M4 | generic-source 协议与旧固件降级 | schema、Host、固件、fixtures、兼容矩阵同时通过 |
| M5 | 通用 UI label | 无产品字符串特判，边界和 fallback 通过 |
| M6 | 图片、页面与资源预算 | 九状态映射、资源回退、离线 UI 契约和分区余量通过 |
| M7 | 趣味文案与本地设置 | 稳定选择、可关闭、不影响关键状态和传输 |
| M8 | 0.5 发布记录与文档 | 全部自动化/构建、许可证、生成方式和兼容摘要完成 |

M4 是 M5 的前置，M5 是 M6 的前置。M1–M3 可在不改固件时独立回归，但不能仅完成 Host
重构就宣称“新增 Agent 不需要设备修改”。
