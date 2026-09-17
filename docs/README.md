# Saki 文档

正式版本的需求、设计、任务和用户指南按语义化版本归档。发布说明单独放在
`releases/`；尚未进入某个版本的工作只记录在 `ROADMAP.md`。

## 当前稳定版本

0.4.5 已正式发布，支持 Codex / Claude Code 多来源、多 Session、最新提交主屏、状态侧栏，
以及未开始空闲会话的显示筛选。版本范围和发布记录以发布说明及冻结任务记录为准。

- [0.4.5 产品规格](./versions/0.4.5/SPEC.md)
- [0.4.5 工程实施](./versions/0.4.5/IMPLEMENTATION.md)
- [0.4.5 实施任务与验证记录](./versions/0.4.5/TASKS.md)
- [0.4.5 用户指南](./versions/0.4.5/USER_GUIDE.md)
- [0.4.5 发布说明](./releases/0.4.5.md)

## 下一版本规划

0.5.0 聚焦可扩展 Agent adapter、通用来源标签和设备视觉体验；当前仅完成规划，尚未实施。

- [0.5.0 产品规格](./versions/0.5.0/SPEC.md)
- [0.5.0 工程实施计划](./versions/0.5.0/IMPLEMENTATION.md)
- [0.5.0 实施任务与验证计划](./versions/0.5.0/TASKS.md)
- [0.5.0 用户指南草案](./versions/0.5.0/USER_GUIDE.md)

## 多 Session 开发基线

0.4.0 是 0.4.5 继承的多来源、多 Session 开发基线，未单独作为正式版本发布。

- [0.4.0 产品与通信规格](./versions/0.4.0/SPEC.md)
- [0.4.0 工程实施设计](./versions/0.4.0/IMPLEMENTATION.md)
- [0.4.0 实施任务与验证](./versions/0.4.0/TASKS.md)
- [0.4.0 用户指南](./versions/0.4.0/USER_GUIDE.md)
- [0.4.0 开发说明（未发布）](./releases/0.4.0.md)

## 传输基线

- [0.3.0 产品与通信规格](./versions/0.3.0/SPEC.md)
- [0.3.0 工程实施设计](./versions/0.3.0/IMPLEMENTATION.md)
- [0.3.0 实施任务清单与验证状态](./versions/0.3.0/TASKS.md)
- [0.3.0 BLE 用户流程](./versions/0.3.0/USER_GUIDE.md)

## 早期已发布版本

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
