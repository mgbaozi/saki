# Saki 路线图

本文件只记录 `0.2.0` 之后、尚未排入正式版本的工作。当前首版完成情况见
[0.2.0 任务清单](./versions/0.2.0/TASKS.md)。目标版本仅为规划建议，在开始开发时确认。

## BLE fallback（建议目标：0.3.0）

### B0.1 实现 NimBLE transport

- 使用 0.2.0 SPEC 预留的 Service/RX/TX UUID。
- 支持 Write With Response、Notify 和 MTU 分片。
- 复用同一 NDJSON parser，不分叉应用协议。

### B0.2 配对和绑定

- 使用 LE Secure Connections。
- Mac 端使用 `bleak` 可选依赖。
- 明确清除绑定、换 Mac 和恢复流程。

### B0.3 BLE 恢复和压力测试

- 覆盖 Mac 休眠/唤醒、距离断开、MTU 变化和连续消息。
- 断线后清除不完整分片。
- 测量从 USB 失效到 BLE 快照可见的切换时间。

## Wi-Fi fallback（建议目标：0.4.0）

### W0.1 配网与凭据存储

- 通过 USB 或 BLE 配置，不硬编码。
- 在 NVS 中安全存储并提供清除流程。

### W0.2 mDNS 和 TCP transport

- 使用 `_saki-agent._tcp.local` 和端口 8765。
- 复用同一 NDJSON parser。
- 多个连接并存时只允许一个授权 session 控制 UI。

### W0.3 配对令牌和 TLS 决策

- 无有效令牌不接受状态更新。
- 明确可信局域网与非可信网络的威胁模型。
- 决定非可信网络是否强制 TLS，以及证书或密钥轮换方式。

## 多传输仲裁（BLE/Wi-Fi 完成后）

### I1.1 实现优先级

- 默认优先级为 USB > BLE > Wi-Fi。
- 只有完成 hello 和完整 status 的候选 transport 才能接管 UI。

### I1.2 实现原子切换

- USB 断开使用短暂宽限，避免枚举抖动造成频繁切换。
- 高优先级恢复时先同步完整快照，再切换连接标识。
- 不混合不同 session 的状态或 ACK。

### I1.3 切换矩阵测试

- 覆盖 USB↔BLE、USB↔Wi-Fi、BLE↔Wi-Fi。
- 每个方向覆盖活动、等待和终态。
- 切换时不出现空白、旧状态回退或重复 ACK 混淆。

## 增强功能（未排期）

- 多 Agent 摘要和子 Agent 数量提示。
- 可配置主题、亮度和空闲策略。
- 安全的有限交互，例如提醒用户处理批准，但不代替用户批准高风险操作。
- OTA 更新、诊断页和设备设置页。

## 可选工程验证（不阻塞当前版本发布）

- 24 小时且至少 10,000 次状态更新的长时间 soak。
- USB 模式下的 Mac 睡眠/唤醒恢复矩阵。
- 使用高速摄影或光学探头测量 Agent hook 到 LCD 最后像素可见变化的端到端延迟。
