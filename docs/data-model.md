# ClassFlow 数据模型

> 版本：v5.0 · 日期：2026-09-07
> 
> 说明：本文档为当前数据模型（含 v3.4 锁定 / v3.5 实体分离 / v3.6 条目原位更新 / v4.1 R2~R8 存储侧增量 / v4.2 编辑落库 / v4.6 双层保存）。类型用 C++ 记法，字段即 `core/models/*.h` 中同名 struct 的成员。与 store 层实际 API 的对照见 `core/store/datastore.h`（doc 不逐字段复述方法签名）。

---

## 1. 基础

```
Term { 
    id          : QString
    name        : QString
    startDate   : QDate
    endDate     : QDate
}
```

> 当前只用到 `semesterWeeks`（学期总周数，`DataStore::semesterWeeks()`，默认 16）。

## 2. 用户

```
User {                        // 基础用户信息
    uid         : QString
    name        : QString
    password    : QString
    permission  : enum { Admin, Teacher, Student }
}

TeacherInfo {                 // 教师表（排课域，一师一行；teacherId 唯一）
    teacherId   : QString
    name        : QString
    depart      : QString     // 所属学院
}
```

> 注意：排课域教师用 `TeacherInfo`（teacherId 主键），与 `user.h` 登录权限模型（Teacher，uid/courseList）不同名，避免冲突。

## 3. 课程体系（v3.5 起实体分离）

```
Course {                      // 课程模板（courseId 主键；可有 0..n 个教学班）
    id              : QString   // courseId：识别/去重一律用它，允许同名不同号
    name            : QString
    credit          : double       // 学分
    sessionsPerWeek : int          // 每周课程次数
    hoursPerSession : double       // 单次课所需学时
    depart          : QString      // 开课学院
    startWeek       : int          // 起始周（默认 1）
    endWeek         : int          // 结束周（默认 16，整学期）
    requiredRoomType : ClassroomType   // 所需教室类型（Any = 不限，H5）
}

TeachingClass {               // 教学班 = 课程的一个具体班次（classId 唯一）
    classId     : QString
    courseId    : QString      // 外键：关联 Course.id（课程可 0 班）
    teacherId   : QString      // 关联教师（TeacherInfo.teacherId，可为空）
    plannedSize : int          // 排课时预期人数（约束教室容量 H4）
    maxCapacity : int          // 最大容纳人数（选课上限）
    // 注意：不含 classroom、不含授课时间 —— 它们属于 ScheduleEntry
}
```

- **删除语义（v3.5）**：`removeTeachingClass` 只删班及其记录（排课/失败/锁定），**不删课程**——删到某课最后一个班，课程保留为空课程；删整门课用 `removeCourse`（连其全部班与记录）。
- 教学班 CSV 13 列（classId,courseId,courseName,credit,sessionsPerWeek,hoursPerSession,depart,teacherId,plannedSize,maxCapacity,startWeek,endWeek,requiredRoomType），课程字段内嵌于班行，按 courseId 归并建档。

## 4. 教室

```
enum class ClassroomType { Any, Norm, Lab, PlayGround };
    // Any 仅作课程 requiredRoomType 的"不限"哨兵，教室本体不会标 Any

Classroom {
    roomNumber : QString
    capacity   : int
    type       : ClassroomType   // 普通教室 / 机房 / 操场
}
```

## 5. 时间

```
Section {                     // 全局作息表：第几节 → 起止时间
    index     : int           // 节次编号
    startTime : QTime
    endTime   : QTime
}

TimeSlot {                    // 一个可排课的时间窗口（排课键：day × section）
    dayOfWeek     : int       // 1..7（周一..周日）
    startSection  : int       // 起始节
    endSection    : int       // 结束节
}
```

## 6. 排课结果（核心）

```
ScheduleEntry {               // 一次每周例行"某班在某时在某教室上课"（entryId 唯一，不透明）
    entryId         : QString
    teachingClassId : QString
    teacherId       : QString   // 冗余，便于按教师查询课表 / H3 冲突判定
    timeSlot        : TimeSlot  // dayOfWeek + startSection~endSection（start..end 连续跨节）
    classroomId     : QString
    startWeek       : int       // 起始周（默认 1）
    endWeek         : int       // 结束周（默认 16）——部分学期课（如后半段）改这一对
}
```

> 一个教学班每周上 `sessionsPerWeek` 次课 → 产生 `sessionsPerWeek` 条 `ScheduleEntry`，各自独立占用不同 `TimeSlot` + `Classroom`（同班各次课**不强制**同一教室/同一周范围结构）。
> **entryId 不透明**（v3.6 起强调；v4.1 R2 起字面与时间解耦）：新生成 id 形如 `classId#班内序号`（如 `C102#2`），**不内嵌时间字段**，班内序号从 1 递增、两两不同即唯一；老快照里的旧格式 id（`classId-星期-起始节`）原样兼容，只保证唯一、不要求格式一致。代码层不做反解析 / 重编号。手动调整改时间/教室时 entryId 保持原值，条目的 `updateScheduleEntry` 为**原位替换**。

**失败明细（随快照 `[ScheduleErrors]` 持久化，供"排课错误"展示）**

```
ScheduleFailure {             // 排课失败：给用户看"为什么排不上、怎么改"
    classId     : QString
    courseId    : QString
    courseName  : QString
    reason      : QString     // 人类可读归因（容量不足 / 类型不符 / 时间冲突…）
}
```

## 7. 运行期状态（store 级，随快照持久化）

```
DataStore 成员                      快照段             语义
semesterWeeks (int, 默认 16)       [Term]            学期总周数
m_courses (QVector<Course>)        [Courses]         独立课程实体（含空课程；老快照缺段时由班行推导）
m_lockedClasses (QSet<QString>)    [Locks]           锁定教学班 id（锁定重排时原样保留）
m_scheduleFailures                 [ScheduleErrors]  失败明细
排课条目 m_scheduleEntries         [ScheduleEntries] 由 ScheduleEntry 原样写出
```

- 单文件快照共**九段**：`[Term] / [Courses] / [TeachingClasses] / [Teachers] / [Classrooms] / [Sections] / [ScheduleEntries] / [ScheduleErrors] / [Locks]`。
- **兼容**：`[Courses]` 与 `[Locks]` 允许缺失——老快照照常打开（缺 `[Courses]` 时由教学班行回填推导课程、缺 `[Locks]` 时锁定为空）。
- **[Courses] 权威性与加载告警（v4.1 R4）**：`[Courses]` 段是课程事实的唯一权威。加载时逐字段核对班行内嵌课程列与 `[Courses]` 是否一致，**不一致一律以 `[Courses]` 覆盖**，并把差异记入 `DataStore::loadWarnings()`（`QVector<QString>` 人类可读中文），不阻断加载；导入/新建后若产生过覆盖，主窗口在状态栏提示"部分课程数据已按课程段校准"。CSV 导入（13 列含内嵌课程字段）语义不变：仍以班行为主、courseId 归并建档——快照加载与 CSV 导入两条路径方向不同，各自权威清晰。
- **store 级查询 / 文本（v4.1 R7/R8 + v4.6）**：
  - `DataStore` 懒建 5 张 `QHash<QString,int>`（id → 集合下标）索引——课程 / 教学班 / 教师 / 教室 / 排课条目；`ensureLookupIndexes()` 一次构建，凡可能改序的操作（`clear` / `clearScheduleEntries` / `dropClassRecords`）失效并在下次查询重建；`courseById / teachingClassById / teacherById / classroomById / scheduleEntryById` 均为 **O(1)**。`updateScheduleEntry` 走**原位替换**（保下标 = 保快照顺序 = 保卡片顺序，整班换教室后卡片不漂移）。
  - `snapshotText()` 把九段写盘内容先在内存拼成一段**确定文本**（`[Locks]` 排序后输出），供双层保存做"内容是否变化"指纹比对（见 §9）；`DataStore` 支持 `operator=`（Qt 值容器 COW），一次 `DataStore before = m_store;` 代价 O(1) 级——撤销/重做环靠它入环。

## 8. store 编辑方法与撤销环（datastore_edit / UndoBuffer）

- **编辑落库域（v4.2，`core/store/datastore_edit.cpp`）**：基本信息编辑不走排课线程，直接对 `DataStore` 做**语义化原子改动**——
  - `updateCourse`：改课程模板（课程名 / 学院）；
  - `updateTeachingClass`：改教学班的 教师 / 计划人数 / 最大容量；
  - `reassignClassTeacher(ids, teacherId)`：为若干班**换任课教师**——连带改写这些班**全部已排条目的冗余 `teacherId` 字段**（否则按教师筛选课表 / H3 判定会失联）；换师是否"撞车"由上层先查新教师在目标时段是否有课；
  - `removeScheduleFailureForClass`：某班补排 / 换师成功后清掉旧失败记录（防 stale 报错）；
  - `teacherSwapClashes(...)`：预检一批班换到某教师后与之冲突的条目（只读）。
  - 决策与提示文案（改名即时生效 / 换师撞车只登记 / 扩容溢出换大教室）由 `editui` 层负责，不在此域。
- **撤销 / 重做环（v4.1 R7，`core/store/undobuffer.{h,cpp}`）**：`UndoBuffer` 是有界 `QVector<DataStore>` 值环（默认容量 20，新值淘汰最旧）。接口 `push(before) / undo(current, &target) / redo(current, &target)`：`push` 存"动作前"状态；`undo/redo` 输出目标状态并维护游标，返回是否可再走。入环边界在 UI 侧（顶层可变动作才 push，见 architecture.md §7）——**数据模型本身无撤销字段**，全靠 DataStore 的值语义 + 环。

## 9. 双层保存的指纹（v4.6）

- `snapshotText()` 的**确定性**是双层保存的前提：同一数据状态无论何时何地生成，文本逐字节一致，才能用作"是否有变化"的判据。
- **L1 自动恢复**：主窗口起 ~1.5s 防抖 `QTimer`，每次到期把 `snapshotText()` 与上次写盘的指纹比对，不同才写 `.classflow/workspace.dat`（关窗 / 新建前再 flush 一次）。该文件 gitignore，用户无感知；启动总从它恢复。
- **L2 项目文件**：Ctrl+S / 「保存」写教务选定的项目文件（`persistProjectFile`）；窗口标题脏标记 ● = 当前 `snapshotText()` ≠ 最近手动保存 / 导入到项目文件时的 `snapshotText()`。
- 侧栏「导出快照」另存**独立副本**，不更新项目文件指纹、不影响 ●。

## 10. 关系图

```
Course 1 ─── 0..n TeachingClass  n ─── n ScheduleEntry ─── 1 Classroom
         (courseId 主键)     (courseId)       │
                                              └─── n  TimeSlot (day×section)
空课程（0 个教学班）合法存在；删除教学班永不级联删课程。
```

## 11. 排课约束（ConflictTable 维护，非数据字段）

| 约束 | 占用键 / 校验 |
|------|---------------|
| 同一教学班同一时间只能一次课（H1） | `C|classId|day|section|week` |
| 同一教室同一时间只能一个班（H2） | `R|room|day|section|week` |
| 同一教师同一时间只能一个班（H3） | `T|teacherId|day|section|week`（仅 teacherId 非空时占用） |
| 教室容量 ≥ 教学班人数（H4） | 排课时由 Scheduler / manualmove 校验 |
| 教室类型匹配课程所需（H5） | requiredRoomType ≠ Any 时须 classroom.type 相等 |

- 冲突判定按周 × 节展开（部分学期课只在其 `startWeek~endWeek` 内占用）。
- `ConflictTable::busyKeysOf(entry)` 返回某条目命中的占用键前缀（R/C/T），供手动调整失败时细分原因。

## 12. 与早期版本的差异对照

| 早期版本 | 本版（v5.0） | 原因 |
|----------|--------------|------|
| `TeachingClass.classroom` | 移除，迁至 `ScheduleEntry.classroomId` | 教室是占用表的键，随排课结果走 |
| 无授课时间 | 新增 `ScheduleEntry`（含 `timeSlot` + `startWeek/endWeek`） | 排课核心输出就是"时间 + 教室"；部分学期课需周范围 |
| `Session`（classDay/section/startTime/endTime） | 拆为 `Section`（作息表）+ `TimeSlot`（排课键） | 起止时间全局唯一，由节次推导，避免冗余与不一致 |
| `TeachingClass.enrolledCount` | 改为 `plannedSize`（排课约束）+ `maxCapacity`（选课上限） | 排课用预期人数，选课后才是实时人数 |
| `Teacher(User)` / `Student(User)` 继承写法 | 组合（has-a）；排课域用 `TeacherInfo` | C++ 实现更清晰，数据类无需继承 QObject |
| Course 恒有 ≥1 教学班、删末班连课删 | **courseId 主键、课程可 0 班、删班不删课**（v3.5） | 实体分离：空课程合法、识别用课程号、整门课删除单独进行 |
| Course 缺周范围 / 教室类型 | 增 `startWeek/endWeek/requiredRoomType` | H5 与部分学期排课需要 |
| ScheduleEntry 缺周范围 | 增 `startWeek/endWeek`（默认整学期 1~16） | 部分学期课（周 9~16）等场景 |
| 课程靠班行去重反推、无法存空课 | 快照增 `[Courses]` 段（含空课）；`[TeachingClasses]` 保留内嵌课程字段 | 老快照兼容 + 空课程可持久化 |
| 无锁定概念 | 快照增 `[Locks]` 段（v3.4） | 锁定班在局部重排中原样保留 |
| 无失败明细 | 增 `ScheduleFailure` + `[ScheduleErrors]` 段 | 失败诊断可持久化查看 |
| 无学期 | 新增 `Term`（预留） | 完整系统按学期排课 |
| 快照缺 `[Courses]`/`[Locks]` 仅"推导/留空" | 缺段照旧兼容，且班行课程列与 `[Courses]` 不符时**以课程段权威覆盖 + 记入 loadWarnings**（v4.1 R4） | 课程事实单一权威，数据不一致不再静默 |
| 无撤销/重做 | `UndoBuffer`（值环，容量 20）+ UI 入环边界（v4.1 R7） | DataStore 全值容器 COW，动作前状态 O(1) 级可存 |
| 编辑直接散落改字段 | 语义化编辑方法收口 `datastore_edit`（update / reassignClassTeacher 连带改条目冗余 teacherId / 清失败）（v4.2） | 换师后按教师查课表 / H3 判定不失联，失败记录不 stale |
| 保存只走"手动"一条路 | 指纹 `snapshotText()` 支撑 防抖自动恢复 + 项目文件 + ● 脏标记（v4.6） | 见 §9 |
