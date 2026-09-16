# ClassFlow 开发计划

> 版本：v1.0 · 日期：2026-08-25 · 目标：MVP 阶段（自动排课）

## 阶段划分

### P0 数据层（core/models + core/store）

- [x] CSV 导入解析：`DataStore::loadCsv`，读入教学班 / 教室 / 作息表
- [x] 数据结构补全：`Course` 关联、`TimeSlot` 由作息表生成
- [x] CSV 导出：`DataStore::exportCsv`（可选，P2 也可）

**验收**：给定三段 CSV，`DataStore` 中能正确查到对应对象。

### P1 排课核心（core/schedule）

- [x] `ConflictTable`：教室 × 时间占用表，`canPlace` / `place` / `remove` / `clear`
- [x] `GreedyStrategy`：按「次数↓、人数↓」排序 → 逐时间槽 → best-fit 教室
- [x] `Scheduler::schedule`：串起策略与冲突检测，返回 `ScheduleResult`

**验收**：排课结果无教室冲突、容量满足、同班各次课时间互斥。

### P2 界面（ui）

- [x] `ImportDialog`：文件选择 + 预览 + 触发导入
- [x] `TimetableModel`：对接 `ScheduleEntry`，行列 = 星期 × 节次
- [x] `MainWindow`：导入 → 自动排课 → 课表网格 三入口串联
- [x] 导出按钮（若 P0 未做）

**验收**：从导入 CSV 到看见全校课表，全程可鼠标完成。

### P3 测试与打磨

- [x] 示例数据：`data/` 下放教学班 / 教室 / 作息表 CSV
- [x] 单元测试：`tests/` 覆盖 CSV 解析、冲突检测、排课正确性
- [x] 排课失败场景：教室不足 / 时间不足时给出明确提示

**验收**：贪心策略在构造数据上稳定复现，失败可诊断。

## 阶段依赖

```
P0 数据层 ──> P1 排课核心 ──> P2 界面 ──> P3 测试打磨
```

## 后续（非 MVP）

- 登录与角色区分（Admin / Teacher / Student）
- 教师个人课表、学生个人课表
- 选课与学生名单
- 回溯 / 遗传等更强排课策略
