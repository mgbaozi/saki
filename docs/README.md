# Saki 文档

正式版本的需求、设计、任务和用户指南按语义化版本归档。发布说明单独放在
`releases/`；尚未进入某个版本的工作只记录在 `ROADMAP.md`。

## 当前版本

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
