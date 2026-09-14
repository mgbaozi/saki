# Saki 0.3.0 实施任务清单

> 文档版本：0.3.0 开发清单
> 状态：Implementation in progress；核心代码与离线构建已贯通，BLE 首配与基础真机验证通过
> 产品规格：[SPEC.md](./SPEC.md)
> 工程设计：[IMPLEMENTATION.md](./IMPLEMENTATION.md)

## 1. 使用方式

状态标记：

- `[ ]` 未开始
- `[~]` 进行中
- `[x]` 已完成并验证
- `[!]` 被明确问题阻塞
- `[-]` 经评审取消或由其他任务取代

只有代码、自动测试、文档和相称的真机验证都完成后才能标记 `[x]`。验证记录只保留命令、
版本、计数、统计和结论摘要；原始蓝牙捕获、配对数据、真实设备 ID、用户路径和 Agent 内容
不得提交。

### 1.2 当前验证摘要（2026-09-14）

- `scripts/check.zsh`：122 项 Host/Schema/文档测试通过，Ruff 与 diff 检查通过。
- `scripts/build-firmware-profile.zsh dev`：`0.3.0-dev` 构建通过；镜像 `0x142ec0`，app
  分区余量 35%，DIRAM 静态余量 163,841 B。
- `scripts/build-firmware-profile.zsh release`：`0.3.0` 构建通过；镜像 `0x123510`，app
  分区余量 41%，DIRAM 静态余量 183,497 B。
- `scripts/build-firmware-tests.zsh`：Unity 测试固件 `0x322a0` 构建通过；尚未刷入设备执行。
- 目标板已刷入 `0.3.0-dev`。修复 UI 初始化与 USB/NimBLE 并发启动导致的 LCD 重启循环后，
  屏幕稳定且 USB doctor 通过；BLE doctor 的所有协议、诊断和资源检查通过，内部 heap 最低
  49,207 B，app/UI/USB/BLE 任务栈最低分别为 1,996/6,032/4,500/4,448 B。
- macOS 真机完成 K2 2 秒开启 120 秒窗口、LE Secure Connections Just Works 首配、K2 5 秒
  清除全部绑定、短按暂停/再次短按恢复。全过程无数字比较确认；清除后的重新配对成功。
- 清除设备与 macOS bond 后，未打开配对窗口的连接失败；打开窗口但不连接，等待 125 秒后
  再次连接以明确 `TimeoutError` 失败；随后重新开启窗口配对成功。失败路径未建立业务会话，
  USB 与资源诊断保持健康。
- 配对窗口内在 macOS 系统弹窗选择取消，Host 以 `Not connected` 失败，设备安全拒绝计数
  增加 1 且无 bond/业务会话；同一窗口再次选择配对后恢复成功，BLE doctor 通过。
- 设备短按 RST 后未重新开启配对窗口即可用原 bond 恢复，BLE doctor 通过且安全/丢包计数
  为 0。
- `ble cycle --count 20`：20/20 通过，失败 0；握手 min/mean/max 为
  11,050.6/11,226.5/11,531.2 ms。
- dev/release 真机 `ble fuzz --count 40 --seed 20260907`：每次共 48 case/7,387 B，设备均
  观察到 61 个 invalid 和 1 个 oversized，TX drop 为 0；同一加密连接恢复 hello、合法
  status 和 ping 通过，关闭后新连接 doctor 仍健康。
- dev 真机 100 次/10 秒 BLE smoke：100/100 applied；实际 12.8 秒，ACK mean/P95/max 为
  124.1/150.0/180.2 ms；协议异常、BLE drop/拒绝/断线和 transport rejection 增量均为 0，
  runtime 安全线通过。本地 JSON 报告位于被忽略的 `artifacts/`，不提交仓库。
- release 真机 100 次/10 秒 BLE smoke：100/100 applied；实际 13.2 秒，ACK mean/P95/max 为
  128.9/150.5/210.3 ms；全部诊断和 BLE 运行时错误增量为 0，内部 heap 最低 69,059 B、
  BLE 任务栈最低 4,464 B。
- 强制 BLE `send` 真机 applied；`basic.ndjson` 脱敏 fixture 经 BLE replay 的
  starting/working/completed 三步均 applied，随后 LaunchAgent 已恢复并重新连接 USB。
- macOS/CoreBluetooth 真机协商 ATT MTU=256，Host Write With Response 分片上限为 253 B；
  hello、ACK 和包含完整 runtime 的 pong 已通过。已发送 1,441 B 的全已知字段 status payload，
  ACK 为 356.3 ms；再用允许的扩展字段填充到严格 2,048 B payload，跨 9 个 GATT write 后
  applied=true，ACK 为 538.7 ms，满足 2 秒目标。
- `auto` 完成 1 次 USB→BLE→USB 物理拔插恢复，状态 seq 连续且 USB 成功抢占；快速拔插
  期间 BLE 短暂重连一次并增加 1 次 heartbeat timeout，仍需按切换矩阵重复验证。
- LaunchAgent 已恢复并保持 running：后台 `auto` 先连接 USB，拔线后通过原 bond 接管 BLE，
  插线复位后恢复 BLE 并由 USB 抢占；状态 seq 连续，stderr 为空。完整 10+10 切换矩阵仍未
  执行。
- Bluetooth off/on 3 次均通过：关闭态强制 BLE doctor 稳定返回
  `Bluetooth is powered off`，USB doctor 保持健康；重新开启后均使用原 bond 恢复，无需重新配对，
  BLE 握手 min/mean/P95/max 为 6,566.8/6,850.1/7,345.9/7,345.9 ms，协议与资源检查均健康。
- 尚未验证第二 Central、完整 USB↔BLE 切换矩阵、睡眠恢复、距离断开和 Unity 真机执行；
  相关任务继续保持进行中或未开始。

### 1.1 全局完成要求

- 开发开始前先完成 0.2.0 发布决策并保留可回退 release 基线。
- 不修改仓库外厂家示例，不运行 `idf.py set-target`，不静默修改分区表。
- 协议行为变化同时检查 Schema、fixtures、Host、固件测试和兼容性说明。
- BLE callback 不解析 JSON、不调用 LVGL、不执行阻塞等待。
- BLE 配对固定使用 LE Secure Connections Just Works；不降低为 Legacy Pairing、固定 PIN、
  debug key 或未加密业务特征。
- 所有外部字节、GATT mbuf、MTU、队列和 bond 数量保持有界。
- UI/配对/切换任务必须在 320×240 真机验证。
- 独占连接测试前停止常驻 Host，完成后恢复；Unity 测试后刷回正常固件。
- 未执行的验证保持未完成，不以编译通过代替真机结论。

## 2. 里程碑

| 里程碑 | 范围 | 完成条件 |
| --- | --- | --- |
| M0 BLE 可行性确认 | V0 | 0.2 基线、共存、安全配对、权限、MTU 和资源决策门全部通过 |
| M1 传输无关基线 | P0、F0、H0 | USB-only 行为不变，firmware/Host 不再写死单一串口会话 |
| M2 安全 BLE 贯通 | F1、F2、U0、H1 | 强制 BLE 模式完成配对、协议、恢复和诊断 |
| M3 自动 fallback | I0 | USB↔BLE 双向原子切换和全局 session/seq 通过矩阵 |
| M4 0.3.0 发布 | Q0、R0 | 规格验收、USB 回归、文档、候选包和版本一致性全部完成 |

## 3. V0：前置条件与风险 spike

### [ ] V0.1 关闭 0.2.0 发布基线

- 完成 0.2.0 `R1.1`、`R1.4` 和发布前剩余项。
- 记录最终 Host/firmware 版本、commit/tag 和关键 USB 验证摘要。
- 确认 0.3 分支或工作树从该基线开始，不把 0.2 未完成项混入 BLE 验收。

退出条件：有一个可重复构建、可刷回、可对比的 0.2.0 基线。

### [~] V0.2 验证 NimBLE、TinyUSB 与 LVGL 共存

- 基于 ESP-IDF 5.5.3 本机 `bleprph` 做最小临时 GATT service。
- 保持现有 USB CDC、320×240 UI、触摸和心跳同时运行。
- 记录 dev/release app size、内部 heap、PSRAM、app/UI/USB/NimBLE task stack。
- 验证 BLE 初始化失败时 USB 仍能启动。

通过标准：内部 heap 和任务栈高于规格安全线，USB 协议无回归。

### [x] V0.3 验证 K2 配对窗口与 macOS Just Works

- 设备配置 NoInputNoOutput、LESC Just Works level 2、bonding 和加密特征权限；关闭
  NimBLE 会强制 level 4/MITM 的运行时 SC-only 模式，同时在编译期关闭 Legacy Pairing。
- K2 按住至少 2 秒、但未达到 5 秒后松开，开启 120 秒配对窗口。
- 由配对窗口内对受保护 RX/TX 特征的访问触发 macOS 系统配对。
- 真机验证窗口外拒绝、窗口内成功、用户取消和窗口超时；确认不依赖显式
  `BleakClient.pair()`。

通过标准：只有本地 K2 开启窗口后才能建立 encrypted bond；失败不生成 bond。

### [~] V0.4 验证 bond、RPA 与 allow list

- 重启设备、重启 Host、关闭/开启 Bluetooth 后验证同一 Mac 自动恢复。
- 确认 macOS 私有地址可由 NimBLE identity resolving/allow list 正确识别。
- 第二台或未绑定 Central 在配对窗口外不能访问业务特征。
- repeat pairing 不自动删除原 bond。

### [x] V0.5 验证 MTU、分片和通知

- 记录 macOS 真机协商 ATT MTU 和 Bleak 可见的 payload 上限。
- 使用 Write With Response 发送跨多个片段的最大合法 status。
- 使用 Notify 返回跨多个片段的 hello/pong/ACK 测试帧。
- 验证断线清半帧、重连后从空 framer 开始。

通过标准：最大合法帧在 2 秒内得到应用 ACK；最小 20 B payload 的离线测试通过。

### [~] V0.6 验证前台权限与 LaunchAgent

- 在前台首次运行触发并完成 macOS Bluetooth 权限。
- 安装/启动 LaunchAgent 后验证扫描、连接和 Notify。
- 分别记录 Bluetooth powered off、权限拒绝和权限已授予的诊断行为。
- 确认后台不会循环弹窗或高频重试。

### [ ] V0.7 形成 Stage 0 决策记录

- 对 SPEC 第 13 节每项标记 pass/block。
- 把实际 MTU、资源初值、Bleak 版本范围和 macOS 限制回写 IMPLEMENTATION。
- 未通过项标记 `[!]` 并先修改规格或设计，不静默降低安全。

完成里程碑：M0。

## 4. P0：协议与兼容契约

### [x] P0.1 固化 BLE GATT 常量

- 在 Host 和 firmware 的单一位置定义 Service/RX/TX UUID。
- 自动测试核对 UUID 与 SPEC，避免两端漂移。
- 广播 fixture/测试确认包含完整 Service UUID，不包含设备 ID。

### [~] P0.2 扩展 hello capability

- 0.3 firmware 返回 `ble`、`secure-connection`、`transport-arbitration`。
- 0.2 Host 兼容测试确认忽略新值。
- 0.3 Host 在 USB 连接 0.2 firmware 时不要求这些值。
- BLE 连接缺少 `ble` capability 时明确失败。

### [x] P0.3 扩展 optional 诊断

- 定义 BLE connection/pairing/RX/TX drop 和 transport switch/reject 计数。
- `pong.runtime` 增加 BLE task/内存字段。
- 同步 Schema、fixtures、Host parser 和 firmware serialization 测试。
- 旧 pong 缺字段保持兼容，未知新字段可忽略。

### [~] P0.4 添加跨 transport 会话 fixture

- USB 工作→USB 断开→BLE 完整快照→完成。
- BLE 工作→USB 恢复→USB 完整快照。
- 同 session 跨 transport 旧 seq、重复 seq 和新 seq。
- 两条链路使用相同 id 时回复不能串线。
- 所有 fixture 人工审查脱敏。

### [x] P0.5 更新协议一致性测试

- 文档中的 hello/capability/diagnostic 示例通过 Schema。
- 自动检查 BLE 不增加状态字段或分片信封。
- `protocol/README.md` 链接当前 0.3 兼容说明，同时保留 0.2 字节限制来源。

## 5. F0：固件传输无关重构

### [~] F0.1 引入 transport id 和 per-peer protocol

- USB、BLE、NONE 使用枚举和统一名称映射。
- 每个 peer 独立拥有 framer、handshake、heartbeat 和 transmit callback。
- 清除 `saki_protocol.c` 中 `USB` 硬编码。
- hello/ACK/pong/error 从来源 peer 返回。

### [~] F0.2 拆分 peer 与全局状态

- peer 只处理字节、握手和语义解析。
- transport manager 维护 active transport/session/seq/last snapshot。
- status/clear 通过 candidate 接口提交。
- `APPLIED/STALE/BUSY` 映射符合 IMPLEMENTATION 第 4.3 节。

### [~] F0.3 实现 USB/BLE 仲裁状态机骨架

- 固定 `USB > BLE` 比较函数。
- 支持 USB 2 秒 grace、BLE immediate offline 和高优先级抢占。
- 使用单调时钟，无 callback 阻塞和并发 UI 写入。
- 状态机先用 fake links 完成 Unity 测试，不提前接入真实 BLE。

### [~] F0.4 适配现有 USB component

- RX/TX/connection/poll 改为 USB peer/context。
- 保留 DTR、StreamBuffer、TX queue、partial write 和日志隔离。
- 断线只清 USB peer，不清未来 BLE peer。
- runtime 继续报告 USB stack watermark。

### [~] F0.5 扩展 firmware Unity 测试

- 双 peer 半帧隔离、来源回复、heartbeat 隔离。
- 同 session 全局 seq 和新 session takeover。
- priority、busy、grace、抢占失败和无候选 offline。
- switch 只提交一个完整快照，不重播旧状态。

### [~] F0.6 USB-only 真机回归

- dev/release/test app 构建通过。
- doctor、send、replay、20 次 DTR cycle、100 次 smoke 和 fuzz 通过。
- 记录与 0.2 基线的 size、heap、stack 和 ACK 延迟差异。

退出条件：完全不启用 BLE 时行为与 0.2.0 等价。

## 6. H0：Host 传输无关重构

### [x] H0.1 定义 async byte transport

- 定义 connect/read/write/close、kind 和 identity hint。
- 读写有界、可取消，断线会解除全部等待请求。
- transport 层不生成协议 id/seq，不理解状态。

### [x] H0.2 提取共享 ProtocolSession

- 统一 NDJSON framer、request/reply_to 匹配、hello/status/clear/ping。
- 单 writer，迟到或未知 reply 安全丢弃。
- 支持 transport-aware ACK timeout 和原消息重发。
- 每个物理连接独立 receive buffer；逻辑 Codec 可跨连接共享。

### [~] H0.3 适配 pyserial

- 阻塞调用继续由 `asyncio.to_thread()` 承载。
- 现有 CLI 和 service 改用共享异步 session。
- 删除重复 request/framer 逻辑前先补齐合同测试。
- 保持 USB port disappearance 快速检测。

### [x] H0.4 引入进程级 logical session

- service 启动时创建一次 UUID/codec。
- id/seq 跨 transport 全局递增。
- reconnect/switch 发送 latest snapshot 的新 seq。
- Host 进程重启才生成新 session。

### [~] H0.5 Host USB 回归

- 全部现有 Host 单元测试和 Ruff 通过。
- fake serial 覆盖 partial read/write、丢 ACK、断线和取消。
- 真机 doctor、serve、LaunchAgent 和 hook→ACK 基线通过。

完成里程碑：M1（与 P0、F0 一起）。

## 7. F1：NimBLE transport

### [~] F1.1 配置最小 NimBLE 功能集

- Peripheral/Broadcaster/GATT Server only，max connection=1。
- preferred ATT MTU=256，安全与 NVS persist 按 SPEC 配置。
- dev/release 关闭不需要的 Central/Observer/GATT Client/debug 功能。
- 增加 generated sdkconfig 安全断言测试。

### [~] F1.2 注册 Saki GATT service

- RX 仅 Write With Response + encrypted permission。
- TX 仅 Notify + encrypted subscription permission。
- 记录 conn handle、TX value handle、CCCD 和 negotiated MTU。
- 未加密访问不能到达 protocol peer。

### [~] F1.3 实现 BLE RX 路径

- GATT callback 有界复制完整 mbuf chain 到 4096 B StreamBuffer。
- buffer full 返回 ATT error 并增加计数，不阻塞 callback。
- BLE worker 把字节交给 BLE protocol peer。
- disconnect/new handshake 清除未完成半帧。

### [~] F1.4 实现 BLE TX 路径

- protocol reply 进入独立有界 TX queue。
- 按 `mtu - 3` 串行拆 Notify，不交错帧。
- 处理 congestion、notify completion、disconnect 和 timeout。
- TX 未订阅时拒绝开始协议会话并记录诊断。

### [~] F1.5 实现广播生命周期

- Service UUID 广播与通用 `Saki` 名称。
- 无 bond 普通状态不可连接；配对窗口可连接；有 bond 使用 resolving/allow list。
- 连接失败/断开后按当前 bond 状态恢复正确广播。
- 广播不包含设备 ID、任务或绑定信息。

### [~] F1.6 实现 BLE peer 生命周期

- encrypted、bonded 且 subscribed 后才允许 hello。
- heartbeat timeout 清 BLE peer/session/半帧并触发 active link offline。
- BLE 初始化失败不阻止 USB。
- runtime/diagnostic 指标接入 pong。

### [~] F1.7 BLE firmware 测试

- 20/61/182/253 payload 分片与 MTU 变化。
- 多 write 拼帧、多 Notify 拆帧、连续多帧、断线半帧。
- RX/TX queue full、非法访问、第二连接和订阅缺失。
- build dev/release/test app 并记录 size/heap/stack。

## 8. F2：安全与绑定

### [~] F2.1 实现 LESC Just Works 安全配置

- `sm_bonding=1`、`sm_mitm=0`、`sm_sc=1`、NoInputNoOutput。
- security level 2；运行时 `sm_sc_only=0`，避免 NoIO Just Works 被错误提升为要求 MITM
  的 level 4。
- Legacy pairing、debug keys、fixed passkey 关闭。
- RX/TX 权限在连接描述上验证 encrypted/bonded。
- 安全配置加入编译后自动审查。

### [x] F2.2 实现本地配对授权窗口

- 只有 K2 `PAIR_RELEASE` action 能开启 120 秒 pairable window。
- pairable window 外无 bond 连接不可访问业务 GATT，新 pairing request 一律拒绝。
- 首次成功 bond、窗口到期、用户清除 bond 或设备重启都会关闭窗口。
- 窗口状态不持久化，不写入可识别 Central 的日志。

### [~] F2.3 持久化一个 bond

- 使用 NimBLE store/NVS，不自定义密钥格式。
- 重启设备后已绑定 Mac 可恢复。
- 达到一个 bond 后拒绝新配对。
- repeat pairing 保留旧 bond 并拒绝。

### [~] F2.4 清除绑定

- K2 连续按住达到 5 秒立即触发一次，不需要二次确认。
- 只删除 NimBLE bond/allow/resolving 数据。
- 断开当前 BLE、清 peer 和临时安全状态。
- 最后 bond 删除后重置 identity/IRK（按真机验证结论）。
- 不调用全局 NVS erase，不影响 UI/USB。

### [~] F2.5 安全负向测试

- 配对窗口外连接、未加密写、未绑定重连、第二 Central。
- 120 秒窗口超时、用户取消系统配对、重复配对。
- K2 在 2 秒前、2–5 秒、达到 5 秒三个边界不串动作。
- 设备端已清但 Mac 未忘记、Mac 已忘记但设备未清。
- 确认所有失败均不生成可用业务 session。

## 9. U0：K2 输入与连接 UI

### [~] U0.1 实现 K2 输入组件

- 轮询 AW9523B `P0_1`（K2，高电平按下），周期初值 20–50 ms。
- 输入消抖并用单调时钟计算时长；一次按压只输出一次最终 action。
- 输出 `SHORT_RELEASE`、`PAIR_RELEASE` 或 `CLEAR_THRESHOLD`，不直接调用 NimBLE/LVGL。
- 在确认 GPIO42 中断复用关系前不启用 AW9523B 中断。

### [~] U0.2 实现 K2 策略状态机

- 少于 2 秒松开是短按；连接时断开并进入本次运行有效的手动暂停。
- 手动暂停或普通断线时再次短按：有 bond 则重新开放 allow-list 广播，等待 Mac Host 连接；
  无 bond 则提示长按 K2 2 秒。
- 达到 2 秒只显示提示，不立即配对；在 2–5 秒间松开才开启 120 秒配对窗口。
- 达到 5 秒立即清除全部 BLE bond，且本次按压不再触发配对；状态机不影响 USB。
- 手动暂停不写 NVS，设备重启恢复已绑定 BLE 的自动重连策略。

### [~] U0.3 实现 K2/配对信息 overlay

- 显示按住进度、“松开进入配对 / 继续按住清除”、120 秒倒计时、手动暂停、等待 Host
  连接和清除结果；不提供数字确认控件。
- overlay 不改变 Agent 快照、耗时或进度，结束后恢复原页面和详情状态。
- 等待页提示 USB/BLE。
- badge 仅显示权威 `USB`、`BLE`、`OFFLINE`。
- transport 切换不重播 completed/failed 动画。
- 离线覆盖层继续保留最后快照。

### [~] U0.4 UI policy Unity 测试

- 消抖、误触、1.99/2.00/4.99/5.00 秒边界和一次按压只发一次 action。
- 2 秒动作不抢跑、5 秒清除不附带配对、120 秒窗口 deadline。
- BLE event、K2 action 与 USB 切换同时到达时的确定性结果。

### [~] U0.5 320×240 真机验收

- 九种 Agent 状态上打开/关闭配对页。
- 中文按压提示、倒计时、失败、暂停、重连和清除结果。
- K2 短按/2 秒/5 秒手感、暗屏、详情、transport 切换和断线覆盖层交互。

## 10. H1：Mac BLE 支持

### [x] H1.1 增加 Bleak 可选依赖

- 新增 `ble` extra 并按 V0 结果固定兼容范围。
- 延迟 import；没有 extra 时 USB 命令正常运行。
- `auto` 一次警告后降级，显式 BLE 命令非零退出。

### [x] H1.2 实现 Service UUID 扫描

- 扫描始终传 Service UUID filter。
- 使用 `BLEDevice`/CoreBluetooth UUID，不使用 MAC 假设。
- 缓存 UUID 只作提示，连接后用 hello.device.id 确认。
- 多个设备时 CLI 明确选择；自动模式只连已验证缓存。

### [~] H1.3 实现 GATT 连接与加密触发

- 验证 Service/RX/TX 和特征属性。
- 先订阅 TX，再发送 hello。
- 在设备配对窗口内通过受保护特征触发 macOS 系统配对。
- 加密/绑定失败、窗口关闭和 TX 未订阅有明确错误。

### [x] H1.4 实现 BLE 写分片与 Notify reader

- 按可信 `mtu - 3` 或保守 20 B 切片。
- 每片 `response=True`，单 writer，不交错帧。
- Notify callback→有界 queue→共享 framer。
- queue overflow、disconnect 和取消使 session 失败并清半帧。

### [x] H1.5 实现 BLE CLI

- `ble list`、`ble pair`、`ble cycle`、`ble fuzz`、`ble soak`。
- doctor/serve 支持 `auto|usb|ble`。
- 命令帮助明确设备配对窗口、常驻服务独占和 macOS 双端忘记流程。
- 最终命令名更新 USER_GUIDE，不能保留不存在的示例。

### [~] H1.6 实现权限与错误分类

- Bleak missing、powered off、unauthorized、scan timeout、GATT mismatch。
- pairing cancelled、encryption failed、notify unavailable、ACK timeout、busy。
- 稳定错误不高频重试、不刷日志、不阻塞 hook socket。

### [~] H1.7 Host BLE 测试

- fake scanner/client 覆盖发现、多个设备、连接、Notify 和断线。
- 最小/典型/最大 chunk、partial frame、queue full 和迟到 reply。
- optional dependency 缺失和 USB-only import/test。
- privacy 测试确认日志无密钥、完整 ID 或帧正文。

完成里程碑：M2（与 F1、F2、U0 一起）。

## 11. I0：自动 fallback 与切换

### [~] I0.1 实现 Host ConnectionManager

- `auto` 启动优先 USB，无 USB 时尝试已验证 BLE。
- BLE 活动时监测 USB 恢复。
- hook mailbox 与扫描/连接独立，永远保留 latest snapshot。
- 一个 supervisor lock 串行化 status、heartbeat 和切换。

### [~] I0.2 对齐 2 秒 USB grace

- USB 消失时 Host 与 firmware 使用同一 2 秒策略。
- grace 内 USB 恢复不触发 BLE 状态接管。
- BLE 扫描可提前，但 grace 前不提交状态。
- 虚拟时钟和真机 USB 抖动均覆盖。

### [~] I0.3 实现 USB→BLE fallback

- 保持 logical session 和全局 id/seq。
- BLE hello 后发送 latest snapshot 的新 seq。
- 只有 ACK applied=true 才报告切换成功。
- 失败时保留离线旧快照并有上限退避。

### [~] I0.4 实现 BLE→USB 抢占

- BLE 保持活动直到 USB hello + latest status ACK。
- firmware 允许高优先级完整快照原子接管。
- ACK 后 Host 才关闭 BLE。
- USB 候选失败时继续 BLE，不抖动或清 UI。

### [~] I0.5 实现跨 transport session/seq 防御

- 同 session 旧/重复 seq 返回 applied=false。
- 不同 session 的低优先级候选返回 busy。
- ACK/pong/error 只匹配来源 transport。
- 两条链路并发、迟到响应和 disconnect race 有确定结果。

### [~] I0.6 切换矩阵真机验收

- USB→BLE 10 次、BLE→USB 10 次。
- 活动、等待、完成、失败状态均至少覆盖一次。
- USB <2 秒抖动、USB 抢占失败、BLE 中途断线。
- 记录 min/mean/P95/max 与 UI 结果。

通过标准：达到 SPEC 时延目标，无空白、旧状态回退、动画重播或 ACK 串线。

完成里程碑：M3。

## 12. Q0：质量、恢复与安全

### [~] Q0.1 BLE 协议互操作

- hello、九种状态、clear、ping/pong、error 和 sessions replay。
- 真机实际 MTU 和 20 B 离线模拟均通过。
- 最大合法帧双向分片在 2 秒内 ACK。

### [x] Q0.2 BLE cycle、smoke 和 fuzz

- 20 次 BLE connect/disconnect cycle。
- release 100 次状态 smoke，异常/drop 增量为 0。
- 40-case 确定性 fuzz 后同一/新连接可重新握手并应用状态。
- 报告只保存在本地 artifacts，文档记录摘要。

### [~] Q0.3 睡眠与无线恢复矩阵

- Mac 睡眠/唤醒 3 次。
- Bluetooth off/on 3 次。
- 至少 1 次距离断开并返回范围。
- 设备 reset、Host restart、LaunchAgent restart。
- 每次恢复都要求 hello + 完整 status。

### [ ] Q0.4 配对与换 Mac 验收

- 首配、拒绝、超时、重启后 bond 恢复。
- 设备端清 bond、macOS 忽略设备、换 Mac 后重配。
- 旧 Mac 在清除后不能继续提交状态。
- 不删除无关 NVS 数据。

### [~] Q0.5 安全与隐私审查

- generated sdkconfig 与运行时安全标志符合 SPEC。
- 未加密/未绑定/第二 Central 全部负向测试通过。
- firmware/Host 日志和 fixture 不含密钥、完整 ID 或 Agent 内容。
- 广播不含稳定设备 ID 或用户数据。

### [~] Q0.6 资源与性能

- dev/release/test app 干净构建。
- 记录 app size、internal/total heap、全部 Saki/NimBLE stack watermark。
- 记录 BLE hook→ACK、USB→BLE、BLE→USB 和 sleep wake 统计。
- 达到 SPEC 第 9 节安全线和时延目标。

### [ ] Q0.7 完整 USB 回归

- USB 20 次 DTR、3 次物理拔插、100 次 smoke、40-case fuzz。
- doctor/send/replay/serve/LaunchAgent 和真实 hook 通过。
- UI、触摸、进度、耗时、断线和日志隔离通过。
- 与 0.2 基线差异有解释且无发布级退化。

## 13. R0：文档与发布

### [~] R0.1 完成 0.3 用户指南

- 把 [USER_GUIDE.md](./USER_GUIDE.md) 从规划流程更新为实际命令。
- 覆盖 BLE extra、前台权限、配对、auto 模式、双端清 bond和排障。
- 明确 macOS 无显式 Bleak unpair API 的限制。

### [~] R0.2 更新项目入口文档

- 根 README、docs index、host/firmware/protocol README。
- BLE 从路线图迁移为已实现能力；Wi-Fi 仍为后续。
- 所有本地链接和命令由自动测试校验。

### [~] R0.3 发布配置审查

- firmware=`0.3.0`、Host=`0.3.0`、协议仍为 v1。
- release 关闭 UI demo、NimBLE debug、SC debug keys 和完整协议日志。
- BLE optional dependency/许可证进入依赖和第三方说明。
- release app 分区余量至少 20%。

### [ ] R0.4 生成 0.3.0 候选包

- 更新 package script、BUILD-INFO、SHA256SUMS、离线烧录和安装说明。
- 包含 BLE Host 安装说明和第三方许可证。
- 不包含 bond、Keychain 数据、原厂 Flash、设备 ID 或本地 validation 产物。

### [ ] R0.5 对照 SPEC 完成功能与安全验收

- 逐项执行 SPEC 11.1、11.2。
- 每项记录通过、失败或不适用理由。
- 任何安全降级都必须回到规格评审，不能以已知限制放行。

### [ ] R0.6 对照 SPEC 完成稳定性与性能验收

- 逐项执行 SPEC 11.3、11.4。
- 链接 cycle、smoke、fuzz、切换、sleep 和资源摘要。
- 24h/10,000 次与 LCD 光学延迟保持可选，不冒充已执行。

### [ ] R0.7 发布决策

- 所有 blocker 关闭。
- Host/firmware 同版本，最终构建来自干净提交。
- 已知限制写入 release notes。
- 经用户明确要求后才创建 commit、tag、GitHub Release 或推送。

完成里程碑：M4。

## 14. 当前决策点与 blocker

| ID | 决策点 | 当前状态 | 解除方式 |
| --- | --- | --- | --- |
| B-001 | 0.2.0 仍是 release candidate | 未解除 | 完成 V0.1 |
| B-002 | K2 限时窗口 + LESC Just Works 的成功、窗口外拒绝、自然超时和用户取消均通过 | 已解除 | V0.3 于 2026-09-07 完成 |
| B-003 | NimBLE + TinyUSB + LVGL 的运行时 RAM/栈水位已满足安全线，BLE 初始化失败回退未验证 | 部分解除 | 完成 V0.2 剩余故障注入，不满足安全线则先裁剪功能 |
| B-004 | 同一 Mac 原 bond 经 3 次 Bluetooth off/on 均恢复；第二 Central 与 repeat pairing 待验证 | 部分解除 | 完成 V0.4 剩余身份负向测试 |
| B-005 | 首次授权、LaunchAgent BLE fallback 与 powered-off 诊断已通过；权限拒绝未验证 | 部分解除 | 完成 V0.6 剩余权限负向测试 |
| B-006 | 实际 ATT MTU=256、写分片=253 B；严格 2,048 B 合法帧 ACK 538.7 ms | 已解除 | V0.5 于 2026-09-14 完成 |

解除 blocker 后保留简短结论和验证日期，不删除历史。
