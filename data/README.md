# ClassFlow 数据集

> 用途：给排课引擎 / 导入解析 / UI 提供样例与异常探测输入。
> 每个数据集都是**独立目录**，内含统一命名的四件套：
> `teaching_classes.csv`（教学班，13 列）、`classrooms.csv`（教室）、
> `sections.csv`（作息表）、`teachers.csv`（可选，缺省时按班行 teacherId 自动推导）。
> App 导入对话框按文件分别挑选即可。

分类两级结构：

- `small/`   小数据 —— 全可排通，入门样例
- `large/`   大数据 —— 量级压力，全可排通
- `conflict/` 必然冲突 —— 资源/约束上**保证排不全**（用于失败归因与告警）
- `error/`   错误/怪异数据 —— 导入或排课层异常、格式探测

| 数据集 | 内容 | 导入 | 预期排课结果 |
|--------|------|------|--------------|
| `small/classic` | 11 班 / 6 门课，含 Norm·Lab·PlayGround | 四件套 | 全排通（0 失败） |
| `large/eighteen` | 18 班 / 6 门课（Any） | 教学班+教室+作息 | 全排通（0 失败） |
| `large/stress` | 约 199 班 / 60 门课 / 100 师，多类型多周段 | 四件套 | 全排通（0 失败），压测用 |
| `conflict/no_lab_rooms` | 需 Lab 但无 Lab 教室 | 教学班+教室+作息 | 2 班全部失败（无机房） |
| `conflict/giant_class` | 千人合班 vs 最大教室 200 | 同上 | C101 失败（容量）；对照班成功 |
| `conflict/teacher_overbooked` | 1 师周 64 节 > 上限 56 | 同上 | 必然无法全排（≥1 班失败/缺漏） |
| `conflict/lab_saturation` | 40 上机班 vs 1 间机房 | 同上 | 至多 28 班成功，≥12 班必失败 |
| `error/invalid_hours` | 学时 1.5 / 0.5（非法） | 同上 | C101/C102 排课失败（学时非法） |
| `error/reversed_weeks` | startWeek(16) > endWeek(1) | 同上 | 周区间为空，行为异常（探测用） |
| `error/capacity_under_planned` | planned(80) > maxCapacity(40) | 同上 | 可导入可排（排课只看 plannedSize），数据不自洽 |
| `error/duplicate_class_id` | 同一 classId 两行 | 同上 | 两个实体同 id，占位/H1 互相干扰 |
| `error/teacher_not_in_roster` | 班引用 T999 不在教师表 | 四件套 | 教师维度失联（外键悬空） |
| `error/column_count_mismatch` | 12 列/14 列行 + 1 合法行 | 同上 | 仅合法行被载入，坏行静默丢弃 |
| `error/unknown_room_type_token` | 类型词 "Computer"/"机房" | 同上 | 静默归一（→Norm / →Any） |
| `error/inconsistent_course_columns` | 同 courseId 内嵌字段打架 | 同上 | 课程以首行建档，后行字段忽略 |
| `error/week_out_of_range` | startWeek=0 / endWeek=30 | 同上 | 越界周原样占用（探测用） |
| `error/empty_rooms_import_fails` | classrooms.csv 空文件 | 同上 | loadCsv 整体导入失败 |

> 说明：`conflict/*` 与 `error/*` 的“预期”多数可由约束/解析逻辑推导（已注于各 README）；
> 标注“探测用/人工验证”的几套用于暴露边界行为，未做硬性断言。
