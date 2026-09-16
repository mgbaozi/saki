# Saki 文档

正式版本的需求、设计、任务和用户指南按语义化版本归档。发布说明单独放在
`releases/`；尚未进入某个版本的工作只记录在 `ROADMAP.md`。

## 当前开发版本

0.4.0-dev 已实现 Codex / Claude Code 多来源、多 Session 与最新提交主屏/状态侧栏；离线验证和构建通过，
真机混合并发、UI、传输切换和资源验收待完成。

- [0.4.0 产品与通信规格](./versions/0.4.0/SPEC.md)
- [0.4.0 工程实施设计](./versions/0.4.0/IMPLEMENTATION.md)
- [0.4.0 实施任务与验证](./versions/0.4.0/TASKS.md)
- [0.4.0 用户指南](./versions/0.4.0/USER_GUIDE.md)
- [0.4.0 开发说明（未发布）](./releases/0.4.0.md)

## 下一版本规划

0.4.5 已进入实施，空闲会话显示筛选已完成，完整验收待完成；复用 0.4 已有 Claude Code 接入及已校准的隐私契约，
补齐安装、生命周期和真实来源验证。本地用户级 hook 已安装，端到端确认仍待完成。

- [0.4.5 产品规格](./versions/0.4.5/SPEC.md)
- [0.4.5 工程实施计划](./versions/0.4.5/IMPLEMENTATION.md)
- [0.4.5 实施任务与验证计划](./versions/0.4.5/TASKS.md)
- [0.4.5 用户指南草案](./versions/0.4.5/USER_GUIDE.md)

## 传输基线

- [0.3.0 产品与通信规格](./versions/0.3.0/SPEC.md)
- [0.3.0 工程实施设计](./versions/0.3.0/IMPLEMENTATION.md)
- [0.3.0 实施任务清单与验证状态](./versions/0.3.0/TASKS.md)
- [0.3.0 BLE 用户流程](./versions/0.3.0/USER_GUIDE.md)

## 已发布版本

- [0.2.0 产品与通信规格](./versions/0.2.0/SPEC.md)
- [0.2.0 工程实施设计](./versions/0.2.0/IMPLEMENTATION.md)
- [0.2.0 实施任务清单](./versions/0.2.0/TASKS.md)
- [0.2.0 用户指南](./versions/0.2.0/USER_GUIDE.md)
- [0.2.0 发布说明](./releases/0.2.0.md)
- [后续路线图](./ROADMAP.md)

## 命名规则

- 版本文档使用 `docs/versions/<semver>/`，同一版本内保持 `SPEC.md`、
  `IMPLEMENTATION.md`、`TASKS.md` 和 `USER_GUIDE.md` 四类固定文件名。
- 发布说明使用 `docs/releases/<semver>.md`。
- 尚未确定版本的功能放入 `docs/ROADMAP.md`，确定目标版本后再迁入对应任务单。
- 若以后需要保存架构决策，使用 `docs/decisions/YYYY-MM-DD-<topic>.md`。
- 真机原始日志、soak JSON、截图、串口捕获和候选固件属于本地 validation 产物，
  不进入代码库；版本文档只保留可复核的结果摘要与复现命令。
