# ClassFlow 开发计划 v3.1

> 版本：v3.1 · 当前版本：v3.0 · 日期：2026-09-01 · 目标：数据层重构——DataStore 按职责拆分 + 新增 ID 索引查询，消灭各调用方重复线性扫描

## 目标

1. **拆分 `datastore.cpp`**：当前 734 行，超 CLAUDE.md 单文件 ≤ 500 行约束。实现分散到 `datastore.cpp` / `csv.cpp` / `snapshot.cpp` / `lookup.cpp` 四个实现文件，公共辅助集中到 `store/utility.cpp`。**纯搬运、不改任何行为**。
2. **ID 索引查询设施**：数据层自持索引，`DataStore` 暴露 O(1) 的 `courseById` / `teachingClassById` / `teacherById` / `classroomById`，让"按唯一 ID 取完整信息"成为唯一权威入口，替换目前散落在各调用方的重复线性扫描。
3. **顺手补齐功能缺口**：课程详情弹窗显示**教师姓名**（此前只显示教师 ID，见 W3）。

## 需求

### 功能点

1. **store 层按职责拆分**：单一 `DataStore` 类、实现分散到多个 `.cpp`（基础 / CSV / 快照 / 索引查询 四个实现文件 + 公共辅助）。
2. **ID → 完整信息查询**：四个实体各自一条 `const T*` 查询，`nullptr` = 未找到；索引懒建、数据整体重建时失效。
3. **调用方切换到新接口**：`saveSnapshot`、`TimetableModel::setDataStore`、`CourseDetailDialog` 三处线性扫描替换为查表。
4. **详情弹窗显示教师姓名**：借助 `teacherById` 补齐，字段缺失仍显示"未指定"。

### 现状（改造基线）

- `datastore.cpp` **734 行**（超 500 行约束）；`datastore.h` 60 行。CMake `CORE_SOURCES` 逐文件登记（`src/core/store/` 现仅 `csv.*` 与 `datastore.*`，新增文件需登记）。
- **重复线性扫描现状**（本次要消灭的四处以 `file:line` 标注）：

  | 位置 | 在干什么 | 当前复杂度 |
  |------|---------|-----------|
  | `loadCsv` 课程去重（[datastore.cpp](src/core/store/datastore.cpp) `hasCourse` 循环） | 每个教学班扫一遍已建课程判断是否重复 | O(课程 × 教学班) |
  | `saveSnapshot` 回填课程字段（[datastore.cpp](src/core/store/datastore.cpp) 逐教学班扫课程） | 教学班 → 课程模板补全 13 列 | O(课程 × 教学班) |
  | `TimetableModel::setDataStore`（[timetablemodel.cpp](src/ui/timetablemodel.cpp)） | 建 classId → 课程名 / 课程 id 映射 | O(课程 × 教学班) |
  | `CourseDetailDialog`（[coursedetaildialog.cpp](src/ui/coursedetaildialog.cpp)） | 反查教学班 → 反查课程（两次扫描） | O(教学班) + O(课程) |

- 以上四处的索引工作由各消费者**各自实现一遍**，未归属数据层；教师 id → 姓名更是全项目无索引（姓名仅在筛选弹窗经 `optionLabel` 显示）。
- 现有 6 个测试套件全部通过；数据量虽小（线性扫描性能无感），但重复模式是本次重构的动机，非性能修复。

## 架构设计

### 模块位置：`src/core/store/`（拆分后目录结构）

```
src/core/store/
├── datastore.h            # 类声明 + 索引成员 + 查询接口（略增）
├── datastore.cpp          # 访问器 / 学期周数 / 失败明细 / clear（收尾小文件）
├── csv.cpp                # 既有 Csv 工具命名空间 + DataStore::loadCsv / exportCsv
├── snapshot.cpp           # saveSnapshot / loadSnapshot（工作区快照）
├── lookup.cpp             # 懒建 ID 索引 + 4 个查询方法
└── utility.h / utility.cpp# 公共辅助：类型互转 / readAllLines / writeTempCsv（namespace store）
```

- **`utility.h/cpp`**：把现匿名命名空间里被 csv 与 snapshot **共用**的辅助集中于此——类型字符串互转（`parseClassroomType` / `classroomTypeToString` / `parseRequiredRoomType` / `requiredRoomTypeToString`）、`readAllLines`、`writeTempCsv`。置于 `namespace store` 的自由函数：`utility.h` 声明、`utility.cpp` 定义。因跨翻译单元（csv.cpp / snapshot.cpp 都要用）共享，**必须外部链接**（不能 anonymous namespace），两处 `#include "utility.h"`。
- **`csv.cpp` 归属**：文件同时承载两样东西——既有 `namespace Csv` 的解析工具（`parseLine` / `cleanField` / `joinLine`，现 99 行）与迁入的 `DataStore::loadCsv` / `DataStore::exportCsv`。
- **估算行数**：`datastore.cpp` ≈150、`csv.cpp` ≈280、`snapshot.cpp` ≈200、`lookup.cpp` ≈100、`utility.cpp` ≈140，全部 ≤ 500。

> **拆分方式（硬约束）**：直接**复制粘贴原实现**——函数体、签名、注释原样迁移，只调整 `#include` 归属与命名空间（匿名 → `store`）。**不逐行重写**，避免行为漂移、杜绝无意改语义。拆分完成先跑 `ctest` 验证行为零变更，再做 W2 功能增强。

### ID 索引与查询设计

- **索引形态**：`QHash<QString, int>`（id → 在对应 `QVector` 的下标）。存下标而非指针，避免 `QVector` 重分配后指针失效问题。
- **懒建 + 失效**：索引为 `mutable` 成员，首次查询时由 `ensureLookupIndexes() const` 一次性构建；`clear()` 内调用 `invalidateLookupIndexes()` 清空。由于 `loadCsv` / `loadSnapshot` 都先走 `clear()`，**索引在整体重建后必然失效并惰性重建**；`addScheduleEntry` / `clearScheduleEntries` 只动排课表、不影响被索引的四张集合，无需失效。
- **查询接口**（`const`，`nullptr` = 未找到）：

  ```cpp
  const Course*        courseById(const QString &id) const;
  const TeachingClass* teachingClassById(const QString &id) const;
  const TeacherInfo*   teacherById(const QString &id) const;
  const Classroom*     classroomById(const QString &id) const;
  ```

- **不变式**：索引与对应集合**同生命周期一致**；集合只经 `clear` + 重建变更，故索引失效时机仅 `clear()` 一处即可覆盖全部路径。

## 决策记录

| 决策 | 结论 |
|------|------|
| 拆分粒度 | 一个 `DataStore` 类、实现分散 `datastore.cpp`（基础）/ `csv.cpp`（CSV 导入导出）/ `snapshot.cpp`（快照）/ `lookup.cpp`（索引查询）四个实现文件；不改类名、不改公共接口（除新增查询） |
| 公共辅助 | `utility.h/cpp`（namespace store 自由函数）：类型互转、`readAllLines`、`writeTempCsv`；外部链接、两处 include |
| 拆分方式 | **复制粘贴原实现**：函数体、注释、签名原样迁移，仅改 include / 命名空间归属；不重写 |
| 索引类型 | `QHash<QString,int>`（id → 下标），非指针，规避 `QVector` 重分配失效；懒建（mutable） |
| 失效时机 | 仅 `clear()` 一处 `invalidateLookupIndexes()`；`loadCsv`/`loadSnapshot` 均先 `clear()`，覆盖整体重建全部路径 |
| 查询返回 | `const T*` + `nullptr` 哨兵；调用方（如详情弹窗）自行做"未找到"兜底 |
| 教师姓名 | 详情弹窗补显示（`teacherById` 查姓名）；课表格子仍不显示教师（格子内容设计为"课程名\n教室"，本次不扩） |
| 算法层 | **本次不触碰** `core/schedule` 内部查找；其可用同样接口属后续优化 |
| 版本号 | CMake `project(ClassFlow VERSION 3.1 ...)` |
| 构建 | 遵循 build 目录约定，仅用 Qt Creator 标准构建目录验证 |

## 任务分解

### W1 store 拆分（纯搬运，行为零变更）

- [x] `utility.h/cpp`：复制粘贴迁移类型互转 / `readAllLines` / `writeTempCsv`（匿名 → `namespace store`，外部链接）
- [x] `csv.cpp`：复制粘贴追加 `DataStore::loadCsv` / `exportCsv`（保留既有 `Csv` 工具命名空间，`#include "datastore.h"` / `"utility.h"`）
- [x] `snapshot.cpp`：复制粘贴迁移 `saveSnapshot` / `loadSnapshot`（`#include "csv.h"` / `"utility.h"` / `"datastore.h"`）
- [x] `lookup.cpp`：先建空实现占位（W2 填充，保证 CMake 可登记编译）
- [x] `datastore.cpp`：保留访问器 / 学期周数 / 失败明细 / `clear` / `addScheduleEntry` 等基础件；**删除已迁走的段落**（loadCsv / exportCsv / saveSnapshot / loadSnapshot / 匿名辅助）
- [x] CMake `CORE_SOURCES` 登记 `snapshot.cpp` / `lookup.cpp` / `utility.cpp` / `utility.h`（`csv.cpp` / `datastore.cpp` 已在列）

**验收**：`ctest` 6/6 通过，拆分前后行为一致（`tst_datastore` / `tst_snapshot` 全绿即证）；`datastore.cpp` 降至 ≈150 行。

### W2 ID 查询接口 + 索引

- [x] `datastore.h`：新增 4 个查询方法声明 + 4 个 `mutable QHash<QString,int>` 索引成员 + 私有 `invalidateLookupIndexes` / `ensureLookupIndexes`；`#include <QHash>`
- [x] `lookup.cpp`：`ensureLookupIndexes`（按课程/教学班/教师/教室各建一张 id→下标表）+ 4 个查询（查表 O(1)，未命中返回 `nullptr`）
- [x] `clear()` 内调用 `invalidateLookupIndexes()`

**验收**：对现有示例数据，四个查询均命中且字段正确；未知 id 返回 `nullptr`。

### W3 调用方切换 + 教师姓名

- [x] `snapshot.cpp::saveSnapshot`：逐教学班回填课程字段改用 `courseById(tc.courseId)`（删内层线性扫描）
- [x] `timetablemodel.cpp::setDataStore`：建 classId → 课程名 / 课程 id 改用 `store->courseById(tc.courseId)`
- [x] `coursedetaildialog.cpp`：反查教学班 / 课程改用 `teachingClassById` + `courseById`；"教师"行改为查 `teacherById(entry.teacherId)` 显示姓名（未找到/为空 → "未指定"）

**验收**：详情弹窗教师显示姓名而非裸 ID；课表渲染与快照导出内容与 v3.0 完全一致。

### W4 测试（`tests/tst_datastore.cpp` 扩展）

- [x] 四查询命中：现网数据各查一条，返回的 `id` 与字段正确
- [x] 未命中：不存在的 id → `nullptr`
- [x] 快照路径回归：`loadSnapshot` 后查询仍正确（索引重建路径覆盖）
- [x] 全套 CTest 无回归（6/6）

> 实施记录：按验收时决定，**删除**了 `nonThirteenColumnRejected` 测试（专测旧格式兼容，验收要求不检查历史版本兼容）；`tst_scheduler` / `tst_annealing` 中 7 处 12 列旧 fixture 统一迁移为严格 13 列（表头补 `requiredRoomType`，数据行补 `Any`）。

**验收**：新增断言全过；`ctest` 6/6。

### W5 构建验证

- [x] `cmake --build build/Desktop_Qt_*_*-Debug` 通过
- [x] 版本号 `3.1` 生效；`ctest` 全绿

**验收**：Qt Creator 标准构建目录编译零告警；手动走查下方验收用例。

### W6 文档

- [x] `docs/architecture.md`：store 层拆分（4 实现文件 + `utility.h/cpp`）与 ID 查询接口入档
- [x] 本文档（plan_v3.1.md）

## 验收

1. `datastore.cpp` ≤ 500 行（目标 ≈150），store 目录按职责拆分、无重复辅助，公共件在 `utility.cpp`。
2. `DataStore` 提供 `courseById` / `teachingClassById` / `teacherById` / `classroomById` 四个 O(1) 查询，`nullptr` 语义明确。
3. 原四处线性扫描（loadCsv 去重 / saveSnapshot 回填 / setDataStore / CourseDetailDialog）全部切换查表；`CourseDetailDialog` 无线性扫描。
4. 课程详情弹窗"教师"行显示姓名（无教师时"未指定"）。
5. 课表渲染、快照导入导出、筛选行为与 v3.0 完全一致（`tst_datastore` / `tst_snapshot` / `tst_filter` 全绿即证）。
6. `ctest` 6/6 通过，无回归；版本号升至 3.1。
7. 拆分全程复制粘贴原实现（W1 阶段 diff 无语义改动，仅换文件/命名空间）。

## 阶段依赖

```
W1 store 拆分（纯搬运） ──> W2 ID 查询接口（依赖新文件骨架）
W2 ──> W3 调用方切换          W2 ──> W4 测试
W3/W4 ──> W5 构建验证          W6 文档（与 W3~W5 并行）
```

## 后续（非本版本）

- **算法层同设施**：`core/schedule` 内部按 courseId / classroomId 查找的线性扫描，可复用 `courseById` 等接口（本次刻意不触碰，避免混入算法改动）。
- **索引演进**：若未来引入教学班/教室/教师的删除或改名编辑，需把"失效时机"从仅 `clear()` 扩展到逐条变更点（现为纯加载模型，无此路径）。
- **教师课表视图**：`teacherById` 就绪后，可按教师聚合 `ScheduleEntry` 渲染个人课表（与 v3.0 后续项衔接）。
