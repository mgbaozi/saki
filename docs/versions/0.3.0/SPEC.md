# Saki 0.3.0 产品与通信规格

> 文档版本：0.3.0 历史开发规格
> 状态：已归档；0.3.0 未单独发布，BLE fallback 能力已并入 0.4.5
> 基线版本：0.2.0；未执行的完整真机矩阵统一迁入 1.0 发布前门槛
> 目标硬件：正点原子 ATK-DNESP32S3B3 / ESP32S3 BOX3
> 软件栈：ESP-IDF 5.5.3、FreeRTOS、LVGL 8.4.0、NimBLE、Python 3.12
> 应用协议：继续使用 v1 UTF-8 NDJSON

## 1. 版本目标

0.3.0 在保留 0.2.0 USB 能力和状态模型的前提下，加入可安全绑定、自动恢复的 BLE
传输，并完成 USB 与 BLE 之间最小但完整的双传输仲裁。

本版本的完成结果应当是：用户通过 K2 打开本地配对窗口并完成一次绑定后，日常仍优先使用 USB；USB 不可用
时 Mac Host 自动通过 BLE 恢复当前完整状态，USB 恢复后再无闪屏、无旧状态回退地切回。

### 1.1 核心原则

- 协议不分叉：USB 和 BLE 传输完全相同的协议 v1 NDJSON 字节流。
- 默认安全：BLE 业务特征只允许已加密并完成绑定的 LE Secure Connections 链路访问。
- 本地授权：首次配对必须通过设备 K2 显式开启短时窗口，不要求数字比对或设备端确认。
- 单一权威：任一时刻只有一个 transport 可以提交状态，默认优先级为 `USB > BLE`。
- 完整切换：候选 transport 必须完成 `hello` 并提交完整 `status` 后才能接管 UI。
- 有界实现：分片、重组、队列、连接数和绑定数均有固定上限。
- 兼容降级：没有安装 BLE 可选依赖时，现有 USB 功能仍可独立工作。

## 2. 范围

### 2.1 本版本范围

- ESP32-S3 使用 ESP-NimBLE 作为 Peripheral/GATT Server。
- 使用既有 Saki Service、Host→Device RX 和 Device→Host TX UUID。
- RX 使用 Write With Response，TX 使用 Notify；两端按协商 ATT MTU 分片并重组 NDJSON。
- 支持 LE Secure Connections Just Works、绑定持久化和一个已绑定 Mac。
- K2 短按控制 BLE 断开/重连，长按 2 秒打开配对窗口，长按 5 秒清除全部 BLE 绑定。
- Mac Host 通过 `bleak` 可选依赖发现、连接、订阅、收发、诊断和恢复 BLE。
- `auto` 模式实现 USB 优先和 USB↔BLE 双向切换；同时保留强制 `usb`、`ble` 模式用于诊断。
- Host 与固件继续使用相同逻辑 session、消息 id、状态 seq、ACK、心跳和完整快照语义。
- 扩展诊断、CLI、自动测试和真机恢复矩阵，覆盖配对、MTU、睡眠/唤醒与距离断开。

### 2.2 非目标

- 不实现 Wi-Fi、mDNS、TCP、配网、TLS 或云端中继。
- 不实现 USB/BLE/Wi-Fi 三传输完整矩阵；0.3.0 只实现 USB 与 BLE 的子集。
- 不提供手机 App、浏览器 Web Bluetooth 客户端或多平台 Host。
- 不允许多个 Mac 同时绑定或轮流控制；换 Mac 前必须显式清除旧绑定。
- 不通过 BLE 配置 Wi-Fi，也不新增通用配置消息。
- 不通过设备批准、取消、重试或控制 Agent；K2 只管理设备本地 BLE 生命周期。
- 不修改任务状态枚举、状态字段含义或 UI 角色/主题映射。
- 不引入 OTA、Secure Boot 或 Flash Encryption；这些安全能力需要独立版本设计。

## 3. 用户体验

### 3.1 首次配对

1. 用户安装 Host 的 BLE 可选依赖，并暂时停止占用设备的常驻 Host。
2. 按住 K2；达到 2 秒后屏幕提示“松开进入配对，继续按住 5 秒将清除绑定”。
3. 在 2 秒及以上、5 秒以内松开 K2，设备进入 120 秒可配对窗口。
4. Mac 前台运行配对命令，按 Saki Service UUID 扫描并连接设备。
5. 访问加密特征时由 macOS/CoreBluetooth 自动完成 LE Secure Connections Just Works 配对。
6. 设备保存唯一绑定，Host 用协议 `hello` 校验设备身份并发送完整状态。

2 秒动作必须在松开时触发，避免一次 5 秒长按先短暂开放配对窗口。不得使用固定 PIN、
Legacy Pairing，或在配对窗口外接受新的未绑定 Central。

### 3.2 日常连接

- USB 可用时，屏幕连接标识为 `USB`，Host 不需要维持 BLE 业务会话。
- USB 不可用且存在有效绑定时，Host 自动扫描并连接 BLE，连接标识变为 `BLE`。
- USB 恢复后，Host 先在 USB 上完成握手并发送最新完整快照；设备 ACK 后才关闭 BLE。
- BLE 断开时保留最后状态并显示离线覆盖层；重连后必须用新握手和完整快照恢复。
- BLE 已连接时短按 K2，设备主动断开 BLE 并进入本次开机有效的手动暂停状态，避免 Host
  立即自动重连。再次短按 K2 清除暂停并重新启用已绑定设备的可连接广播；实际连接仍由
  Mac Host 作为 Central 发起。
- BLE 因距离、休眠或异常断开时不进入手动暂停，设备仍允许自动恢复。
- 没有 bond 时短按 K2 不开放配对，只提示“长按 K2 2 秒开始配对”。
- USB 是当前权威 transport 时，K2 不会断开 USB；再次启用 BLE 也不能越过 USB 优先级接管 UI。
- 手动暂停不写 NVS；设备重启后恢复为允许已绑定 Mac 自动连接。
- 蓝牙关闭、权限被拒绝或 BLE 可选依赖缺失时，Host 继续尝试 USB，并给出一次可操作诊断，
  不能刷屏或拖慢 Agent hook。

### 3.3 清除绑定与换 Mac

- 持续按住 K2 达到 5 秒时，设备立即断开 BLE 并删除全部 Saki BLE bond/allow-list 数据；
  不要求触摸或第二次确认，也不能同时触发 2 秒配对动作。
- macOS 端配对记录无法由 Bleak 显式删除时，CLI 必须引导用户在系统设置中“忽略此设备”。
- 换 Mac 必须先清除设备端绑定，再在旧 Mac 上忽略设备，然后从新 Mac 重新配对。
- 清除绑定不能擦除整个 NVS、设备配置、固件或任务状态。
- 重复配对请求不得自动删除旧 bond；必须由用户执行上述显式流程。

## 4. BLE GATT 契约

### 4.1 角色与 UUID

ESP32 是 Peripheral/GATT Server，Mac 是 Central/GATT Client。

| 用途 | UUID | 属性 |
| --- | --- | --- |
| Saki Service | `9f6d0100-7c7a-4c3b-9d9a-73616b690001` | Primary Service |
| Host→Device RX | `9f6d0101-7c7a-4c3b-9d9a-73616b690001` | Write With Response、Encrypted |
| Device→Host TX | `9f6d0102-7c7a-4c3b-9d9a-73616b690001` | Notify、Encrypted |

- 0.3.0 不增加控制特征、长度特征或自定义分片特征。
- TX 的 CCCD 必须成功订阅后才能发送 `hello`。
- 一次只允许一个 BLE connection；新的未绑定连接不得挤掉当前已加密连接。

### 4.2 广播与发现

- 广播必须包含完整 128-bit Saki Service UUID，供 CoreBluetooth 按服务过滤。
- 广播名使用通用 `Saki`，不广播完整设备 ID、Mac 身份、任务或绑定信息。
- 无 bond 且未处于配对窗口时只做不可连接广播，便于诊断但不接受连接。
- 配对窗口内允许一个未绑定 Central 连接。
- 已绑定时使用 NimBLE resolving/allow list，只接受已绑定 Central；仍不得依赖随机地址作为身份。
- Host 在 macOS 上可缓存 CoreBluetooth 的不透明 UUID 作为重连提示，但最终身份必须由加密链路
  上的 Saki `hello.device.id` 确认。

### 4.3 安全级别

- 仅允许 LE Secure Connections；关闭 Legacy Pairing 和调试密钥。
- 必须启用 bonding、持久化密钥和 128-bit 加密，但不要求 MITM。
- IO capability 采用 No Input No Output，配对方式为 LE Secure Connections Just Works。
- RX 写入和 TX 通知/订阅要求 encrypted link；完成配对后只允许已保存的 bond。
- 未加密、未绑定或配对窗口外的新 peer 必须拒绝，且不能进入协议 parser。
- bond 记录达到上限时拒绝新配对，不自动淘汰已有 Mac。

该方案以“用户必须在设备旁按住 K2 开启 120 秒窗口”作为本地授权边界，操作比数字比对简单，
但不提供 MITM 身份验证：窗口开放期间，附近攻击者理论上可能抢先完成绑定。0.3.0 接受这一
明确权衡，并通过短窗口、单 bond 和 K2 清除全部 bond 限制风险。

### 4.4 MTU 与分片

- 设备 preferred ATT MTU 初始设为 256；实际值以每条连接协商结果为准。
- 每个 GATT 数据块最多为 `ATT_MTU - 3` 字节；两端必须能在最小 MTU 23 下工作。
- Host 把一个 NDJSON 帧顺序拆成多个 Write With Response；不得并发写入或交错两帧。
- 固件把每次 RX write 的全部 mbuf 内容按顺序复制到有界字节流，再复用同一 NDJSON framer。
- 固件把 ACK、pong、error 或 hello 帧顺序拆成多个 Notify；同一帧的片段不得与下一帧交错。
- Host 的 Notify callback 只复制字节并喂给有界 framer；解析、请求匹配和日志在事件循环中完成。
- 断线、取消连接或新的协议握手开始时，双方都清除未完成的半帧。
- MTU 变化只影响后续片段大小，不改变字节流内容，也不自行清除已收到的合法半帧。
- 应用帧仍以 LF 结尾，最大 2048 字节，不增加分片头、CRC、压缩或 Base64。

## 5. 协议兼容与能力协商

### 5.1 协议版本

0.3.0 继续使用协议主版本 `v=1`。BLE 只是字节 transport，不改变：

- 状态枚举和字段；
- 完整 `status` 快照语义；
- `session`、`id`、`seq`；
- ACK、ping/pong、clear 和 error；
- UTF-8、NDJSON 和 2048 字节上限。
- `elapsed_ms` 的 `0..9007199254740991` JSON 安全整数范围。

设备 0.3.0 的 `hello.capabilities` 增加下列可选值：

- `ble`
- `secure-connection`
- `transport-arbitration`

旧 Host 必须忽略这些值。0.3 Host 只有在 BLE GATT 已加密、绑定且 `hello` 返回 `ble` 时才把链路
视为 0.3 BLE 设备。

### 5.2 transport 上下文

- `transport` 仍不是 Host `status` 的字段；固件在快照通过仲裁后写入本地 `USB` 或 `BLE`。
- 每条物理链路有独立 framer、握手状态、TX 路由和心跳 deadline。
- hello、ACK、pong 和 error 必须从收到请求的同一 transport 返回。
- 任何 transport 的非法帧、半帧或超时都不得污染另一 transport 的 parser 或回复队列。
- 诊断计数允许在现有 `pong.diagnostics` 和 `pong.runtime` 中增加可选 BLE/仲裁字段；未知字段
  仍按协议 v1 规则忽略。

## 6. USB/BLE 仲裁

### 6.1 权威状态

固件 transport manager 维护全局活动 transport、活动 session 和最后应用 seq。每条链路的
协议 parser 不能直接修改 UI。

候选 transport 只有同时满足以下条件才能接管：

1. 物理/GATT 链路可用；BLE 还必须完成加密、绑定和 TX 订阅；
2. 在该链路上完成 `hello`；
3. 提交一条通过全部语义校验的完整 `status`；
4. 满足优先级、session 和 seq 规则。

### 6.2 优先级与切换

- 固定优先级为 `USB > BLE`，本版本不提供用户自定义顺序。
- 活动 USB 存在时，BLE 的状态提交返回现有 `busy` 错误，不能覆盖 UI。
- USB 物理断开后保留 2 秒宽限期，避免枚举抖动；宽限期内不允许 BLE 接管。
- 宽限期结束后，已握手的 BLE 必须提交新的完整快照才可接管。
- BLE 活动时 USB 恢复，USB 可在握手和完整快照通过后立即原子接管。
- 切换前保留旧快照；切换时一次性替换内容和连接标识，不能先清空页面。
- 活动 BLE 断开不等待 USB 宽限，立即冻结最后快照并进入离线状态。

### 6.3 session 与 seq

- 一次 Host 进程运行使用一个逻辑 session UUID，跨 USB/BLE 重连与切换保持不变。
- `id` 和状态 `seq` 在该 Host 进程内全局递增，不能为每个 transport 单独从 1 开始。
- 切换时发送最新状态的新完整快照，并使用新的 seq；不能复用已 ACK 快照的旧 seq。
- 固件对同一 session 跨 transport 执行全局 seq 去重，旧值只能返回 `applied=false`。
- 新 Host session 只有在候选 transport 被允许接管且完整状态已应用时，才能替换权威 session。
- 不得把一个 transport 收到的 ACK 与另一个 transport 的待处理请求匹配。

## 7. Mac Host 行为

### 7.1 安装与降级

- `bleak` 作为 `ble` 可选依赖提供，不增加 USB-only 用户的必需运行时依赖。
- 未安装 `bleak` 时，`auto` 等价于 USB-only 并只提示一次；显式 `ble` 模式必须返回非零。
- 首次 BLE 使用应在前台完成，以便 macOS 显示 Bluetooth 权限并完成系统配对流程。
- LaunchAgent 不得循环触发权限提示；权限拒绝应成为稳定、可诊断的降级状态。

### 7.2 发现与身份

- BLE 扫描必须传入 Saki Service UUID filter，不能按设备名或蓝牙 MAC 作为唯一条件。
- macOS CoreBluetooth 返回的外围设备标识是本机不透明 UUID，不是硬件地址。
- 缓存标识只用于加速查找；连接后仍必须验证 Service/RX/TX UUID、特征属性和 `hello`。
- 多个可配对 Saki 同时可见时，配对 CLI 必须列出本机 UUID/RSSI 并要求明确选择。
- 正常自动重连只连接已成功完成 Saki 握手的缓存设备，不探测任意 BLE 外设。

### 7.3 模式与恢复

规划的 Host 模式：

| 模式 | 行为 |
| --- | --- |
| `auto` | 默认；USB 优先，无 USB 时尝试已绑定 BLE，BLE 活动时监测 USB 恢复 |
| `usb` | 只使用现有串口路径；不初始化 Bleak |
| `ble` | 只使用已绑定 BLE；用于无线使用和诊断 |

- Host 必须保存最新完整快照，任一 transport 重连后只发送该快照，不重放事件历史。
- USB ACK 默认超时保持 1 秒；BLE ACK 默认超时为 2 秒，均最多重发 2 次且保留原 id/seq。
- BLE 心跳仍为 5 秒，设备 15 秒没有有效 Host 消息后回到未握手状态。
- 切换 supervisor、hook collector 和状态 timeline 必须解耦；BLE 扫描/连接不能阻塞 hook。

### 7.4 诊断入口

0.3.0 至少提供以下能力，最终命令名可以在实现阶段按现有 CLI 风格微调：

- 列出可见 Saki BLE 设备、RSSI 和本机 CoreBluetooth UUID；
- 前台配对并验证 GATT、加密/bond 状态、hello 和完整状态；
- 对 BLE 单独执行 doctor、cycle、send、replay、fuzz 和短时 soak；
- 报告 Bleak 缺失、Bluetooth powered off、权限拒绝、配对窗口关闭、加密/绑定失败、特征缺失、
  TX 未订阅、ACK 超时和设备 busy；
- 显示当前活动 transport 和最近一次切换原因，但不记录任务正文或完整设备 ID。

## 8. 设备 UI 行为

- 等待连接页提示 `USB / BLE`，但不把“广播中”显示为已连接。
- 顶栏连接标识仅显示活动权威 transport：`USB`、`BLE` 或 `OFFLINE`。
- BLE 提示层是本地覆盖层，不改变 Agent 主状态、任务耗时或 Host 协议快照。
- K2 操作时显示按住时长、2/5 秒阈值、配对窗口倒计时、BLE paused/connecting 和清除结果。
- K2 不受触摸“暗屏首次点击只唤醒”规则限制；按键动作生效并同时唤醒屏幕显示反馈。
- 5 秒清除绑定不显示确认按钮，但达到阈值前必须持续显示清除进度，避免误触不知情。
- USB/BLE 切换不得重播 completed/failed 入场动画；只有 Agent 状态本身变化才触发。
- 无有效连接时沿用 0.2.0 的旧快照淡化和亮度策略。

## 9. 资源、性能与可靠性目标

- BLE warm 状态更新：hook 源时间戳到设备 ACK 的 P95 小于 750 ms。
- 2048 字节最大合法帧在 BLE 上应在 2 秒 ACK deadline 内完成发送、解析和 ACK。
- 已绑定、Bluetooth 可用时，USB 消失到 BLE 完整状态 ACK 的 P95 小于 8 秒，包含 2 秒宽限。
- BLE 活动时发现 USB，到 USB 完整状态 ACK 的 P95 小于 3 秒。
- Mac 睡眠/唤醒后 15 秒内恢复一个权威 transport，或明确保持离线并给出可诊断原因。
- BLE 保持每秒最多 4 个状态、短时每秒 10 帧的既有输入上限；积压时只保留最新完整快照。
- release 固件内部 heap 历史最低值不低于 32 KiB；任一 Saki 任务栈余量不低于 1 KiB。
- release app 必须保留至少 20% factory 分区余量；若无法满足，应先评审分区和 OTA 路线，
  不能静默扩大或修改分区表。
- BLE 断线、RX 拥塞、通知拥塞和非法输入不得导致复位、内存持续下降或 UI 卡死。

性能计时以 hook 同机单调时钟到设备成功 ACK 为准；除非另做光学测量，不宣称覆盖 LCD
最后像素。

## 10. 异常与恢复矩阵

| 场景 | 预期行为 |
| --- | --- |
| 没有 bond | 设备只允许本地开启配对；Host 不冒充已连接 |
| 配对窗口超时 | 停止接受未绑定连接，保留原状态和旧 bond |
| 配对失败 | 终止连接，不保存 bond；窗口内可由 Host 有界重试 |
| 未加密写 RX | GATT 层拒绝，字节不得进入 NDJSON framer |
| BLE 半帧后断线 | 清空该 BLE peer 半帧；USB parser 不受影响 |
| Notify 未订阅 | 不开始 hello/status 会话，Host 给出明确错误 |
| USB 抖动小于 2 秒 | 保留 USB 状态，不切 BLE |
| USB 持续断开 | 宽限结束后通过 BLE hello + 完整 status 接管 |
| BLE 活动时 USB 恢复 | USB 完整快照 ACK 后原子切换，再关闭 BLE |
| BLE 活动时蓝牙关闭/越界 | 立即离线，恢复广播/扫描后重新握手和发快照 |
| BLE 连接时短按 K2 | 主动断开 BLE 并暂停自动重连；不影响 USB |
| BLE 暂停时短按 K2 | 解除暂停并广播，等待 Mac Host 发起连接 |
| 无 bond 时短按 K2 | 不开放配对，提示长按 2 秒 |
| K2 按住 2–5 秒后松开 | 开启 120 秒配对窗口 |
| K2 持续按住达到 5 秒 | 只清除全部 BLE bond，不先进入配对 |
| Mac 睡眠/唤醒 | 旧连接失效时清半帧；唤醒后重连并发送最新完整快照 |
| 重复配对 | 保留旧 bond 并拒绝；不自动删除安全状态 |
| 设备清 bond、Mac 未忽略 | 后续连接失败并提示用户完成双端清理 |
| Bleak 缺失或权限拒绝 | USB 继续工作；显式 BLE 命令非零退出 |
| 两个 transport 同时发状态 | 只允许优先级胜出的完整快照，回复不跨链路 |

## 11. 验收标准

### 11.1 功能

- K2 在 2–5 秒松开只打开一次 120 秒配对窗口，5 秒动作只清除全部 bond。
- macOS 与设备完成一次 LESC Just Works pairing，重启两端后 bond 仍可自动重连。
- K2 短按可以暂停当前 BLE 并在再次短按后恢复可连接广播；不能断开 USB。
- RX/TX 使用预留 UUID 和规定属性，最小 MTU 分片单元测试与真机实际 MTU 均通过。
- BLE 可完成 hello、九种状态、clear、ACK、ping/pong、error 和完整会话 replay。
- 顶栏只显示当前权威 transport；配对 UI 不改变 Agent 状态。
- `auto`、`usb`、`ble` 三种模式行为与第 7.3 节一致。
- 清除绑定和换 Mac 流程可重复完成，且不擦除无关 NVS 数据。

### 11.2 安全

- Legacy Pairing、固定 passkey、MITM 要求和调试密钥均关闭；只允许 LESC Just Works。
- 未加密或非当前 bond 的 Central 无法写 RX、订阅/接收 TX 或提交协议状态。
- 配对窗口外的新 Central 无法创建业务会话。
- bond 上限为 1，重复配对不能替换现有绑定。
- 日志、fixture 和诊断不包含 BLE 密钥、完整设备 ID 或原始 Agent 内容。

### 11.3 恢复与稳定性

- BLE 独立完成至少 20 次连接/断开循环，每次均重新 hello 并发送完整状态。
- release 固件完成至少 100 次 BLE 状态 smoke，协议异常、TX drop 和 UI 卡死均为 0。
- USB→BLE 与 BLE→USB 各完成至少 10 次切换，均无空白、旧状态回退或 ACK 串线。
- 覆盖最小/典型/最大分片边界、连续多帧、非法 UTF-8、超长帧和通知分片组合。
- 覆盖 Mac 睡眠/唤醒 3 次、Bluetooth 关闭/开启 3 次和至少 1 次距离断开恢复。
- 设备重启、Host 重启和 LaunchAgent 重启后均能恢复已绑定连接或给出明确离线原因。

### 11.4 性能与资源

- 第 9 节四项时延目标以可复核样本和统计摘要通过。
- BLE 活动、配对页、USB/BLE 切换和 fuzz 后 heap/stack 均高于安全线且无持续下降。
- dev、release、Unity test app 均从干净 build 目录成功构建。
- 0.2.0 全部 USB 发布基线回归通过，不因启用 NimBLE 破坏 CDC、触摸、动画或启动时间。

24 小时/10,000 次 BLE soak 和 LCD 光学延迟仍是可选扩展验证，不阻止 0.3.0 发布；上面的
100 次 BLE smoke、切换矩阵和睡眠/唤醒属于本版本发布门槛。

## 12. 兼容性规则

- 0.3 Host 连接 0.2 固件时继续使用 USB；不得假设存在 BLE capability。
- 0.2 Host 连接 0.3 固件时继续使用 USB，并忽略新增 capability/诊断字段。
- 0.3 Host 只有安装 `ble` extra 后才启用 BLE；USB-only 安装仍受支持。
- 固件降级到 0.2.0 时 BLE bond 可以留在 NVS 但不生效；重新升级不得因此擦除整个 NVS。
- 新增 optional capability 和诊断字段不提升协议主版本；改变 UUID、分片方式、状态字段语义
  或安全要求必须重新评审兼容性。
- dev profile 报告 `0.3.0-dev`，Host 开发版本使用 `0.3.0.dev0`；只有通过发布门槛的
  release profile 报告 `0.3.0`。

## 13. 发布前决策门

以下结果必须在全面实现前通过 Stage 0 真机 spike；失败时回到规格评审，不能静默降低安全
或删减恢复目标：

- ESP-IDF 5.5.3 NimBLE 与现有 TinyUSB/LVGL 在目标板上可同时运行且资源余量满足安全线；
- macOS CoreBluetooth 能在 K2 配对窗口内通过加密特征触发 LESC Just Works 并持久化 bond；
- LaunchAgent 在用户完成前台授权后可以稳定扫描、连接和接收 Notify；
- resolving/allow list 能在 macOS 使用私有地址时识别已绑定设备；
- 真机实际 ATT MTU 和 Write With Response/Notify 行为能满足 2 秒最大帧 ACK 目标。

工程拆分与阶段见 [IMPLEMENTATION.md](./IMPLEMENTATION.md)，可执行清单见
[TASKS.md](./TASKS.md)。
