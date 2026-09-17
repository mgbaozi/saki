# Saki 0.5.0 实施任务与验证记录

> 状态：已发布；更新：2026-09-17。
> [SPEC](./SPEC.md) · [IMPLEMENTATION](./IMPLEMENTATION.md) · [USER_GUIDE](./USER_GUIDE.md)

`[x]` 表示已有可复核结果，`[ ]` 表示 0.5 待实施或待完成的离线/构建验证。完整真机矩阵
不在本清单悬挂，统一见 [1.0 发布前真机门槛](../../ROADMAP.md#10-发布前真机门槛)。

## P：规划与基线

- [x] P0：审计当前 SourceEvent、adapter、SessionRegistry、hook 安装、CLI、schema、固件 parser
  和 UI 来源分支，确认 Host 注册表不足以单独实现端到端扩展。
- [x] P1：冻结 0.5 范围：AdapterSpec + generic-source + 通用 UI label + 图片/文案；明确不接入
  第三个 Agent、不做动态插件、Wi-Fi、OTA 或远程批准。
- [x] P2：建立 SPEC、IMPLEMENTATION、TASKS、USER_GUIDE 草案并接入文档索引与路线图。
- [x] P3：记录开发起点的 dev/release 固件大小与分区余量；运行时资源基线留到 1.0 候选。
- [x] P4：冻结 48×48 RGB565A8、九张共 62,208 B、默认轻量文案，以及 K1 短按/2–5 秒长按
  的本地设置入口；K2 BLE 手势不变。

## A：AdapterSpec 与归一化

- [x] A0：定义 `NativeHookFields`、`AdapterSpec`、config family 及有界校验。
- [x] A1：Codex spec 内聚 event map、thread/session/turn 提取、Interrupt/Stop 分类、工具与输入工具。
- [x] A2：Claude Code spec 内聚 event map、session 提取、失败/通知分类、工具与输入工具。
- [x] A3：建立一对一注册表，检查所有 SourceKind 恰有一个 spec，拒绝重复、遗漏和 legacy。
- [x] A4：重写 `normalize_hook()` 为表驱动流程，删除产品二选一和全局共享事件默认表。
- [x] A5：验证同名事件可按来源映射为不同语义，未知事件/来源/身份/父事件保持 fail closed。
- [x] A6：回归 goal 过滤、HMAC source 域、IPC 上限和原始 payload 不持久化契约。

## H：安装、CLI 与历史清理

- [x] H0：`hooks_config.py` 从 spec 取得事件集合，删除 Claude/Codex 条件表达式。
- [x] H1：CLI choices、默认 settings 路径和 wrapper 名称从注册表取得；显式路径行为不变。
- [x] H2：两来源用户级/项目级 install/check/uninstall、幂等、路径空格、损坏配置和并发修改回归。
- [x] H3：未来不同配置族只能通过显式 `config_family` 分发接入，不默认套用 JSON hook 写法。
- [x] H4：删除生产未引用的 `codex_hooks.py` 和专属旧测试，将仍有效覆盖迁入当前测试。
- [x] H5：确认 hook 静默、1 秒 handler deadline、64 KiB 输入和服务离线快速退出不变。

## S：SessionRegistry 与显示投影

- [x] S0：保持 SourceEvent/checkpoint v1 格式，验证既有 checkpoint 原样恢复。
- [x] S1：保留跨 source 串写拒绝，移除测试和公共逻辑中恰好两个来源的分支与取模常量。
- [x] S2：按所有注册来源参数化主项、侧栏、TTL、stale、resume、旧 run 和容量测试。
- [x] S3：验证 Host 32 项、设备 4 项、隐藏注意计数和 2,048 B 上限不随来源数量变化。

## W：generic-source 协议

- [x] W0：规格化并实现 hello `generic-source` capability，旧 Host 可忽略。
- [x] W1：更新 sessions schema：有界 source pattern、legacy 保留值、非 legacy HMAC ID 和 agent.name。
- [x] W2：Host 按连接 capability 输出真实 source 或安全降级 legacy；内存记录不被降级污染。
- [x] W3：固件 parser 在 generic 模式接受合法未知来源，严格拒绝超长、非法 ASCII、空 label、
  非法 UTF-8、非 HMAC task ID 和重复 ID。
- [x] W4：旧模式仍拒绝未知 wire source；0.5 Host 不得向旧固件发送这种帧。
- [x] W5：同步 schema fixtures、Host encoder、原生 C 契约、Unity 和兼容说明。

## U：通用来源 UI

- [x] U0：移除 Claude Code 专用缩写分支，统一使用 agent.name 和 UTF-8 安全省略。
- [x] U1：验证 Codex、Claude Code、通用第三来源合成标签和 legacy fallback 的主屏/侧栏布局。
- [x] U2：底栏 `hidden_attention == 0` 时不显示提示，非零时显示 `待处理 N`，不再输出 `!N`。
- [x] U3：为主项/临时查看、0/非零待处理、容量告警及最大 32 项增加有界格式化测试。
- [x] U4：label 和底栏变化不改变 task/run 选择，不重置耗时，不重复播放终态动画。
- [x] U5：用 UI policy/原生契约覆盖 ASCII/中文/最大 32 B、1/2/4 Session 和通用省略策略。
- [x] U6：侧栏以状态和任务标题替代四位 HMAC；运行态过期后保留 15 分钟再退出最近会话，
  迟到事件可恢复，等待用户/批准不自动隐藏。
- [x] U7：紧凑主卡计时在主内容区右侧保留 8 px padding，状态间距 6 px；失败会话使用 10 分钟
  TTL，旧 checkpoint 恢复时迁移，等待用户/批准继续受保护。

## V：图片与页面

- [x] V0：保存 source sheet、生成提示、非官方同人素材声明和输入 SHA-256；转换脚本校验哈希，
  从 alpha 透明沟槽检测非均匀九宫格边界，生成文件明确禁止手工修改。
- [x] V1：九种 AgentState 各有静态图片；0.5 的 ActivityKind 继承状态图片，未知类型使用同一
  状态 fallback；`classic` 提供中性无图片回退。
- [x] V2：主屏状态图不替代文字、进度或风险信息；详情、侧栏、选中态和注意提示继续使用既有
  文字/色标层级。透明图后由 UI 绘制状态色圆角底板和轻微辉光，配色不烘焙进素材包；整体
  保持在 52×52 内，避免 320×240 信息区被角色素材挤占。
- [x] V3：空资源描述符或 `classic` 包自动显示原有状态圆点和完整文字界面；协议和页面对象不
  依赖图片才能工作。低内存运行时趋势留到 1.0 真机矩阵。
- [x] V4：状态图淡入只在 state、焦点 task/run 或素材包变化时触发；普通计时、文案模式切换和
  重复完整快照不重复动画。
- [x] V5：release app 镜像 `0x134bd0`，目标 app 分区剩余 38%；运行时资源与连续切换趋势
  迁入 1.0。

## F：趣味文案与设置

- [x] F0：建立逐状态设备端梗文案库；失败固定显示「全——都不会做～」，其余状态保留轮换；
  不含用户数据，GB2312 之外只追加已审计的 U+2014 全角破折号字形。
- [x] F1：按 pack/task/run/state/activity key 稳定选择，计时与重复快照不重抽。
- [x] F2：等待批准、等待输入和失败时真实 detail 保持第一行；详情页、断线和 BLE 覆盖层不显示
  趣味文案。
- [x] F3：K1 短按切换“关闭 / 轻量”，K1 2–5 秒松开切换素材包，选择写入 NVS；K2 和触摸
  行为不变。
- [x] F4：关闭或切换到 `classic` 后仍显示完整文字、进度和状态圆点；设置不进入协议路径。

## C：兼容矩阵

| 组合 | 预期 | 状态 |
| --- | --- | --- |
| 0.5 Host → 0.4.5 firmware，Codex/Claude | 沿用已知 source，完整多 Session | 离线契约通过 |
| 0.5 Host → 0.4.5 firmware，未来来源合成事件 | wire 降级 legacy，显示 Agent，不拒绝整帧 | 离线契约通过 |
| 0.4.5 Host → 0.5 firmware | 忽略新 capability，既有行为不变 | 离线契约通过 |
| 0.5 Host → 0.5 firmware | 真实通用 source + agent.name | 离线契约通过 |
| 0.4.5 checkpoint → 0.5 Host | 身份、状态、run、耗时、焦点和 TTL 保留 | 离线契约通过 |

兼容测试使用合成第三来源，不等同于宣布支持该 Agent 产品。真实第三方接入不属于本版。

## R：发布门槛

- [x] R0：`scripts/check.zsh` 全部通过，包含 189 项 Host/协议/文档测试与 Ruff。
- [x] R1：dev、release 和 Unity 固件构建通过；dev/release 分别报告 0.5.0-dev/0.5.0。
- [x] R2：记录受测 Codex CLI 0.153.0、Claude Code 2.1.274、离线协议兼容结果、构建尺寸
  和分区余量；版本号来自发布主机本地 CLI，不表示完整真实 API 矩阵已执行。
- [x] R3：确认未执行的完整真机矩阵仅列入 1.0 门槛，没有描述为 0.5 已通过或待关闭项。
- [x] R4：核对高清素材母版、生成提示与 SHA-256、Noto Sans CJK OFL、厂家 BSP 来源、根
  `LICENSE` / `NOTICE` 及发布包第三方声明。
- [x] R5：用户指南和 0.5.0 发布说明完成；用户已明确授权创建并推送 tag、发布 GitHub Release。

## 当前实施验证

2026-09-17：M1–M8 已完成并冻结。hook 配置回归覆盖两来源、显式用户/项目路径、路径空格、损坏配置
和并发修改；来源参数化矩阵补齐后修复了 stale 会话收到恢复事件仍保持过期标记的问题。通用
label 在主屏与侧栏使用独立定宽单行和 LVGL UTF-8 省略，底栏 U2/U3 同步完成。侧栏移除
四位 HMAC，运行态过期会话增加 15 分钟保留后隐藏与迟到事件恢复测试；LCD flush 在通知 LVGL
前等待异步 SPI 传输完成，避免双缓冲过早复用。紧凑主卡计时回到右侧，失败记录增加 10 分钟
TTL、旧 checkpoint 迁移与跨重启剩余时间测试。
`scripts/check.zsh` 通过，189 项测试、Ruff 与 Git whitespace 检查通过；dev、release 和 Unity
测试固件构建通过。混合角色九张 48×48 RGB565A8 图片和 U+2014 字形加入后，dev 镜像
`0x154b10`、app 分区余量 31%、DIRAM 静态余量 162,841 B；release 镜像 `0x134bd0`、app
分区余量 38%、DIRAM 静态余量 182,521 B；Unity 镜像 `0x449c0`。Unity 仅完成构建，未刷入
设备执行；未执行新素材的完整状态矩阵、连续切换或完整真实 Agent API 矩阵。Codex CLI
0.153.0、Claude Code 2.1.274 为发布主机本地版本；真机矩阵统一迁入 1.0 发布前门槛，不作为
0.5 的未关闭验收。素材母版/生成提示/哈希、字体 OFL、BSP 来源、LICENSE 与 NOTICE 已核对。
0.5.0 candidate 包完成合并镜像、包内 SHA256SUMS、外层 zip 校验和、BUILD-INFO、依赖锁、
LICENSE、NOTICE、字体许可证和动态端口烧录说明检查；正式包由 release-prep 干净提交重建。
