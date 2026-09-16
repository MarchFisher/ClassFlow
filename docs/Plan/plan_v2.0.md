# ClassFlow 开发计划 v2.0

> 版本：v2.0 · 当前版本：v1.1 · 日期：2026-08-28 · 目标：周概念与数据/UI 层打磨

## 目标

在 v1.1（工作区持久化与记忆，见 `plan_v1.1.md`）基础上，做两件事：

1. **删除学生端**：不再做学生侧功能，移除学生相关数据模型
2. **引入「周」概念**：一门课定义开课周范围（如 1~3 周、5~8 周）；作息表按周展示，一学期默认 16 周

> **本版本聚焦 UI 层与数据层**。排课核心（模拟退火 SA）与 UI/数据层基本解耦、且相对复杂，当前贪心策略暂时可用，故 **SA 后置**到后续版本。本版本仅对排课核心做**最小兼容改动**，保证数据模型变更后贪心仍可编译、可用。

## 决策记录

| 决策 | 结论 |
|------|------|
| 学生模型删除范围 | 只删 `Student` 结构与 `Permission::Student`；保留 `User` / `Teacher` / `Permission{Admin,Teacher}` |
| 「上几周」位置 | 放在 `Course`（`startWeek` / `endWeek`），一门课所有教学班共享周范围 |
| 学期总周数来源 | `DataStore::semesterWeeks`，默认 16，随快照 `[Term]` 段持久化（暂不新增源文件） |
| ScheduleEntry 周建模 | 一条 = 一次每周例行的课，携带周范围 `[startWeek, endWeek]`；`TimeSlot`（day × section）语义不变 |
| 冲突判定 | 占用键加周维度 `tag\|id\|day\|section\|week`，周范围展开为逐周键 → **不同周自动不冲突** |

**「周」建模思路**：1~3 周与 5~8 周的两门课，在同 `(day, section)` 上各自是一条带周范围的 `ScheduleEntry`。由于冲突表把周范围展开成逐周键，两门课的占用键落在不同周上，天然可共用同一教室、同一教师——无需写区间重叠判定。

## 数据模型变更

### `user.h`（删除学生端）
- 删除 `struct Student`
- 删除 `Permission::Student`，枚举剩 `{ Admin, Teacher }`
- `User.permission` 默认值由 `Permission::Student` 改为 `Permission::Teacher`

### `course.h`（周范围）
- `Course` 新增：
  ```cpp
  int startWeek = 1;   // 起始周
  int endWeek   = 16;  // 结束周（默认整学期）
  ```

### `timetable.h`（排课条目加周）
- `ScheduleEntry` 新增：
  ```cpp
  int startWeek = 1;   // 起始周
  int endWeek   = 16;  // 结束周
  ```
- `TimeSlot` **不变**（仍是 `dayOfWeek` × `startSection`~`endSection` 的每周例行语义）

### `datastore.h`（学期周数）
- 新增 `semesterWeeks`（默认 16）getter / setter；`clear()` 时重置为 16

## 交互变化

```
┌──────────────────────────────────────────────┐
│ [保存]              [周: 1 ▾]      [自动排课] │  ← 顶部栏 + 周选择器
├──────────┬───────────────────────────────────┤
│ [新建]   │            课表网格               │
│ [导入]   │    （按当前周过滤显示）           │
│ [导出]   │                                   │
└──────────┴───────────────────────────────────┘
```

- 课表上方新增**周选择器**（`QSpinBox`，范围 1..semesterWeeks）：切换周次，网格只显示周范围覆盖该周的条目
- 单元格 Tooltip 显示该课周范围（如 "高等数学 A101（1~3 周）"）

## 任务分解

### W1 数据模型（`core/models` + `core/store/datastore.h`）

- [x] `user.h`：删 `Student` 结构、`Permission::Student`，`User.permission` 默认改 `Teacher`
- [x] `course.h`：`Course` 加 `startWeek` / `endWeek`（默认 1~16）
- [x] `timetable.h`：`ScheduleEntry` 加 `startWeek` / `endWeek`（默认 1~16）
- [x] `datastore.h`：加 `semesterWeeks`（默认 16）与 getter/setter，`clear()` 重置

**验收**：字段齐全、默认值语义明确（未指定周时按整学期）；全项目编译通过。

### W2 数据层 CSV / 快照格式（`core/store/datastore.cpp`）

- [x] `loadCsv`：教学班行解析追加 `startWeek,endWeek`（课程字段，按 courseId 去重回填 `Course`）；**兼容旧 10 列格式**（周字段回退默认整学期）
- [x] `saveSnapshot` / `loadSnapshot`：新增 `[Term]` 段存 `semesterWeeks`（解析放 `loadCsv` 之后，避免被 `clear()` 重置）；`[TeachingClasses]` / `[ScheduleEntries]` 表头追加周列
- [x] `exportCsv`：排课结果追加周列
- [x] 更新 `data/teaching_classes.csv` / `teaching_classes_large.csv` 示例数据（追加周列）

**验收**：loadCsv → saveSnapshot → loadSnapshot 往返，周字段与 `semesterWeeks` 一致；旧 10 列数据仍可加载（默认整学期）。

### W3 排课核心最小兼容（`core/schedule`，SA 仍后置）

- [x] `ConflictTable::keyOf` 加 `week` 参数，`keysOf` 将 `[startWeek, endWeek]` 展开为逐周键
- [x] `GreedyStrategy::run` 构造 `ScheduleEntry` 时按 courseId 回填课程周范围（贪心逻辑本身不动）

**验收**：同 `(day, section)` 不同周范围不冲突；周范围重叠时正常冲突；贪心排课结果无回归。

### W4 UI 层周选择器（`ui`）

- [x] `TimetableModel`：加 `setCurrentWeek(int)` 与当前周成员；建 `m_byCell` 时只收 `startWeek ≤ 当前周 ≤ endWeek` 的条目；Tooltip 显示周范围
- [x] `MainWindow`：课表上方加周选择器 `QSpinBox`（1..semesterWeeks），新建/导入后刷新范围，切换即刷新课表

**验收**：切换周次，课表内容随之变化；不同周可看到不同课程；1~3 周与 5~8 周的课在同一格子不同周交替出现。

### W5 测试（`tests/`）

- [x] 更新 `tst_datastore` / `tst_snapshot` / `tst_scheduler` / `tst_conflicttable` 的列格式与条目构造
- [x] 新增用例：同一 `(day, section)` **不同周范围不冲突**；周范围重叠时冲突

**验收**：CTest 全部通过，无回归。

## 快照格式（v2.0）

```
[Term]
semesterWeeks
16

[TeachingClasses]
classId,courseId,courseName,credit,sessionsPerWeek,hoursPerSession,depart,teacherId,plannedSize,maxCapacity,startWeek,endWeek
C101,C01,高等数学,4,2,2,理学院,T001,120,150,1,3
...
[Classrooms]
roomNumber,capacity,type
A101,60,Norm
...
[Sections]
index,startTime,endTime
1,08:00,08:45
...
[ScheduleEntries]
entryId,teachingClassId,teacherId,dayOfWeek,startSection,endSection,classroomId,startWeek,endWeek
E1,C101,T001,1,2,2,A101,1,3
```

教学班行的课程字段（含周范围）按 `courseId` 回填，保证快照自洽可恢复。

## 阶段依赖

```
W1 数据模型 ──> W2 存储格式 ──> W3 核心兼容 ──> W4 UI ──> W5 测试
```

## 后续（非本版本）

- **模拟退火 SA**：排课核心优化，设计文档见 `docs/algorithms/simulated-annealing.md`
- 教师个人课表
- 单周 / 双周错峰（非连续周模式，当前仅支持连续周区间）
