# Saki 实施任务清单

> 文档版本：0.2.0
> 状态：Release candidate
> 产品规格：[SPEC.md](./SPEC.md)
> 工程设计：[IMPLEMENTATION.md](./IMPLEMENTATION.md)

## 1. 使用方式

状态标记：

- `[ ]` 未开始
- `[~]` 进行中
- `[x]` 已完成并验证
- `[!]` 被明确问题阻塞
- `[-]` 原计划步骤被后续集成验证取代，不再单独执行

完成任务时，在任务下补充验证命令、真机结果或产物路径。仅写完代码但没有完成对应验证，不能标记为 `[x]`。

### 1.1 全局完成要求

- 不直接修改外部厂家示例目录中的原始文件。
- 不运行 `idf.py set-target` 覆盖厂家板级配置，除非已完成配置迁移并审查差异。
- 不把真实凭据、完整 Agent 对话、用户主目录或未脱敏协议捕获提交到 Git。
- 烧录前确认动态串口路径和目标开发板。
- 涉及协议的任务同时更新 Schema、fixtures、Host 测试和固件测试。
- 涉及 UI 的任务必须真机验证 320×240 布局，不能只依赖截图或编译通过。

## 2. 里程碑

| 里程碑 | 范围 | 完成条件 |
| --- | --- | --- |
| M0 基线可用 | E0 | 工具链、LVGL、触摸和厂家 USB CDC 均完成真机验证 |
| M1 静态状态屏 | R0、F0、F1、F2 | 固件能脱离 Mac 轮播全部 UI 状态 |
| M2 有线协议贯通 | P0、F3、F4、H0、H1 | Host demo 可通过 USB 控制状态屏 |
| M3 真实 Agent 同步 | H2、A0、I0 | 首个真实 Agent 端到端工作 |
| M4 第一版发布 | Q0、R1 | 通过全部验收并生成可复现固件产物 |

## 3. E0：环境和厂家基线

### [x] E0.1 安装并验证 Ninja

- 使用 ESP-IDF 工具管理器或 Homebrew 安装 Ninja。
- 验证 `ninja --version`。
- 激活 ESP-IDF 后再次确认 Ninja 在 PATH 中。

完成证据：记录 Ninja 版本和安装方式。

验证记录（2026-09-02）：使用 ESP-IDF `idf_tools.py` 安装 Ninja 1.12.1；激活 ESP-IDF 后已进入 PATH。

### [x] E0.2 固化 ESP-IDF 环境脚本

- 创建 `scripts/env-idf.zsh`。
- 选择 Python 3.12，再 source ESP-IDF 5.5.3。
- 检查 `python3`、`idf.py`、`esptool.py` 和 `ninja` 版本。
- 任一版本或路径不符时返回非零退出码。

验证：在新的非交互 zsh 中执行脚本，不能命中系统 Python 3.9。

验证记录（2026-09-02）：已创建 `scripts/env-idf.zsh`，校验 Python 3.12 和 ESP-IDF 5.5.3，并检查 Ninja、esptool；ESP-IDF 和可选 pyenv 根目录均支持由环境变量覆盖，仓库不写入用户绝对路径。

### [x] E0.3 再次核验原厂 Flash 备份

- 检查备份文件存在且大小为 16 MiB。
- 核对 SHA-256：`0600c3140d63a197c87e36f1a7cd207f2af0277af1082ca32b6f56d8cd86477e`。
- 确认备份未被加入当前 Git 仓库。

验证：把只包含大小、哈希和时间的结果记录到本地测试日志，不复制备份文件。

验证记录（2026-09-02）：备份位于项目外，大小为 16,777,216 bytes；SHA-256 为 `0600c3140d63a197c87e36f1a7cd207f2af0277af1082ca32b6f56d8cd86477e`，与交接记录一致。

### [-] E0.4 构建厂家 LCD 示例副本

- 把 `08_lcd` 复制到临时或项目外独立验证目录。
- 保留厂家 `sdkconfig`，直接执行 `idf.py build`。
- 保存 `idf.py size` 摘要和所有构建警告。

验证：从干净 build 目录连续构建两次成功。

### [-] E0.5 真机验证 LCD

- 动态确认 `/dev/cu.usb*` 烧录端口。
- 烧录 LCD 示例并确认背光、方向、颜色和刷新正常。
- 记录固件版本、串口和观察结果。

前置：E0.3、E0.4。

### [-] E0.6 真机验证触摸和 LVGL

- 分别构建/烧录 `14_touch` 和 `01_lvgl_transplant` 副本。
- 验证触摸方向、边界坐标和 benchmark。
- 运行至少 30 分钟，观察是否花屏、卡死或触摸漂移。

前置：E0.5。

### [-] E0.7 真机验证厂家 USB CDC

- 构建/烧录 `23_usb_uart` 副本。
- 在 Mac 上记录枚举前后的 VID/PID、Product String、Serial String 和端口名。
- 验证 DTR、双向回显、拔插和重新枚举。
- 确认应用 CDC 是否与烧录/monitor 端口冲突或切换。

前置：E0.3。

### [x] E0.8 形成基线结论

- 汇总 LCD、触摸、LVGL、USB CDC 的已验证行为。
- 把对项目结构或调试方式有影响的结论回写 `IMPLEMENTATION.md`。
- 关闭所有基线阶段遗留的不确定项，或明确列为 blocker。

完成里程碑：M0。

基线结论（2026-09-04）：E0.4–E0.7 原计划分别烧录厂家示例，实际开发选择保留 BSP 后
直接在集成固件中验证。LCD 320×240 横屏、LVGL、CHSC5432 触摸、AW9523B、TinyUSB CDC、
DTR 和重新枚举均已由后续真机与 Unity 测试覆盖；未执行的独立示例步骤不再补做。

## 4. R0：仓库骨架

### [x] R0.1 创建工程目录和根说明

- 创建 `firmware/`、`host/`、`protocol/`、`scripts/`、`artifacts/`。
- 创建根 `README.md`，包含项目目标、快速入口和文档链接。
- 创建 `.gitignore`，覆盖 ESP-IDF build、Python venv/cache、固件二进制、协议捕获和 macOS 文件。

验证：`git status` 只出现应提交的源码和文档。

### [x] R0.2 引入厂家 LVGL 工程副本

- 以 `01_lvgl_transplant` 为 `firmware/` 基线。
- 保留 `sdkconfig`、分区表、依赖锁和 BSP。
- 删除 benchmark 之前先完成一次未修改基线构建。
- 不拷贝旧 build 目录和编辑器缓存。

前置：E0.6。

验证记录（2026-09-04）：已从厂家 LVGL 示例提取并记录实际使用的 BSP，ESP-IDF 5.5.3
dev/release 均可构建；集成固件已完成 LCD、触摸、USB CDC 和 UI 真机验证。

### [x] R0.3 记录 BSP 来源

- 创建 `firmware/components/vendor_box3_bsp/ORIGIN.md`。
- 记录来源绝对路径、示例名称、拷贝日期、原文件列表和修改摘要。
- 保留原版权头；不进行与项目无关的格式化。

### [x] R0.4 固定依赖版本

- LVGL 固定 8.4.0。
- TinyUSB 接入时固定 `esp_tinyusb` 和底层 tinyusb 已验证版本。
- 提交 `dependencies.lock`。
- 记录 ESP-IDF 5.5.3 与厂家锁文件中 5.5.4 的实际兼容结论。

验证记录（2026-09-02）：`dependencies.lock` 固定 ESP-IDF 5.5.3、LVGL 8.4.0、`esp_tinyusb` 2.2.1 和底层 TinyUSB `0.21.0~1`；已完成干净构建和真机启动。

### [x] R0.5 建立 build profile

- 把真机验证过的厂家配置保存为 `config/sdkconfig.vendor`，再提取 `sdkconfig.defaults`。
- 创建 dev/release defaults，保持板级 Flash、Octal PSRAM、分区和 LCD 设置一致。
- dev 允许诊断计数和 UI demo；release 禁止完整协议日志。
- dev/release 分别使用 build 目录内独立生成的 `SDKCONFIG`，不能被根目录旧 `sdkconfig` 覆盖。
- 使用独立 build 目录验证两个 profile。

前置：R0.2、M0。

验证记录（2026-09-03）：已将厂家板级基线、公共 Saki 配置及 dev/release 差异拆为三个有序 defaults 层，生成配置分别位于 `build-dev/sdkconfig` 与 `build-release/sdkconfig`。固件版本由 CMake profile 注入，dev=`0.2.0-dev`、release=`0.2.0`；Host 开发版为 `0.2.0.dev0`。两套配置均从清空后的独立 build 目录成功构建；release 使用 size optimization、Warning 日志、silent assertions，关闭 TinyUSB debug、Saki/UI/LVGL demos、app trace 与 Unity test runner，且 CMake 禁止 release 误开 Saki UI demo。

## 5. P0：协议契约与 fixtures

### [x] P0.1 创建 v1 JSON Schema

- 定义 envelope、hello、status、ack、ping/pong、clear 和 error。
- 对必需字段、类型、枚举、字符串长度和进度范围编码约束。
- 允许未知额外字段，保持向前兼容。

验证：Schema 自身通过 meta-schema 校验。

实现记录（2026-09-02）：已创建 envelope、status 和 messages Draft 2020-12 Schema，并加入自动校验测试。

### [~] P0.2 创建有效 fixtures

至少覆盖：

- 九种协议主状态；
- idle 最小消息；
- 中文和英文；
- 三种 progress mode；
- hello、ACK、重复 ACK、ping/pong、clear 和 error；
- 2048 字节以内的最大合法消息。

### [~] P0.3 创建无效 fixtures

至少覆盖：

- 非法 JSON、错误版本、缺字段、错误类型；
- 未知主状态、非法进度、负耗时；
- 超长字符串、超长帧、非法 UTF-8；
- 握手前 status、旧 seq、重复 seq。

### [~] P0.4 创建完整会话 fixtures

- 正常工作→完成；
- 工作→等待批准→恢复→完成；
- 断线重连和新 session；
- ACK 丢失重发；
- 乱序和重复状态。

所有 fixture 必须人工检查脱敏。

### [ ] P0.5 协议文档一致性测试

- 从 `SPEC.md` 示例提取或复制为测试 fixture。
- 测试所有文档示例符合 Schema。
- 协议字段修改时 CI 能发现文档和实现不一致。

## 6. F0：固件基础与状态模型

### [x] F0.1 拆分 `app_main` 和组件

- 保持厂家初始化顺序，拆出 model、protocol、transport 和 UI 组件。
- `app_main` 只负责编排和错误处理。
- 每次拆分后保持 LCD/LVGL 基线可构建。

实现记录（2026-09-02）：已拆出 `saki_model`、`saki_ui`、`saki_protocol`、`saki_usb` 和最小 `app_main`；协议层不 include TinyUSB，整合构建和真机通信通过。

### [x] F0.2 实现固定状态结构

- 定义九种协议状态和设备本地状态。
- 定义固定上限 task/activity/progress/agent 字段。
- 实现 init、clear、完整 replace 和 copy。
- 记录接收 tick，用于本地 elapsed 递增。

实现与验证记录（2026-09-03）：九种状态、三种进度模式、固定容量字段、初始化和复制均已实现。固件 `0.1.3` 为快照加入本地单调时钟接收基准；活动及等待状态每秒计算并刷新执行时间，新事件替换 Host 基准完成校准，终态、idle 和断线快照冻结。真机 Unity 用例覆盖递增、时钟回退、终态/断线冻结和 `uint64_t` 饱和，验证通过；UTF-8 防御独立由 F0.3 跟踪。

### [x] F0.3 实现 UTF-8 安全函数

- 在字符边界截断。
- 检测非法序列并替换或拒绝。
- 覆盖 ASCII、三字节中文、混合文本和边界长度测试。

实现与验证记录（2026-09-03）：`saki_model` 提供严格 UTF-8 标量值校验和按字节容量安全复制；拒绝过长编码、孤立 continuation、surrogate、截断序列及超过 U+10FFFF 的输入，合法文本截断时回退到完整 code point 边界。协议层在 cJSON 解析前校验整帧，已知字段只接受未截断的契约长度，非法编码返回 `invalid_json` 且不提交 UI。Host 同时归一化未配对 surrogate。当前完整 Host 套件 56 项测试通过；目标设备 UTF-8 Unity 用例及完整 10 项套件得到 `0 Failures`。

### [x] F0.4 实现固件内部队列

- UI queue 长度 1，采用 latest snapshot 覆盖语义。
- TX queue 初始长度 8。
- 定义 queue full 指标和失败行为。

实现记录（2026-09-02）：UI queue 采用长度 1 和 `xQueueOverwrite`；USB TX queue 使用固定
容量并记录 drop 诊断。协议、fuzz、心跳和真机恢复测试均覆盖这两条数据路径。

## 7. F1：设备 UI

### [x] F1.1 建立 theme 和布局常量

- 集中定义颜色、字号、间距、圆角和动画时间。
- 完成 320×240 主屏静态布局。
- 确保状态不能只靠颜色区分。

### [x] F1.2 实现启动、等待连接和空闲页

- 初始化成功和失败路径可见。
- 重启后不展示旧任务。
- 空闲页支持背光降级。

### [x] F1.3 实现活动状态页

- starting、thinking、working。
- 标题两行、动作两行、耗时和 transport pill。
- 只更新变化控件，不重建整个页面。

### [x] F1.4 实现等待状态页

- waiting_user 和 waiting_approval 有不同文案。
- 琥珀提示可见但不快速闪烁。
- detail 最多三行并安全省略。

### [x] F1.5 实现终态页

- completed、failed、cancelled。
- 终态耗时停止。
- failed 优先显示错误摘要。

### [x] F1.6 实现断线覆盖层

- 保留并淡化最后快照。
- 显示断线和最后更新时间。
- 只有新 session 的完整 status 才能移除。

### [x] F1.7 实现 progress

- `none` 不占用空进度条。
- `indeterminate` 使用轻量动画。
- `determinate` 显示 0–100% 并钳制防御性异常值。

实现记录（2026-09-03）：0.1.5 首次将 `indeterminate` 从固定 35% 改为 LVGL range bar 的 24% 宽短条，真机观察发现单向扫过后跳回左端较生硬。0.1.6 改为每程 1.1 秒的 ease-in-out 平滑往返，移除边界跳变和停顿；连续 hook 更新不会重启动画。切换到 `determinate`、`none` 或断线时删除无限动画并恢复普通 bar，防止后台动画残留。0.1.6 验证镜像已构建、写入并由 `doctor` 确认版本和协议健康，用户已确认改进后的动态效果自然。

### [x] F1.8 实现 UI demo

- 轮播九种状态、三种进度和设备断线状态。
- 覆盖长中英文、空字段和不支持字形。
- 可通过编译配置关闭。

离线记录（2026-09-03）：320×240 主界面、九种状态、三种进度、断线页和 3 秒轮播已编译进固件；现由 `CONFIG_SAKI_UI_DEMO` 控制，普通 dev 默认关闭、可通过 menuconfig 手动启用，release 误启用时构建失败。横屏布局和中文字体已真机确认，断线时现保留最后一个实时快照并将连接标记为离线。

首次烧录记录（2026-09-02）：bootloader、partition table 和 `saki.bin` 写入后均通过哈希校验。启动日志确认 8 MiB PSRAM 测试通过、AW9523B ID 为 `0x23`、触摸控制器返回 `CTP:0x5`、`saki_ui` demo 任务启动；屏幕视觉结果待人工确认。

分辨率修正记录（2026-09-02）：首次画面只刷新 `240×240`，原因是厂家 `SPI_LCD_TYPE` 未定义时选择方屏 fallback。BSP 现显式定义 `SPI_LCD_TYPE=1`，保持原横屏旋转并使用 `320×240`；重新构建、烧录、启动日志和用户视觉确认均通过。

## 8. F2：触摸、字体和背光

### [x] F2.1 选择并生成字体

- 统计固定中文 UI 文案。
- 选择动态文本可接受的 CJK 字符范围。
- 记录字体来源、许可证、生成命令和固件体积。
- 不支持字符必须稳定显示替代字形。

实现记录（2026-09-02）：以 SIL OFL 1.1 的 Noto Sans CJK SC Regular 生成 16 px、2 bpp、压缩的完整 GB2312 子集，共 7,445 个符号（含 6,763 个汉字），并作为 Montserrat 14/16 的 fallback。生成脚本固定 `lv_font_conv` 1.5.3、校验源字体 SHA-256，许可证与生成说明随代码保存。加入字库后 `saki.bin` 从约 552 KiB 增至约 990 KiB，2 MiB app 分区仍余 50%；编译已通过，真机视觉结果待确认。

真机记录（2026-09-03）：烧录后用户确认中文标题和动作摘要显示正常，未再出现缺失字形方框。

### [x] F2.2 实现详情视图

- 单击任务/动作区域切换摘要和详情。
- 15 秒无操作自动返回。
- 详情仍执行隐私和长度限制。

实现记录（2026-09-03）：新增独立 `saki_ui_policy` 状态机和厂家触摸坐标 release-edge
处理。亮屏点击任务/动作区域可展开 106 px 的详情卡，再次点击返回；15 秒无操作自动返回，
暗屏首次点击只唤醒。详情仍来自已经过 Host 隐私过滤和固件长度校验的快照。策略边界已在
目标设备的 Unity test app 中通过；开发固件的中文详情展开、再次点击返回和 15 秒自动返回
已完成用户视觉确认。

### [x] F2.3 实现亮度策略

- 活动 80%、空闲 5 分钟后 35%、断线 1 分钟后 20%。
- 暗屏首次触摸只唤醒。
- 新状态恢复正常亮度。
- 确认 AW9523B 接线能力，不能假定背光是 ESP32 直连 GPIO。

实现记录（2026-09-03）：dev 使用 30 秒空闲/15 秒断线阈值便于真机验收，release 使用
规格要求的 5 分钟/1 分钟；视觉亮度均为 80%/35%/20%。目标板 `LCD_BL` 由 AW9523B
P1.0 提供低有效使能，但实测 constant-current DIM 模式下任何非零电流值都保持全亮，说明
该脚不是可用的 LED 电流调光通路。0.2.0 因此保留厂家 GPIO 使能并用 LVGL 顶层黑色遮罩
实现稳定的感知亮度。该方式不节省背光功耗，硬件节能调光明确延期；策略与透明度映射已
纳入 Unity 真机测试。开发固件的断线降暗、首次点击只唤醒且不展开详情已完成用户视觉确认。

### [ ] F2.4 UI 真机稳定性

- demo 连续运行 1 小时。
- 检查堆、最小栈水位、触摸、动画和花屏。
- 保存 `idf.py size` 和运行指标。

完成里程碑：M1。

## 9. F3：固件协议层

### [x] F3.1 实现有界 NDJSON 分帧器

- 兼容 LF/CRLF 和半帧。
- 支持一次 read 中多帧。
- 超过 2048 字节后丢弃到下一 LF 并恢复。
- 不为远端长度动态分配无限内存。

### [x] F3.2 实现 hello 和握手状态机

- v1 版本检查。
- 返回 device id、firmware version、screen 和 capabilities。
- 握手前拒绝 status/clear/ping 以外未允许消息。
- 3 秒握手超时。

### [x] F3.3 实现 status 完整快照解析

- 验证字段类型、枚举、范围和 UTF-8。
- 缺失可选字段清除旧值。
- cJSON 数据不跨任务保留。
- 成功入 UI queue 后才 ACK。

### [x] F3.4 实现 session 和 seq

- 新 session 重置 seq 基准。
- 重复/旧 seq 返回 `applied=false`。
- uint32 回绕比较行为有测试。
- 断线时清空握手和半帧状态。

### [x] F3.5 实现 clear、ping/pong 和 error

- clear 与完整 idle 状态结果一致。
- pong 返回 uptime 和 last_seq。
- 错误消息不回显完整敏感输入。
- 连续 5 个非法帧回到等待握手。

整合记录（2026-09-02）：以上能力均已进入 `saki_protocol` 并通过真实 hello、5 个连续 status/ACK 和 ping/pong；仍需补设备握手 watchdog、uint32 回绕和 C 侧 Unity/非法 fixture 测试后才能完成。

心跳实现记录（2026-09-03）：握手成功和每条通过会话校验的 status/clear/ping 都刷新设备端活动时间；连续 15 秒无有效消息时清除 handshake/session/seq/半帧状态、递增 heartbeat timeout 计数，并通知 UI 保留最后实时快照但标记离线。恢复通信必须执行新 hello 和完整 status。固件版本升至 `0.1.1`；冻结 Host 17 秒且保持串口/DTR 打开的真机测试中，恢复后的首个 ping 收到 `not_handshaken`，Host 随即重连、重新握手并从新会话 `seq=1` 恢复状态，验证通过。

### [x] F3.6 实现协议诊断计数器

- valid、invalid、oversize、old_seq、UI overwrite、TX drop、heartbeat timeout。
- uint32 饱和或回绕行为明确。
- release 模式不输出完整 frame。

实现与验证记录（2026-09-03）：已加入 `saki_protocol_diagnostics_t`，覆盖 valid、invalid、oversize、old seq、UI pending overwrite、TX drop 和 heartbeat timeout；所有字段使用 `uint32_t` 饱和递增。固件 `0.1.2` 通过 `pong.diagnostics` 对 Host 暴露计数，Host 仅在异常值变化时记录汇总且兼容无该字段的旧固件。主固件编译通过并已刷入真机，`saki.bin` 为 `0xf7ac0` bytes，app 分区仍剩余 50%；诊断字段、异常计数和饱和行为随 F3.7 的真机 Unity 测试全部通过。

### [x] F3.7 固件协议单元测试

- 在 `firmware/test_app` 使用 ESP-IDF Unity。
- 覆盖 `protocol/fixtures/v1` 的 C 侧等价输入。
- 每个错误码至少有一个测试。

实现与验证记录（2026-09-03）：已建立 `firmware/test_app`、`scripts/build-firmware-tests.zsh` 和真机 runner，ESP-IDF 自动纳入 `saki_protocol/test`。最初 10 个 Unity 用例覆盖 LF/CRLF、半帧/多帧、超长恢复、中文完整状态、UTF-8 校验/边界截断、旧 seq、uint32 回绕、5 次非法帧重置、全部稳定错误码、busy/TX/UI 计数、心跳超时、饱和计数以及本地执行时间语义。首次真机运行暴露测试夹具约 7 KiB 超过默认 3.5 KiB 主任务栈，现已将大夹具移至静态内存并把测试栈提高至 16 KiB。新增第 11 个 `pong.runtime` provider/序列化用例后，测试镜像为 194,272 bytes、SHA-256 `b49cb83ed16650e24190c919afc55a1ca7c45a409b8f982cfaeaaed6269fc7c4`；目标设备完整运行得到 `11 Tests 0 Failures 0 Ignored / OK`。

## 10. F4：USB transport

### [x] F4.1 定义 transport 接口

- start、stop、connected、read、write、name/context。
- 协议层不 include TinyUSB 头文件。
- 为未来 BLE/TCP 保留实现空间，但不提前实现无线逻辑。

### [x] F4.2 集成 TinyUSB CDC ACM

- 固定已验证组件版本。
- 设置 Product String `Saki Agent Display`。
- 使用 eFuse MAC 生成稳定 Serial String。
- CDC buffer 和 task 配置有依据并记录。

### [x] F4.3 实现 RX 数据路径

- 回调只写 StreamBuffer。
- 处理 DTR connect/disconnect。
- 新 DTR 清理旧半帧和握手状态。
- 压力下不在回调内阻塞。

### [x] F4.4 实现 TX 数据路径

- 单一任务串行写 ACK/pong/error。
- 明确 partial write 和超时行为。
- 队列满时记录 drop，并优先重要回复。

### [x] F4.5 隔离业务 CDC 和日志

- 验证 `printf`/ESP_LOG 不进入 NDJSON 流。
- 定义 debug profile 的日志去向。
- 发布构建默认只保留安全错误指标。

### [x] F4.6 真机 USB 恢复测试

- 连续 20 次打开/关闭 DTR。
- 连续 3 次物理拔插。
- 每次重连都要求 hello + 完整 status。
- 记录端口重枚举时延。

真机记录（2026-09-03）：设备以 VID/PID `303a:4001`、Product `Saki Agent Display` 枚举；业务 CDC 只返回协议帧。首版 4 KiB worker 在首个 status 后复位，提升至 10 KiB 后完整序列和持续心跳稳定通过。新增 `saki-host serial cycle` 后，0.1.4 真机连续 20 次 DTR 打开、新 session hello 和关闭全部成功，无失败；握手延迟最小 156.4 ms、平均 160.6 ms、最大 164.4 ms。当前验收范围的 3 次物理拔插全部重新枚举，并由常驻 Host 以新 session 的 `seq=1` 恢复完整状态；重枚举到 Host 完成握手的耗时分别为 448.4、1092.4、631.6 ms，平均 724.1 ms，stderr 为空。

## 11. H0：Host 工程基础

### [x] H0.1 建立 Python package

- 创建 `pyproject.toml` 和 `src` layout。
- 运行时依赖只有 pyserial。
- dev extras 包含 pytest、pytest-asyncio、ruff 和 Schema 校验工具。
- 提供 `saki-host` console entry point。

验证记录（2026-09-02）：Python 3.12 venv 已安装 editable package、runtime/dev extras，CLI 可执行。

### [x] H0.2 实现 Host 数据模型

- AgentEvent、StateSnapshot、ProtocolMessage。
- 字段限制与 SPEC 一致。
- 序列化时不输出无意义的 null 字段。

### [x] H0.3 实现协议 codec

- 严格一行一对象、UTF-8、末尾 LF。
- 生成 id/session/seq。
- 解析 hello、ACK、pong、error。
- 单元测试使用 protocol fixtures。

### [~] H0.4 实现配置和日志

- 默认配置无需文件即可运行 demo。
- 支持日志级别和脱敏 protocol trace。
- 不把端口路径、凭据或任务文本无条件写入持久日志。

## 12. H1：Host 串口与会话

### [x] H1.1 实现串口枚举

- 使用 pyserial list_ports。
- 优先 `/dev/cu.*`。
- 显示 VID/PID、Product、Serial 和路径。
- 单元测试用模拟 port records。

### [x] H1.2 实现握手确认发现

- Product String 匹配后仍执行 hello。
- 描述信息缺失时允许受控 probing。
- 3 秒超时后关闭错误候选端口。
- 只缓存 USB Serial identity。

### [x] H1.3 实现 asyncio serial transport

- 使用专用工作线程或 `asyncio.to_thread()` 承载 pyserial 阻塞 read/write，不在 event loop 中阻塞。
- 支持有界 RX/TX 队列、取消和安全关闭。
- 端口消失后向 session 报告明确原因。

### [x] H1.4 实现 ACK 和重发

- 同时只允许一个状态 ACK 在途。
- 1 秒超时，最多重发 2 次，保留原 id/seq。
- 新状态在等待期间合并为 latest snapshot。
- 三次失败后重连。

### [x] H1.5 实现心跳和重连

- 每 5 秒 ping。
- 追踪 pong 延迟和 last_seq。
- 重连后只发送最新完整状态。
- 采用有上限退避，USB 重插后快速恢复。

优化记录（2026-09-03）：初版只在串口读写或 5 秒心跳失败后进入 10 秒快速窗口，首轮真机拔插虽出现 `0.10s fast-window`，但设备重枚举到 Host 连接仍耗时 1541 ms，根因是旧串口失效发现过晚。随后增加已连接 `/dev/cu.*` 节点的 100 ms 存在性检查和两次缺失防抖；第二轮真机日志确认按 `serial device disappeared → 0.10s fast-window → connected → seq=1` 路径恢复，未产生 stderr。外部计时进程已结束，因此该轮不记录不可靠的精确毫秒值。

### [x] H1.6 实现 CLI doctor/list/demo/send/replay

- `doctor` 能解释“无端口、端口不是 Saki、握手失败、版本不兼容”。
- `demo --cycle` 覆盖九种状态。
- `replay` 只读取脱敏 fixture。
- 命令退出码可用于自动测试。

整合记录（2026-09-03）：`doctor`、`serial list/demo/cycle`、`send`、`replay` 和 `serve` 已实现；阻塞 pyserial 操作经 `asyncio.to_thread()` 执行，状态单在途、1 秒 ACK/最多两次重发、latest snapshot 合并、5 秒心跳和有上限的两阶段重连已接入。`serial cycle` 已完成 20 次真机 DTR 独立会话验证。`doctor` 能区分无串口、非 Saki/ROM 端口、端口占用、握手失败和协议版本不兼容；真机读取到设备元数据、320×240、握手和 ping 延迟，全部设备诊断计数为 0。`send` 真机中文完整状态得到 ACK；`replay` 仅接受仓库脱敏 fixture，重新校验 status/clear 并用新 session/id/seq 发送，三步 basic fixture 真机全部 applied。CLI 参数错误和拒绝场景返回非零退出码，当前完整 Host 套件为 56 项。受控 probing 和常驻服务 pong 延迟指标留在后续传输观测任务中。

完成里程碑：M2。

## 13. H2：Host 状态处理

### [-] H2.1 实现 adapter 基础接口和 mock adapter

- async iterator 输出 AgentEvent。
- mock 支持正常、等待批准、失败和取消场景。
- adapter 停止不直接终止串口 session。

范围调整（2026-09-04）：0.2.0 只接入 Codex，使用 `codex_hooks` 的纯映射函数作为事件源
边界，并由 `demo`/合成 hook payload 提供离线场景；暂不增加只有一个实现的抽象基类。
第二种 Agent 接入时再提取公共 adapter 接口。

### [x] H2.2 实现 normalizer

- 把 AgentEvent 映射到九种主状态。
- 把工具事件映射到有限 activity.kind。
- 不把隐藏推理内容放入 snapshot。

### [x] H2.3 实现 privacy filter

- 凭据模式、Authorization、URL query、用户主目录和敏感路径。
- UTF-8 字节限制在过滤后执行。
- 回归测试不包含真实秘密。

### [~] H2.4 实现 coalescer 和限速

- 相同语义状态不重复发送。
- 高频文本变化最多每秒 4 个 status。
- 终态、等待用户和等待批准不能被延迟或吞掉。
- 重连时总能获得最新完整 snapshot。

### [~] H2.5 Host 单元和集成测试

- 覆盖 codec、Schema、ACK、重连、privacy、normalize、coalesce。
- 使用 fake serial transport 模拟 partial read/write、丢 ACK 和断线。
- 测试无任务、连续任务和新 session。

## 14. A0：真实 Agent 适配

### [x] A0.1 确认第一个 Agent 事件源

- 记录可用 API、事件文件或稳定集成点。
- 明确权限、生命周期、事件延迟和版本兼容性。
- 不以 UI 像素识别作为正式方案。

当前决策（2026-09-02）：Codex 使用 lifecycle hooks；本机 Codex `hooks/list` 已正确解析九个项目 hook 且无 warning/error，用户已批准项目 hook trust。真实服务日志已覆盖提交、工具、思考和批准等待；仍需记录完成、失败、取消以及事件断档的实际 payload。

### [x] A0.2 定义 Agent 事件映射表

- 覆盖提交、规划、工具、编辑、命令、测试、等待输入、等待批准、完成、失败、取消。
- 对无法识别的事件安全降级为 thinking/working/other。
- 明确多 Agent 活动如何汇总为单个当前动作。

### [x] A0.3 实现首个 adapter

- 只访问所需最小权限的数据源。
- 处理 Agent 进程重启、任务切换和事件断档。
- 原始事件不默认落盘。

### [x] A0.4 建立脱敏回归数据

- 人工构造或彻底脱敏正常/失败/批准等会话。
- 加入映射和 privacy 测试。
- 审查后再提交 Git。

实现记录（2026-09-02）：项目 `.codex/hooks.json` 覆盖 Session、Prompt、Tool、Permission、Subagent 和 Stop；hook CLI 在短进程内归一化、脱敏后通过 0600 Unix datagram socket 投递完整快照，Host 离线时默认不阻塞 Agent。合成 PreToolUse/Stop 已跨进程真机验证。

显示文本修复（2026-09-04）：观察到 Codex hook 的 task title 可能把空格序列化为字面量
`&#x20;`，此前会原样传至 LCD。Host 现在只归一化十六进制/十进制空格、tab、换行和
`&nbsp;`，兼容已观察到的非标准 `&#20;`，并清理标题首尾空白；`&lt;` 等非空白实体保持
字面量。task title、tool summary 和带编码空格的 usage-limit 检测均有回归用例，Host
套件增至 76 项。

额度与事件断档记录（2026-09-02）：当前 Codex lifecycle hook 集合没有专门的 turn-error、usage-limit 或 quota 事件；本次额度耗尽后日志停在 `PostToolUse → thinking`，未收到 `Stop`。如果未来 `Stop` payload 带 `usage limit`、`rate limit`、`quota`、`credit`、`额度` 或 `上限`，adapter 会映射到 `waiting_user`；同时 Host 对 starting/thinking/working 提供默认 120 秒无源事件 watchdog，超时后显示通用“请检查 Mac”，下一条真实 hook 自动覆盖。

### [ ] A0.5 真实端到端验证

- 至少完成编辑代码、运行命令、等待批准、完成和失败五种真实场景。
- 测量 Agent hook 源时间戳到设备 ACK 的延迟；LCD 光学延迟为可选扩展验证。
- 确认无思维链、凭据和完整私密路径泄露。

部分验证（2026-09-03）：本地 IPC envelope 新增同机单调时钟源时间戳，常驻 Host 在状态
ACK 后只记录 `source_to_ack_ms`，不增加 prompt、工具参数或状态正文日志。release 真机
前台服务采集到 12 个合成/真实 lifecycle hook 样本，平均 114.6 ms，P95/最大 169.4 ms；
编辑、命令和批准流已覆盖。仍需补真实完成、失败、取消三类 payload 的验收记录。

完成里程碑：M3。

## 15. I0：有线端到端集成

### [x] I0.1 自动启动与停止

- 定义 Host 手工启动方式。
- 可选增加 macOS LaunchAgent，但必须提供卸载方法。
- Host 停止后设备按心跳进入断线。

验证记录（2026-09-03）：手工 `saki-host serve` 已支持启动、Ctrl-C 清理、长期占用业务串口和 hook socket；设备端 15 秒心跳超时和真机冻结/恢复已验证。已加入用户级 LaunchAgent `com.saki.agent-display` 和 `scripts/saki-service.zsh` 的 render/install/start/stop/status/logs/restart/uninstall 管理命令。plist 通过 `plutil`，安装后 launchd 状态为 running，Host 同时持有 0600 hook socket 和业务 CDC；合成 `PreToolUse` 及真实 Codex hooks 均成功同步到设备。受控 restart 后进程重新拉起、重新握手并从新 session 的 `seq=1` 恢复，stderr 为空；`start` 在服务已运行时幂等，独占串口测试可用 stop/start 临时释放并恢复服务。卸载方法已记录但为保留当前常驻服务未执行。

### [x] I0.2 断线矩阵（当前版本范围）

- 首版覆盖拔 USB、关闭/暂停 Host、Agent 源事件中断和设备 reset。
- Mac 睡眠/唤醒作为可选扩展验证，不阻止当前版本发布。
- 每种情况记录设备 UI、Host 日志和恢复时延。
- 不允许恢复后显示旧任务为实时状态。

部分验证（2026-09-03）：Host `SIGSTOP` 17 秒且保持 USB CDC/DTR 打开时，设备在 15 秒 deadline 后清除旧协议会话并显示保留快照的离线状态；`SIGCONT` 后旧 ping 被 `not_handshaken` 拒绝，Host 自动重新握手并恢复最新完整快照。后续记录覆盖 Host 正常退出和设备 reset；Mac 睡眠/唤醒转为可选扩展验证。

物理拔插记录（2026-09-03）：0.1.4 真机完成 3/3 次拔出/插入；每次 Host 都先检测到 `Device not configured` 或设备缺失，再按 0.25/0.5/1/2 秒上限退避重试，设备重新枚举后自动 hello，并以 `seq=1` 恢复最新完整快照。恢复耗时最小 448.4 ms、平均 724.1 ms、最大 1092.4 ms，未产生 stderr。该矩阵中的 USB 拔插项已小样本通过。

设备 reset 记录（2026-09-04）：在 `0.2.0` release 和 LaunchAgent 运行期间短按
RST。设备随后以相同序列号和产品名重新枚举，`/dev` 节点实例发生变化；LaunchAgent
未重启并重新独占打开新的动态应用串口，说明连接循环可在设备 reset 后
恢复。等待人工操作超过了本轮 120 秒监控窗口，未捕获精确离线/重连耗时；该项功能通过，
当前版本不要求补录该性能数据。

Host 正常退出记录（2026-09-04）：通过管理脚本正常停止 LaunchAgent，等待 17 秒后，用户
确认 release 设备保留最后任务快照并显示 `DISCONNECTED/OFFLINE`；重新启动 LaunchAgent
后服务恢复为 running 并重新独占应用端口。该矩阵项通过。

范围决策（2026-09-04）：项目所有者决定暂缓 Mac 睡眠/唤醒验证。现有 Host 暂停/恢复、
物理拔插、设备 reset、Host 正常退出和 120 秒 Agent 事件断档兜底足以作为当前版本
发布基线，因此本任务按当前范围完成。

### [x] I0.3 连续运行（当前版本基线）

- 首版完成至少 100 次连续状态更新的 release smoke。
- 24 小时且至少 10,000 次更新作为可选扩展验证。
- 记录 ESP heap、stack watermark、Host RSS 和 ACK 延迟。

工具与 smoke 记录（2026-09-03）：新增 `saki-host serial soak`，默认目标为 86,400 秒和
10,000 次完整状态更新，按时长均匀调度，并拒绝会超过 10 秒心跳安全间隔的参数。测试从
`pong` 定期记录 diagnostics/runtime，统计 ACK min/mean/P50/P95/P99/max 和 Host 最大
RSS，报告通过临时文件原子替换且不覆盖已有文件。release 真机 100 次/10 秒 smoke 通过：
实际 11.1 秒，ACK 平均 103.6 ms、P95 105.5 ms、最大 105.6 ms；协议异常、UI 覆盖、
TX 丢弃和心跳超时增量均为 0，内部 heap 历史低点 198,963 bytes，app/UI/USB stack
最低 2,668/5,952/5,544 bytes，Host 最大 RSS 27,344,896 bytes。报告保存在本地、不进入
仓库；项目所有者于 2026-09-04 决定暂缓 24 小时正式运行，现有 smoke 作为当前版本发布基线。

### [x] I0.4 性能验收（当前版本范围）

- Agent hook 源时间戳到设备 ACK 小于 250 ms。
- UI 动画不阻塞协议。
- 长文本更新不冻结超过 500 ms。
- LCD 最后像素的光学延迟为可选扩展验证。

部分验证（2026-09-03）：20 次独立 DTR 会话的 hello 握手平均 160.6 ms、最大 164.4 ms，低于 250 ms 目标；该结果作为独立的链路握手基线。

hook 链路记录（2026-09-03）：IPC 源时间戳到设备成功 ACK 的 12 个 release 真机样本平均
114.6 ms、P95/最大 169.4 ms，全部低于 250 ms。ACK 表示固件已验证并把完整快照放入
UI queue；UI task 以 10 ms 周期消费，随后同一轮调用 `lv_timer_handler()` 启动 LCD
刷新。因此当前结果给出 hook→UI 入队的精确值和额外约 10 ms 的 UI 调度上界。项目所有者
于 2026-09-04 决定暂缓高速摄影或光学探头测量，现有数据和人工视觉确认作为当前版本基线。

## 16. Q0：质量、安全与发布

### [x] Q0.1 非法输入与 fuzz

- 回放全部 invalid fixtures。
- 随机截断、拼接、重复和超长字节流。
- 验证设备不崩溃且能在下一合法帧恢复。

实现与验证记录（2026-09-03）：新增 `saki-host serial fuzz`，自动加载 `protocol/fixtures/v1/invalid` 下的全部脱敏 fixture，并用固定 seed 生成有界的截断、分块、拼接、重复、任意字节、非法 UTF-8 和超长帧 corpus。Host 在注入后以静默窗口排空错误响应，再在同一 CDC 连接内重新 hello、应用合法 idle 快照并 ping。目标设备使用 seed `20260903` 完成 40 个 case、6,840 bytes：诊断增量为 invalid 51、oversized 1，恢复握手、合法状态和心跳全部通过；突发错误响应产生 1 次有界 TX drop，未影响恢复。随后 `doctor` 确认设备仍为 healthy，内部 heap 历史低点 182,079 bytes，app/UI/USB stack 最低值为 2,116/4,896/5,552 bytes。加入文档链接检查后 Host 回归增至 66 项并全部通过。

release profile 复测（2026-09-03）：相同 seed 和 40 个 case 在固件 `0.2.0` 上再次通过，同一 CDC 连接内恢复 hello/status/ping；invalid 增量 51、oversized 增量 1，`tx_drops=0`。测试后 `doctor` 为 healthy。

### [x] Q0.2 内存和栈检查

- 记录启动、工作、详情、断线后的 free/min heap。
- 检查所有任务 stack watermark。
- 处理任何持续增长或低于安全余量的任务栈。

实现与验证记录（2026-09-03）：固件新增可选 `pong.runtime`，报告总 8-bit heap 和内部 8-bit heap 的当前/历史最低值，以及 app 初始化、UI、USB 三个 Saki 任务的 stack watermark，单位统一为 bytes。Host `doctor` 兼容缺少该对象的旧固件；新固件内部 heap 历史低点低于 32 KiB，或任一任务 stack watermark 低于 1 KiB 时返回 unhealthy。Schema、fixture、Host 解析/阈值测试和第 11 个固件 Unity 用例已加入；60 项 Host 测试、Ruff、Unity 真机测试及 dev/release 主固件构建通过。首次真机基线发现默认 3,584-byte `app_main` 栈仅余 580 bytes，因此把 dev/release 共用配置提高至 5,120 bytes。修正固件刷入后，启动基线的 app/UI/USB stack 分别为 2,116/5,216/6,736 bytes；经过含 starting、working、completed、中文详情和断线覆盖层的 4 轮回放后，内部 heap 历史低点稳定在 182,199 bytes，app/UI/USB stack 历史低点为 2,116/4,976/5,552 bytes，全部超过安全线，协议异常计数保持为 0。

release profile 复测（2026-09-03）：启动后 internal min heap 为 201,207 bytes，app/UI/USB stack 为 2,772/4,896/6,912 bytes；完成状态回放及 40-case fuzz 后，历史最低值为 200,503 bytes 和 2,772/4,896/5,568 bytes，仍全部超过安全线。

### [x] Q0.3 固件尺寸和分区

- 执行 `idf.py size` 和 size-components。
- app 分区保留明确安全余量。
- 如采用 5 MiB 分区，验证表地址、总大小和 coredump。

验证记录（2026-09-03）：加入 GB2312 字库、设备端心跳 watchdog、协议诊断、本地执行时间、UTF-8 防御和平滑未知进度动画后，正式 `saki.bin` 为 `0xf7e40` bytes（1,015,360 bytes），SHA-256 `f894ccc972fc8bd49f4e9d5afab5dc8dc9987d7d30708edd421d78d92a48da29`；2 MiB app 分区剩余 `0xf81c0` bytes（50%）。0.1.6 镜像已完成真机写入及 Flash 哈希校验，`doctor` 确认设备报告 0.1.6。

profile 记录（2026-09-03）：已执行 `size` 和 release `size-components`。加入 `pong.runtime` 并把 `app_main` 栈提高至 5,120 bytes 后，dev `0.2.0-dev` 镜像为 1,016,096 bytes、SHA-256 `06fd80abfefc20186efab8e045aacdaa5dbb46567a3c7abb7876bf4284c58126`，2 MiB app 分区余 1,015,520 bytes（50%）；release `0.2.0` 镜像为 922,640 bytes、SHA-256 `1e56346fd4ac598f5ff054d81aa90c3a450a86d6f24f305c969661789c9a8e08`，分区余 1,108,976 bytes（55%）。

UI policy 更新记录（2026-09-03）：加入详情/亮度状态机后，dev 镜像为 1,017,680 bytes、
SHA-256 `8bf47519a0a2ee93702518998995ff6e198aa8b6187af3309f3cec3665190c83`，
分区余 1,013,936 bytes（50%）；release 镜像为 923,648 bytes、SHA-256
`16e906a2b78f09778b0e03f6cc7a96e5f6f6cad626db154bc421ed7bff0e7ca1`，分区余
1,107,968 bytes（55%）。固件 UI policy 在目标设备通过 17/17 Unity 用例；Host 新增手工
`--hold` 心跳后为 67/67 测试通过。

### [~] Q0.4 发布配置审查

- 日志不输出完整 JSON。
- UI demo、trace 和测试入口关闭。
- 固件版本、协议版本和 USB 描述正确。
- sdkconfig、依赖锁和构建命令已提交。

部分验证（2026-09-03）：release 生成配置和二进制已核对，版本为 `0.2.0`，协议仍为 v1，USB 描述为 Saki / Saki Agent Display / saki；未发现记录完整 JSON 的固件日志；UI demo、LVGL demos、app trace、TinyUSB debug 和 Unity test runner 已关闭。该镜像已刷入目标设备，`doctor`、状态回放、40-case fuzz 和恢复后资源检查均通过；LaunchAgent 已重新连接并同步真实 hooks。尚需在干净的发布提交上复现构建。

UI policy release 复测（2026-09-03）：最新 923,648-byte `0.2.0` release 镜像完成真机
Flash 哈希校验；重新启动后 `doctor` 验证 320×240、协议 v1、固件 0.2.0，全部协议异常
计数为 0。内部 heap 历史低点 199,951 bytes，app/UI/USB stack 余量为
2,668/6,192/6,984 bytes。LaunchAgent 恢复后状态为 running，hook socket 为 `0600`，
进程实际打开正确的动态应用串口。仍只缺干净发布提交上的最终复现。

首次提交准备（2026-09-04）：版本文档已重组，后续无线任务移入路线图；本机 validation
产物、构建目录、Python 缓存和 wheel 构建目录均从提交排除。Codex hook 与 ESP-IDF 环境
脚本不再写死用户目录，并加入自动回归检查。Host 78/78、Ruff、Git staged whitespace、
dev/release 主固件和 Unity 测试镜像构建均通过；首次提交内容已完成检查，最终 release
提交与 tag 仍待发布验收通过后创建。

### [~] Q0.5 生成发布产物

- 生成 bootloader、partition table、app 和统一烧录说明。
- 输出 SHA-256。
- 记录 ESP-IDF、Python、依赖和 Git commit。
- 产物不包含原厂 Flash 备份。

部分验证（2026-09-03）：新增 `scripts/package-release.zsh`，默认生成带 `-candidate` 后缀的目录与 zip，`--final` 强制要求已有干净 Git 提交且 Host/固件版本同为 `0.2.0`。首个候选包已包含 bootloader、partition table、app、从 `0x0` 烧录的合并镜像、release sdkconfig、依赖锁、Noto 字体许可证、构建环境、烧录说明和 9 项 SHA-256；包内校验全部通过，合并镜像 988,176 bytes，zip 1,402,511 bytes，未包含原厂备份。当前封包脚本还会加入项目 Apache-2.0 `LICENSE` 与 `NOTICE` 并纳入 SHA-256 校验。该候选生成时仓库为 unborn/dirty 且 Host 仍是 `0.2.0.dev0`，因此不能标记最终完成。

UI policy 候选更新（2026-09-03）：旧候选可恢复地移入
`artifacts/stale/20260903-pre-ui-policy/`；重新构建并生成当前
`artifacts/saki-0.2.0-candidate/`。新合并镜像为 989,184 bytes，zip 为 1,404,059 bytes，
zip SHA-256 为 `3cfcd178a0e736643ea5272522e15a40e5f90fe8b9fb4c87efb9f687f731c0ab`；
包内 9 项校验和及外层 zip 校验均通过。候选仍明确记录 Git `UNBORN/dirty` 和 Host
`0.2.0.dev0`，不冒充最终 release。

### [x] Q0.6 用户文档

- Host 安装和启动。
- 设备烧录和动态端口发现。
- 常见错误和恢复原厂固件。
- 隐私模型和日志开关。

验证记录（2026-09-03）：规格、实施设计、任务清单和用户指南已按 `0.2.0` 归档，发布说明单独按版本保存；README 提供统一入口，自动化测试会检查全部本地 Markdown 链接。文档覆盖固件校验/烧录、动态端口、Host venv 与 LaunchAgent、Codex hook 信任、日常管理、隐私过滤、最小运行日志、常见错误、额度耗尽兜底和原厂恢复。另从 `./host` 构建 `saki_host-0.2.0.dev0-py3-none-any.whl`，在全新 Python 3.12 临时 venv 中联网安装运行时依赖；`pip check`、`saki-host --help` 和脱敏协议 demo 均通过，临时目录已自动清理。

完成里程碑：M4。

## 17. R1：第一版发布检查

### [ ] R1.1 对照 SPEC 功能验收

- 逐项执行 `SPEC.md` 第 12.1 节。
- 每项记录通过、失败或不适用理由。

### [x] R1.2 对照 SPEC 稳定性验收

- 逐项执行 `SPEC.md` 第 12.2 节的当前版本发布门槛。
- 链接 soak、拔插和非法帧结果。

验收记录（2026-09-04）：100/100 release soak smoke、USB 拔插 3/3、非法帧恢复、心跳
断线/完整快照恢复和业务 CDC 日志隔离均通过。24 小时/10,000 次与 Mac 睡眠/唤醒按
项目所有者决策转为可选扩展验证。

### [x] R1.3 对照 SPEC 性能验收

- 逐项执行 `SPEC.md` 第 12.3 节的当前版本发布门槛。
- 保存延迟、刷新和冻结测量方法。

验收记录（2026-09-04）：12 个 release 真机 hook→设备 ACK 样本平均 114.6 ms、P95/最大
169.4 ms，低于 250 ms；UI 以 10 ms 周期消费队列，未知进度动画期间协议持续响应。人工
真机观察未见明显撕裂或长文本冻结；最后像素光学测量转为可选扩展验证。

### [ ] R1.4 发布决策

- 所有 blocker 已关闭。
- 已知限制写入 README/release notes。
- 标记 firmware 和 host 同一版本。

版本决策（2026-09-03）：历史 `0.1.x` 均视为开发验证固件；首个正式 release 从 `0.2.0` 开始。

BLE、Wi-Fi 和多传输仲裁不属于 `0.2.0`，后续工作统一记录在
[项目路线图](../../ROADMAP.md)，不在首版任务清单中混排。

## 18. 当前已知 blocker / 决策点

| ID | 项目 | 当前状态 | 解除方式 |
| --- | --- | --- | --- |
| D-001 | Ninja 缺失 | 已解除：Ninja 1.12.1 | E0.1 已完成 |
| D-002 | 当前未检测到开发板串口 | 已解除：检测到目标设备 | 2026-09-02 确认动态端口可用，端口仍不可写死 |
| D-003 | LVGL lock 记录 IDF 5.5.4，当前使用 5.5.3 | 已解除：5.5.3 干净构建并真机启动成功 | 后续继续固定并记录 5.5.3 |
| D-004 | TinyUSB 与 Serial/JTAG 日志路径的实际行为 | 已解除：业务 CDC 为 `303a:4001` 且协议流无日志；运行时替代 USB-JTAG | 后续发布 profile 继续审查日志配置 |
| D-005 | Codex Hook payload 与 trust 流程 | 部分解除：schema、trust 和真实工具/批准流已验证；额度耗尽不产生 Stop | 补全终态 payload；事件断档由 120 秒 watchdog 兜底 |
| D-006 | 动态中文字体覆盖范围与固件体积 | 已解除：完整 GB2312 子集，`saki.bin` 约 990 KiB，分区余 50% | 真机确认后完成 F2.1 |

阻塞项解除后应更新表格，不删除历史；保留简短结论或指向决策记录。
