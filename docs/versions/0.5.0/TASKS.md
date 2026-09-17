# Saki 0.5.0 实施任务与验证计划

> 状态：规划完成，尚未实施；更新：2026-09-17。
> [SPEC](./SPEC.md) · [IMPLEMENTATION](./IMPLEMENTATION.md) · [USER_GUIDE](./USER_GUIDE.md)

`[x]` 表示已有可复核结果，`[ ]` 表示 0.5 待实施或待完成的离线/构建验证。完整真机矩阵
不在本清单悬挂，统一见 [1.0 发布前真机门槛](../../ROADMAP.md#10-发布前真机门槛)。

## P：规划与基线

- [x] P0：审计当前 SourceEvent、adapter、SessionRegistry、hook 安装、CLI、schema、固件 parser
  和 UI 来源分支，确认 Host 注册表不足以单独实现端到端扩展。
- [x] P1：冻结 0.5 范围：AdapterSpec + generic-source + 通用 UI label + 图片/文案；明确不接入
  第三个 Agent、不做动态插件、Wi-Fi、OTA 或远程批准。
- [x] P2：建立 SPEC、IMPLEMENTATION、TASKS、USER_GUIDE 草案并接入文档索引与路线图。
- [ ] P3：实施前记录 0.4.5 dev/release 固件大小与分区余量；运行时资源基线留到 1.0 候选。
- [ ] P4：冻结图片格式、单项/总资源预算、趣味文案默认值和无冲突的本地设置入口。

## A：AdapterSpec 与归一化

- [ ] A0：定义 `NativeHookFields`、`AdapterSpec`、config family 及有界校验。
- [ ] A1：Codex spec 内聚 event map、thread/session/turn 提取、Interrupt/Stop 分类、工具与输入工具。
- [ ] A2：Claude Code spec 内聚 event map、session 提取、失败/通知分类、工具与输入工具。
- [ ] A3：建立一对一注册表，检查所有 SourceKind 恰有一个 spec，拒绝重复、遗漏和 legacy。
- [ ] A4：重写 `normalize_hook()` 为表驱动流程，删除产品二选一和全局共享事件默认表。
- [ ] A5：验证同名事件可按来源映射为不同语义，未知事件/来源/身份/父事件保持 fail closed。
- [ ] A6：回归 goal 过滤、HMAC source 域、IPC 上限和原始 payload 不持久化契约。

## H：安装、CLI 与历史清理

- [ ] H0：`hooks_config.py` 从 spec 取得事件集合，删除 Claude/Codex 条件表达式。
- [ ] H1：CLI choices、默认 settings 路径和 wrapper 名称从注册表取得；显式路径行为不变。
- [ ] H2：两来源用户级/项目级 install/check/uninstall、幂等、路径空格、损坏配置和并发修改回归。
- [ ] H3：验证未来不同配置族只能通过显式 installer strategy 接入，不默认套用 JSON hook 写法。
- [ ] H4：删除生产未引用的 `codex_hooks.py` 和专属旧测试，将仍有效覆盖迁入当前测试。
- [ ] H5：确认 hook 静默、1 秒 handler deadline、64 KiB 输入和服务离线快速退出不变。

## S：SessionRegistry 与显示投影

- [ ] S0：保持 SourceEvent/checkpoint v1 格式，验证 0.4.5 检查点原样恢复。
- [ ] S1：保留跨 source 串写拒绝，移除测试和公共逻辑中恰好两个来源的假设。
- [ ] S2：按所有注册来源参数化主项、侧栏、TTL、stale、resume、旧 run 和容量测试。
- [ ] S3：验证 Host 32 项、设备 4 项、隐藏注意计数和 2,048 B 上限不随来源数量变化。

## W：generic-source 协议

- [ ] W0：规格化并实现 hello `generic-source` capability，旧 Host 可忽略。
- [ ] W1：更新 sessions schema：有界 source pattern、legacy 保留值、非 legacy HMAC ID 和 agent.name。
- [ ] W2：Host 按连接 capability 输出真实 source 或安全降级 legacy；内存记录不被降级污染。
- [ ] W3：固件 parser 在 generic 模式接受合法未知来源，严格拒绝超长、非法 ASCII、空 label、
  非法 UTF-8、非 HMAC task ID 和重复 ID。
- [ ] W4：旧模式仍拒绝未知 wire source；0.5 Host 不得向旧固件发送这种帧。
- [ ] W5：同步 schema fixtures、Host encoder、原生 C 契约、Unity 和兼容说明。

## U：通用来源 UI

- [ ] U0：移除 Claude Code 专用缩写分支，统一使用 agent.name 和 UTF-8 安全省略。
- [ ] U1：验证 Codex、Claude Code、通用第三来源合成标签和 legacy fallback 的主屏/侧栏布局。
- [ ] U2：底栏 `hidden_attention == 0` 时不显示提示，非零时显示 `待处理 N`，不再输出 `!N`。
- [ ] U3：为主项/临时查看、0/非零待处理、容量告警及最大 32 项增加有界格式化测试。
- [ ] U4：label 和底栏变化不改变 task/run 选择，不重置耗时，不重复播放终态动画。
- [ ] U5：用 UI policy/原生契约覆盖 ASCII/中文/最大 32 B、1/2/4 Session 和通用省略策略。

## V：图片与页面

- [ ] V0：建立素材来源、许可证、输入哈希和可复现转换脚本；生成文件禁止手工修改。
- [ ] V1：为九种 AgentState、常用 ActivityKind 和未知类型提供图片映射与中性 fallback。
- [ ] V2：更新主屏、详情、侧栏、选中态和注意提示，不以图片替代文字、进度或风险信息。
- [ ] V3：资源缺失、加载失败和低内存回退现有可读界面，无白屏、崩溃或半更新。
- [ ] V4：转场只在真实状态/焦点变化触发，普通计时和重复完整快照不重复动画。
- [ ] V5：release app 构建尺寸保留至少 20% 分区余量；运行时资源与连续切换趋势迁入 1.0。

## F：趣味文案与设置

- [ ] F0：建立短、无攻击性、无用户数据且字体覆盖完整的设备端文案库。
- [ ] F1：按 task/run/state/activity 稳定选择，状态不变时不闪烁、不重抽。
- [ ] F2：等待批准、等待输入、失败、断线和配对场景始终优先显示真实状态与操作信息。
- [ ] F3：实现“关闭 / 轻量”持久化设置，不与 K2、亮度、唤醒和详情触摸冲突。
- [ ] F4：关闭后界面完整；切换设置不改变协议、ACK、SessionRegistry 或 transport。

## C：兼容矩阵

| 组合 | 预期 | 状态 |
| --- | --- | --- |
| 0.5 Host → 0.4.5 firmware，Codex/Claude | 沿用已知 source，完整多 Session | 待验证 |
| 0.5 Host → 0.4.5 firmware，未来来源合成事件 | wire 降级 legacy，显示 Agent，不拒绝整帧 | 待验证 |
| 0.4.5 Host → 0.5 firmware | 忽略新 capability，既有行为不变 | 待验证 |
| 0.5 Host → 0.5 firmware | 真实通用 source + agent.name | 待验证 |
| 0.4.5 checkpoint → 0.5 Host | 身份、状态、run、耗时、焦点和 TTL 保留 | 待验证 |

兼容测试使用合成第三来源，不等同于宣布支持该 Agent 产品。真实第三方接入不属于本版。

## R：发布门槛

- [ ] R0：`scripts/check.zsh` 全部通过，包含 adapter、schema、原生 C、隐私和文档检查。
- [ ] R1：dev、release 和 Unity 固件构建通过，版本元数据按发布阶段统一。
- [ ] R2：记录受测 Codex/Claude 版本、离线协议兼容结果、构建尺寸和分区余量。
- [ ] R3：确认 0.5 文档没有把未执行的真机矩阵描述为已通过或当前版本待关闭项。
- [ ] R4：核对所有素材许可、NOTICE、来源哈希和发布包第三方声明。
- [ ] R5：完善用户指南和发布说明；commit/tag/push/GitHub Release 仅按用户明确要求执行。

## 本次规划验证

2026-09-17：已完成架构审计和 0.5 文档规划；`scripts/check.zsh` 通过，173 项测试、Ruff、
原生契约与 Git whitespace 检查通过。尚未修改运行代码、协议、固件或素材，未执行固件构建、
真机和真实 Agent API 验证；该结果只证明规划文档与当前 0.4.5 基线一致，不代表 0.5 功能完成。
真机矩阵已统一迁入 1.0 发布前门槛，不作为 0.5 的未关闭验收。
