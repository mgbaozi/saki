# Saki 0.3.0 BLE 用户流程

> 状态：历史开发指南；0.3.0 未单独发布，BLE 能力已并入 0.4.5
> 稳定 USB 指南：[0.2.0 USER_GUIDE](../0.2.0/USER_GUIDE.md)
> 开发规格：[SPEC.md](./SPEC.md)

本文保留 0.3.0 开发阶段的 BLE 用户旅程。当前安装以根 README 和 0.4.5 文档为准；完整
真机矩阵统一在 1.0 候选执行。

## 1. 预期能力

0.3.0 默认仍优先通过 USB 连接。完成一次 BLE 配对后，USB 不可用时 Host 会自动使用 BLE，
USB 恢复后再切回。BLE 只传输与 USB 相同的脱敏状态快照，不传输完整对话或隐藏推理。

## 2. 安装方式

USB-only 安装保持不变。需要 BLE 时安装可选依赖：

```zsh
host/.venv/bin/pip install -e 'host[ble]'
```

开发检查可能使用：

```zsh
host/.venv/bin/pip install -e 'host[dev,ble]'
```

当前 Bleak 兼容范围为 `>=3.0.2,<4`；最终发布前仍需在目标 macOS 上复核。

## 3. 首次配对流程

首次配对必须在前台完成，不能交给 LaunchAgent：

1. 用 `scripts/saki-service.zsh stop` 停止常驻 Host。
2. 按住设备 K2；满 2 秒后设备提示“松开进入配对 / 继续按住清除”。
3. 在达到 5 秒前松开 K2，确认屏幕出现 120 秒配对倒计时。
4. 在 Mac 前台运行 `host/.venv/bin/saki-host ble pair`。
5. macOS 请求 Bluetooth 权限或系统配对授权时允许访问。
6. 等待 CLI 完成加密 GATT、hello 和完整状态验证。
7. 用 `scripts/saki-service.zsh start` 恢复常驻 Host。

设备一次只保存一个 Mac。无需在设备上确认数字；用户取消系统配对或 120 秒窗口超时都不会
保存绑定。按住达到 5 秒属于“清除全部绑定”，不会同时触发配对。

## 4. 日常使用

默认服务模式为 `auto`：

- `USB`：有线正常，优先使用 USB；
- `BLE`：USB 不可用，已绑定 BLE 正常；
- `OFFLINE`：两个 transport 都未完成握手和完整快照。

切换 transport 不会清空任务，也不会重播完成/失败动画。离线时设备保留并淡化最后快照，
恢复后必须收到新的完整状态才移除离线标记。

K2 短按用于临时控制 BLE：

- BLE 已连接：短按会断开当前连接，并暂停 BLE 自动重连；USB 不受影响。
- BLE 已断开且已有 bond：再次短按取消暂停并重新开放只允许已绑定 Mac 的广播；Saki 是
  BLE Peripheral，真正发起扫描和连接的是 Mac Host。
- 尚无 bond：短按不会开放配对，只提示“长按 K2 2 秒进入配对”。
- 手动暂停只在本次运行有效；设备重启后恢复已绑定 BLE 的自动重连策略。

诊断命令：

```zsh
host/.venv/bin/saki-host doctor --transport auto
host/.venv/bin/saki-host doctor --transport ble
host/.venv/bin/saki-host ble list
host/.venv/bin/saki-host ble cycle --count 20
host/.venv/bin/saki-host send --transport ble --state idle
host/.venv/bin/saki-host replay protocol/fixtures/v1/sessions/basic.ndjson --transport ble
host/.venv/bin/saki-host ble fuzz --count 40 --seed 20260907
host/.venv/bin/saki-host ble soak --count 100 --duration 10 \
  --report artifacts/ble-smoke.json
```

强制 BLE 的 `send`/`replay` 以及 `ble cycle`、`ble fuzz`、`ble soak` 会独占 BLE 连接，应先
停止常驻 Host。`send` 和 `replay` 未指定 `--transport` 时继续默认使用 USB。fuzz 使用有界、
脱敏且可复现的非法输入集，完成后验证同一连接恢复；soak 报告包含 ACK 延迟、诊断增量和
资源安全线结果。以上命令不会清除 macOS 或设备上的 bond，`artifacts/` 报告不提交仓库。

## 5. 清除绑定与换 Mac

清除绑定需要同时处理设备和 macOS：

1. 连续按住 K2；满 2 秒后继续按住，达到 5 秒时设备立即清除全部 BLE bond，无需确认。
2. 在 macOS 系统设置的 Bluetooth 页面找到 Saki，选择“忽略此设备”。
3. 如需换 Mac，在新 Mac 上重新执行首次配对流程。

Bleak 在 macOS 上没有显式 unpair API，因此 Host CLI 只能检查状态并给出系统设置指引，不能
代替用户删除 macOS 配对记录。只清一端会导致后续加密/绑定校验失败，但不会自动覆盖旧 bond。

## 6. 常见问题

### BLE 命令提示可选依赖缺失

安装 `host[ble]`。USB 模式仍应正常工作；显式 BLE 命令应返回非零退出码。

### macOS 没有弹出配对窗口

- 确认设备仍在 120 秒配对窗口内；
- 确认 K2 是在 2–5 秒之间松开，而不是一直按到 5 秒触发清除；
- 确认使用前台命令而不是 LaunchAgent；
- 运行 BLE doctor 检查 Bluetooth 是否开启、权限是否被拒绝；
- 如果两端残留不一致的 bond，按第 5 节同时清除。

Bluetooth 关闭时，BLE doctor 应以非零状态退出并显示
`BLE scan failed: Bluetooth is powered off`；打开 Bluetooth 后重试即可。`auto` 模式仍会在
USB 可用时继续使用 USB。

### BLE 已连接但屏幕仍显示 OFFLINE

GATT 连接不等于 Saki 已连接。必须依次完成加密与绑定、TX Notify 订阅、协议 hello 和完整
status。
doctor 应指出失败发生在哪一步。

### USB 拔掉后没有切换 BLE

- USB 有 2 秒防抖宽限；
- 确认已完成 bond，Host 安装了 BLE extra 且运行 `auto`；
- 检查 macOS Bluetooth/权限；
- 确认没有另一个 Host 占用同一设备。

### USB 恢复后仍显示 BLE

Host 必须先在 USB 上完成 hello 和完整状态 ACK 才会切换。USB 端口占用、设备处于 ROM
下载模式或 CDC 未枚举时不会强制关闭仍正常工作的 BLE。

## 7. 隐私与安全

- 配对只在用户按住 K2 后由设备本地开启的 120 秒窗口内进行。
- 使用 LE Secure Connections Just Works、加密 GATT 和 bond，不使用 Legacy Pairing 或固定
  PIN，也不要求数字比对。
- 这套模型以“能接触设备并按 K2”作为配对授权；它不提供数字比对的 MITM 防护。配对窗口
  开启时应避免附近陌生设备抢先连接，完成后窗口会立即关闭。
- 广播不包含任务内容、完整设备 ID、Mac 身份或 bond 信息。
- BLE 密钥不写 Host/firmware 日志。
- BLE bond 由设备 NVS 和 macOS 系统安全存储分别管理，仓库与发布包不包含这些数据。
