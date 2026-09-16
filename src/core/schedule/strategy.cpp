/**
 * 文件职责：GreedyStrategy 实现——贪心排课（对齐 docs/algorithms/greedy-strategy.md）。
 * 以课程为单位按难度排序：周次数↓、单次学时↓、代表教师ID↑；
 * 每门课生成"离散度时间模式"（理想间隔 (5-N)/(N-1) 天，逐步放宽）；
 * 同课程教学班共用同一套模式（同一天、不同教室/节次），排不下的按 classId 兜底拆分；
 * 同一教学班固定同一教室（best-fit：容量 ≥ 人数 且 容量最小），并匹配课程所需教室类型（H5）。
 */

#include "strategy.h"

#include <algorithm>

#include <QHash>

#include "conflicttable.h"
#include "core/store/datastore.h"
#include "roomrules.h"

namespace {

/*
idealGap - 一周 N 次课的理想间隔（两次课之间的空闲天数）

Parameter：
    n: 每周课次（≥2）

Result:
    int: 理想间隔天数 (5-n)/(n-1)，下限 0

Remark:
    间隔 = 两次课之间空闲的天数；如 N=2 得 3，即"周一/周五"（中间隔 3 天）。
*/
int idealGap(int n)
{
    return qMax(0, (5 - n) / (n - 1));
}

/*
buildPatterns - 生成候选时间模式（按间隔从理想到最密排列）

Parameter：
    n: 每周课次

Result:
    QVector<QVector<int>>: 每个元素是一组 n 个"星期"；n=1 时依次尝试周一..周日

Remark:
    间隔逐步放宽（N=2：间隔 3→2→1→0，即 周一/五→周一/四→周一/三→周一/二）；
    模式内星期 = 1 + i×(间隔+1)；超出 7 天则该间隔放不下，跳过。
*/
QVector<QVector<int>> buildPatterns(int n)
{
    QVector<QVector<int>> patterns;
    if (n <= 1) {
        for (int d = 1; d <= 7; ++d)
            patterns.append(QVector<int>{d});
        return patterns;
    }
    for (int gap = idealGap(n); gap >= 0; --gap) {
        const int step = gap + 1;
        QVector<int> pat;
        pat.reserve(n);
        for (int i = 0; i < n; ++i) {
            const int day = 1 + i * step;
            if (day > 7)
                break;      // 该间隔放不下 n 次课
            pat.append(day);
        }
        if (pat.size() == n)
            patterns.append(pat);
    }
    return patterns;
}

/*
makeEntry - 构造排课条目

Parameter：
    tc: 教学班
    day: 星期
    section: 起始节次
    room: 教室号
    weeks: (起始周, 结束周)
    duration: 一次课占用的连续节数（默认 1）
    seq: 班内递增序号（从 1 起，用于 entryId；默认 0 = 占位，仅探查用）

Result:
    ScheduleEntry: 构造的条目（entryId = classId#班内序号，不内嵌时间）

Remark:
    endSection = section + duration - 1：多学时课程一次课跨多节，
    由冲突表/UI 按节次区间展开。
    entryId 形如 classId#seq（如 C102#2），序号由调用方按班内落子顺序传入，
    绝不内嵌星期/节次；时间信息另存 timeSlot，展示层现算人读标签。
*/
ScheduleEntry makeEntry(const TeachingClass &tc, int day, int section,
                        const QString &room, const QPair<int, int> &weeks,
                        int duration = 1, int seq = 0)
{
    ScheduleEntry entry;
    entry.entryId              = tc.classId + '#' + QString::number(seq);
    entry.teachingClassId      = tc.classId;
    entry.teacherId            = tc.teacherId;
    entry.classroomId          = room;
    entry.timeSlot.dayOfWeek     = day;
    entry.timeSlot.startSection = section;
    entry.timeSlot.endSection   = section + duration - 1;
    entry.startWeek = weeks.first;
    entry.endWeek   = weeks.second;
    return entry;
}

/*
placeClassInPattern - 为一个教学班在给定时间模式上找"同班同教室"的排法

Parameter：
    tc: 教学班
    patternDays: 模式内的星期序列
    weeks: (起始周, 结束周)
    requiredType: 课程所需教室类型（Any = 不限）
    duration: 一次课占用的连续节数
    maxSection: 作息表最大节次号
    rooms: 教室列表（容量升序）
    table: 冲突表（引用；成功时落子）
    store: 数据仓库（引用；成功时写入条目）

Result:
    bool: 该班 N 次课全部排下返回 true，否则 false

Remark:
    best-fit：按容量升序选第一个「容量 ≥ 人数、类型匹配所需类型（H5）、
    且在模式每个星期都有连续 duration 个空闲节次」的教室；各星期独立取该教室首个空闲空窗；
    全部空闲才统一落子，避免产生半截排课。
*/
bool placeClassInPattern(const TeachingClass &tc, const QVector<int> &patternDays,
                         const QPair<int, int> &weeks, ClassroomType requiredType,
                         int duration, int maxSection,
                         const QVector<Classroom> &rooms, ConflictTable &table,
                         DataStore &store)
{
    for (const Classroom &room : rooms) {
        if (!roomrules::roomOk(room.capacity, room.type, tc.plannedSize, requiredType))
            continue;   // H4（容量）与 H5（类型）判定，单源在 roomrules

        // 检查该教室在模式每个星期是否有连续 duration 个空闲节次
        QVector<int> sections;
        sections.reserve(patternDays.size());
        bool ok = true;
        for (int day : patternDays) {
            int sec = -1;
            for (int s = 1; s + duration - 1 <= maxSection; ++s) {
                if (table.canPlace(makeEntry(tc, day, s, room.roomNumber, weeks, duration))) {
                    sec = s;
                    break;
                }
            }
            if (sec < 0) {
                ok = false;
                break;
            }
            sections.append(sec);
        }
        if (!ok)
            continue;

        // 同班同教室：全部星期空闲才统一落子
        for (int i = 0; i < patternDays.size(); ++i) {
            const ScheduleEntry entry =
                makeEntry(tc, patternDays.at(i), sections.at(i), room.roomNumber,
                          weeks, duration, i + 1);
            table.place(entry);
            store.addScheduleEntry(entry);
        }
        return true;
    }
    return false;
}

/*
roomTypeLabel - 教室类型枚举转中文描述

Parameter：
    type: ClassroomType 枚举

Result:
    QString: "普通教室" / "机房" / "操场" / "不限"
*/
QString roomTypeLabel(ClassroomType type)
{
    switch (type) {
    case ClassroomType::Norm:       return QStringLiteral("普通教室");
    case ClassroomType::Lab:        return QStringLiteral("机房");
    case ClassroomType::PlayGround: return QStringLiteral("操场");
    default:                        return QStringLiteral("不限");
    }
}

/*
diagnoseFailure - 归因一个教学班排不下的原因

Parameter：
    tc: 教学班
    course: 课程模板（含所需教室类型）
    rooms: 教室列表

Result:
    QString: 人类可读的失败原因（给用户照着改课程）

Remark:
    按优先级归因：① 没有匹配所需类型的教室；② 类型匹配但最大容量仍不足；
    ③ 类型/容量都满足但所有离散时间模式均排不下（时间冲突）。
*/
QString diagnoseFailure(const TeachingClass &tc, const Course &course,
                        const QVector<Classroom> &rooms)
{
    const bool anyType = (course.requiredRoomType == ClassroomType::Any);
    const QString typeLabel = roomTypeLabel(course.requiredRoomType);

    int bestCap = 0;
    bool hasTypeRoom = false;
    for (const Classroom &r : rooms) {
        if (!roomrules::typeOk(r.type, course.requiredRoomType))
            continue;   // H5 共享判定（roomrules 单源）
        hasTypeRoom = true;
        bestCap = qMax(bestCap, r.capacity);
    }

    if (!hasTypeRoom)
        return QStringLiteral("没有「%1」类型的教室可用（课程要求 %1）").arg(typeLabel);

    if (bestCap < tc.plannedSize) {
        if (anyType)
            return QStringLiteral("最大教室容量 %1 人，仍小于本班 %2 人")
                       .arg(bestCap).arg(tc.plannedSize);
        return QStringLiteral("「%1」类型教室最大容量 %2 人，仍小于本班 %3 人")
                   .arg(typeLabel).arg(bestCap).arg(tc.plannedSize);
    }

    return QStringLiteral("类型与容量合适的教室存在，但所有离散时间模式均排不下（时间冲突）");
}

} // namespace

/*
GreedyStrategy::run - 贪心排课（支持局部排课：冻结班作背景占位）

Parameter：
    store: 数据仓库；排课结果经 addScheduleEntry 写回
    ctx: 进度/取消上下文；nullptr 时不启用
    movableClasses: 本次可动教学班集合；空 = 全量排课（所有班都排，旧行为）

Result:
    ScheduleResult: 排课摘要（ok / 已排条数 / 失败班数 / 失败班级 id 列表）

Remark:
    以课程为单位按难度排序先排难的；每门课生成离散度时间模式，
    其全部教学班共用该模式（同一天、不同教室/节次），教室不足时按 classId
    兜底拆分、间隔逐步放宽；同一教学班固定同一教室。冲突判定由 ConflictTable 保证。
    movableClasses 非空时：先把 store 中现存条目（Scheduler 已回填的冻结班）种入
    冲突表占位，课程内只排本次可动班，冻结班条目原样保留、不重排。
*/
ScheduleResult GreedyStrategy::run(DataStore &store, ScheduleContext *ctx,
                                   const QSet<QString> &movableClasses)
{
    ScheduleResult result;
    const bool partial = !movableClasses.isEmpty();   // 是否局部排课（有冻结背景）

    // 课程维度查询表：周次数 / 周范围 / 代表教师（该课首个教学班）/ 单次课占节数
    QHash<QString, int> sessionsOf;
    QHash<QString, QPair<int, int>> weekOf;
    QHash<QString, QString> teacherOf;
    QHash<QString, int> durationOf;        // 课程 → 单次课占用的连续节数（整数学时）
    QHash<QString, bool> invalidHoursOf;   // 课程 → 学时非法（非整数 / 小于 1 节）
    for (const Course &c : store.courses()) {
        sessionsOf.insert(c.id, c.sessionsPerWeek);
        weekOf.insert(c.id, qMakePair(c.startWeek, c.endWeek));
        const double h = c.hoursPerSession;
        if (h >= 1.0 && qAbs(h - qRound(h)) <= 1e-9)
            durationOf.insert(c.id, int(qRound(h)));
        else
            invalidHoursOf.insert(c.id, true);
    }

    // 教学班按课程分组（组内按 classId 排序，保证兜底拆分确定性）
    QHash<QString, QVector<TeachingClass>> classesOfCourse;
    for (const TeachingClass &tc : store.teachingClasses()) {
        classesOfCourse[tc.courseId].append(tc);
        if (!teacherOf.contains(tc.courseId))
            teacherOf.insert(tc.courseId, tc.teacherId);
    }
    for (auto it = classesOfCourse.begin(); it != classesOfCourse.end(); ++it)
        std::sort(it.value().begin(), it.value().end(),
                  [](const TeachingClass &a, const TeachingClass &b) {
                      return a.classId < b.classId;
                  });

    // 课程按「周次数↓、单次学时↓、代表教师ID↑」排序，先排难排的
    QVector<Course> courses = store.courses();
    std::sort(courses.begin(), courses.end(),
        [&](const Course &a, const Course &b) {
            const int sa = sessionsOf.value(a.id, 0);
            const int sb = sessionsOf.value(b.id, 0);
            if (sa != sb) return sa > sb;
            if (a.hoursPerSession != b.hoursPerSession)
                return a.hoursPerSession > b.hoursPerSession;
            return teacherOf.value(a.id) < teacherOf.value(b.id);
        });

    // 教室按容量升序（best-fit 语义）
    QVector<Classroom> rooms = store.classrooms();
    std::sort(rooms.begin(), rooms.end(),
        [](const Classroom &a, const Classroom &b) { return a.capacity < b.capacity; });

    // 节次上限由作息表最大节次号决定
    int maxSection = 0;
    for (const Section &s : store.sections())
        maxSection = qMax(maxSection, s.index);

    ConflictTable table;

    // 局部排课：冻结班条目已在 store（Scheduler 回填），先种入冲突表作背景占位；
    // 全量排课无冻结条目，行为与旧版一致。
    if (partial) {
        const QVector<ScheduleEntry> &entries = store.scheduleEntries();
        for (const ScheduleEntry &e : entries)
            table.place(e);
    }

    int courseIndex = 0;
    for (const Course &course : courses) {
        // 进度：每门课上报一次（分母 = 课程总数，含被跳过的课）
        if (ctx && ctx->onProgress)
            ctx->onProgress(SchedulePhase::Greedy, courseIndex, int(courses.size()));
        ++courseIndex;
        // 取消：每门课开头检查，中断即停止后续课程（剩余班不计失败、不诊断）
        if (ctx && ctx->cancelled) {
            result.aborted = true;
            break;
        }
        const int need = sessionsOf.value(course.id, 0);
        if (need <= 0)
            continue;
        const auto weeks = weekOf.value(course.id, qMakePair(1, 16));
        const QVector<TeachingClass> all = classesOfCourse.value(course.id);

        // 本次待排教学班：局部排课时只取可动班；整门课都冻结则跳过该课
        QVector<TeachingClass> pending;
        pending.reserve(all.size());
        for (const TeachingClass &tc : all)
            if (!partial || movableClasses.contains(tc.classId))
                pending.append(tc);
        if (pending.isEmpty())
            continue;

        // 学时非法（非整数 / 小于 1 节）：本次待排的教学班全部记失败，不参与排课
        if (invalidHoursOf.value(course.id, false)) {
            for (const TeachingClass &tc : pending) {
                ++result.failedCount;
                result.failedClassIds.append(tc.classId);
                ScheduleFailure fail;
                fail.classId    = tc.classId;
                fail.courseId   = course.id;
                fail.courseName = course.name;
                fail.reason     = QStringLiteral("单次学时 %1 非法：必须是 ≥1 的整数（节）")
                                      .arg(course.hoursPerSession);
                result.failures.append(fail);
            }
            continue;
        }

        const int duration = durationOf.value(course.id, 1);
        // 待排教学班（已滤除冻结班）共用同一套时间模式；排不下的流入下一（更密）模式
        for (const QVector<int> &pattern : buildPatterns(need)) {
            if (pending.isEmpty())
                break;
            QVector<TeachingClass> unplaced;
            for (const TeachingClass &tc : pending) {
                if (placeClassInPattern(tc, pattern, weeks, course.requiredRoomType,
                                        duration, maxSection, rooms, table, store))
                    result.scheduledCount += need;
                else
                    unplaced.append(tc);
            }
            pending = unplaced;
        }

        // 所有模式都排不下的教学班记入失败（附归因，供 UI 展示/持久化）
        for (const TeachingClass &tc : pending) {
            ++result.failedCount;
            result.failedClassIds.append(tc.classId);
            ScheduleFailure fail;
            fail.classId    = tc.classId;
            fail.courseId   = course.id;
            fail.courseName = course.name;
            fail.reason     = diagnoseFailure(tc, course, rooms);
            result.failures.append(fail);
        }
    }

    result.ok = (result.failedCount == 0);
    return result;
}
