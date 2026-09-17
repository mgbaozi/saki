# Saki 0.4.0 用户指南

> 历史开发指南：0.4.0 未单独发布，相关能力已并入 0.4.5。
> USB 和 BLE 启动/绑定沿用 [0.3 指南](../0.3.0/USER_GUIDE.md)。

当前安装以根 README 和 0.4.5 用户指南为准；完整真机矩阵统一在 1.0 候选执行。

## 安装来源 hook

先按根目录 README 安装 Host 依赖。Claude Code 使用本机已有登录/API 配置；Saki 日常采集
不调用模型，也不需要保存 API key。受测 Claude Code 为 2.1.270。

```zsh
host/.venv/bin/saki-host hooks install --source claude_code
host/.venv/bin/saki-host hooks check --source claude_code
```

默认合并用户级 `~/.claude/settings.json`，保留已有 hooks、permissions、env 和其他配置。
安装后按客户端配置加载方式重开会话；跨项目均使用同一 Saki Host。若只想在某项目启用，
使用显式 `--settings <项目>/.claude/settings.json`，不要同时安装两个作用域。

Codex 可继续使用本仓库的项目 hook，也可以安装用户级 hook，二选一：

```zsh
host/.venv/bin/saki-host hooks install --source codex
host/.venv/bin/saki-host hooks check --source codex
```

两种来源可独立安装，不要求同时存在。`hooks check` 仅输出安装、wrapper 和身份密钥就绪
布尔值，不打印配置。解释器/仓库移动后重新 install 更新 wrapper。安装器只管理带 Saki
标记的条目，重复 install 幂等；显式项目路径的 check/uninstall 使用同一个 `--settings`。

```zsh
host/.venv/bin/saki-host hooks uninstall --source claude_code
```

配置变更前会在原配置目录留 `.saki-settings-backup-*` 私有备份。备份可能包含原配置已有的
凭据，不要分享、提交或放进发布包。常规卸载保留其他设置；需要恢复整个配置时先核对
备份时间，避免覆盖之后的用户修改。身份密钥与 wrapper 留在本机供其他作用域继续使用。

## 启动与显示

```zsh
scripts/saki-service.zsh restart
```

也可以以前台 `host/.venv/bin/saki-host serve` 运行；不要和常驻 Host 同时启动。安装和 Host
启动会初始化私有身份密钥，不需要打开或复制其内容。

主屏默认显示最近提交新请求的 Session，状态、摘要、耗时和进度保持完整。其他 Session
在右侧显示为状态色条 + 来源简称 + 短 ID + 状态小字，最多三项；单 Session 使用全宽。
工具调用、等待批准或完成不会抢走主屏，新的用户请求会切换主屏。

点击侧栏可临时查看其他 Session，点击顶栏或 15 秒无操作返回最新提交项；活动卡片可切换
详情。临时查看期间收到新请求会回到最新项。短 ID 后的 `*` 表示过期/待确认，主屏会显示
“过期”；底栏 `!N` 是隐藏待处理数，`FULL` 表示 Host 曾拒收新任务。
暗屏首次触摸只唤醒。批准与输入操作仍在 coding agent 中完成。

Host 可跟踪 32 项。最新提交项固定在主屏，其余按等待批准、等待输入、失败、活动、idle、
普通终态排序取三项。超过 4 项时没有设备翻页；可在 Mac 清理不需要保留的显示记录。

```zsh
host/.venv/bin/saki-host sessions list
host/.venv/bin/saki-host sessions forget <list 返回的脱敏 id>
host/.venv/bin/saki-host sessions forget all
```

list 读取最近脱敏检查点，输出不是实时 Agent 查询。forget 向 Host 请求删除，成功返回
`requested:true` 只表示已投递；稍后再次 list 核对，通常一秒内更新。此操作不会停止任务。
completed/cancelled/SessionEnd 60 秒后清理，idle 10 分钟；等待和失败需后续事件或手动清理。
Host 满额时保留受保护项，清理后新开始事件可继续进入。

旧固件自动显示最近提交的单任务焦点；0.4 新模式建立后，旧无身份 IPC 不会覆盖已识别任务。
仅旧 Host 连接 0.4 固件时保持单任务页面。

## 恢复与限制

身份和检查点在 `~/Library/Application Support/Saki`，文件权限为 0600。不要分享 identity.key；
更换它会使之后的任务获得全新 ID。重启 Host 恢复的任务统一冻结并标待确认，收到可信
活动再恢复。损坏检查点不影响 Host 空启动；终态剩余 TTL 按离线时间扣除，恢复最长 24 小时。

没有 Session ID 的来源事件被丢弃。任务长时间无事件只标过期，不推测已经结束。工具失败
不等于整轮失败，Stop 仅表示本轮回复结束；Claude 的迟到 Notification 不作为批准信号。
Claude 没有可靠 run ID/取消信号时，跨轮乱序和用户取消只能尽力反映。
主屏标题来自新请求的 task_title 或 prompt，经本地规则过滤、首句截取和长度限制后显示，
最多 48 字符/96 UTF-8 B。工具状态更新或已识别的“继续”会保留目标；没有有效目标时才回退
来源和短 ID。旧记录没有存过目标时，需要下一次明确的新请求补充。

标题会经本地 IPC 发送给 Host、保存在私有检查点，并传到设备显示。它可能包含用户请求的
原文片段，短请求可能完整显示；这不是自动语义概括或完全匿名化。程序会过滤规则识别出的
凭据、网址和绝对路径，但相对路径、文件名及其他未识别的敏感内容可能保留。原始 hook 对象
和独立的完整 prompt 字段不落盘或转发，不读取历史对话、工具输出或模型回复来生成标题。
常驻服务的常规同步日志不打印标题；手动使用 hook --stdout 时，诊断输出包含提取后的 goal。

## 开发验证

常规运行 `scripts/check.zsh`，不使用 API。显式真实 Claude probe：

```zsh
host/.venv/bin/python scripts/test-claude-hooks.py
```

该命令会使用本机既有认证调用真实 API，预算 0.25 USD、最长 90 秒，只让 Claude 读取临时
测试文件；输出只含通过状态、事件类别和计数。不会安装用户 hook、读取 transcript 或保存
原始输入/模型输出。它验证单 Session 生命周期，不能替代四任务真机验收。
