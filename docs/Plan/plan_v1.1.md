# ClassFlow 开发计划 v1.1

> 版本：v1.1 · 日期：2026-08-27 · 目标：工作区持久化与记忆

## 目标

在 v1.0（MVP 自动排课，见 `plan_v1.0.md`）基础上引入「工作区」概念：

- 侧边栏 **新建** = 从三份源文件（教学班 / 教室 / 作息表）新建工作区，新建后立即自动排课；**导入** = 仅从工作区快照恢复；**导出** = 导出完整工作区快照
- 顶部栏新增 **保存**（快捷键 Ctrl+S）；新建前自动保存原工作区，关闭时自动保存，启动时自动恢复上次工作区
- 数据落盘到项目本地 `.classflow/workspace.dat`（不随 build 清理，加入 .gitignore）

## 交互布局变化

```
┌──────────────────────────────────────────────┐
│ [保存]                          [自动排课]   │  ← 顶部栏
├──────────┬───────────────────────────────────┤
│ [新建]   │                                   │
│ [导入]   │            课表网格               │
│ [导出]   │                                   │
└──────────┴───────────────────────────────────┘
```

## 任务分解

### W1 数据层：工作区快照（`src/core/store/datastore.h/.cpp`）

- [x] `DataStore::saveSnapshot(const QString &path) const`：序列化 教学班 / 教室 / 作息 / 排课结果 四段，写为单文件 CSV 分节（见下方格式）
- [x] `DataStore::loadSnapshot(const QString &path)`：解析四段恢复；教学班/教室/作息三段先落临时文件再调 `loadCsv` **完整复用现有解析**（含 Course 按 courseId 去重）；排课段单独解析后逐个 `addScheduleEntry`

**验收**：saveSnapshot → loadSnapshot 往返，5 个集合数量与逐字段内容一致；快照含排课结果时恢复后课表立即可用；损坏 / 空文件 loadSnapshot 返回 false。

### W2 UI：主窗口改造（`src/ui/mainwindow.cpp`）

- [x] 侧边栏三按钮：新建 / 导入 / 导出（新建替代原导入位，同为大按钮）
- [x] 顶部栏：左侧加「保存」小按钮 + `QShortcut(QKeySequence::Save)`；右上角「自动排课」保留（新建后已自动排一次，可手动重排）
- [x] 槽：`onNewClicked` 先自动保存当前工作区（若有数据）→ 弹新建对话框选三份源文件 → `loadCsv` → 立即自动排课；`onImportClicked` 弹快照选择对话框 → `m_store.loadSnapshot(path)`；`onSaveClicked` 写记忆文件；`onExportClicked` 另存快照
- [x] 重写 `closeEvent`：关闭时自动保存；构造函数：记忆文件存在则自动恢复，状态栏提示

**验收**：新建 → 自动排课 → 保存 → 关闭 → 重开，工作区与课表完整恢复；新建前原工作区已自动保存。

### W3 UI：新建 / 导入对话框（`src/ui/importdialog.cpp`）

- [x] 新建对话框：沿用现有三文件选择 UI（教学班 / 教室 / 作息表），确定后 `loadCsv`
- [x] 导入对话框：单文件快照选择行 + 预览；过滤 `*.csv;*.dat`，确定后 `DataStore::loadSnapshot`

**验收**：新建能正确载入三份源文件并排课；导入能正确恢复快照并刷新课表。

### W4 测试（`tests/tst_snapshot.cpp`）

- [x] 往返一致性：saveSnapshot → loadSnapshot 后各集合数量与内容相等
- [x] 含排课结果：快照恢复后 `scheduleEntries` 完整
- [x] 异常输入：损坏文件 / 空文件返回 false

**验收**：CTest 全部通过，无回归。

## 快照格式（单文件 CSV 分节）

```
[TeachingClasses]
classId,courseId,courseName,credit,sessionsPerWeek,hoursPerSession,depart,teacherId,plannedSize,maxCapacity
C101,C01,高等数学,4,2,2,理学院,T001,120,150
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
entryId,teachingClassId,teacherId,dayOfWeek,startSection,endSection,classroomId
E1,C101,T001,1,2,2,A101
```

教学班行的课程字段来自 `Course`（按 courseId 查询回填），保证快照自洽可恢复。

## 记忆文件

- 路径：`<项目根>/.classflow/workspace.dat`，`.gitignore` 追加 `.classflow/`
- 项目根定位：从 `QCoreApplication::applicationDirPath()` 向上寻找含 `data/teaching_classes.csv` 的目录（兼容 `build/` 与 `build/Desktop_*/` 两种构建位置）
- 目录不存在时自动创建

## 行为变化（实现前需确认）

- **新建** = 从三份源文件新建工作区并立即自动排课（替代原「导入 → 源数据 3 CSV」路径）
- **导入** = 仅从快照恢复，覆盖当前工作区，无二次确认（新建/关闭时会自动保存，风险可控）
- **新建前**自动保存原工作区；**新建后**立即自动排课
- 导出按钮语义从「仅导出排课结果 CSV」变为「导出完整工作区快照」（原排课结果导出被替代）

## 阶段依赖

```
W1 数据层 ──> W2 主窗口 ──> W3 新建/导入对话框 ──> W4 测试
```

## 后续（非本版本）

- 教师个人课表、学生个人课表
- 选课与学生名单
- 回溯 / 遗传等更强排课策略
