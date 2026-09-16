#ifndef CORE_SCHEDULE_MANUALMOVE_H
#define CORE_SCHEDULE_MANUALMOVE_H

#include <QString>
#include <QStringList>
#include <QVector>

class DataStore;

// 教务手动调整的整批校验与原子落库（不经排课线程/退火）。
// 语义：把一批「同 entryId 的旧条目」按最小变更（只动 时间+教室）整体换新；
// entryId 视为不透明锚点，编辑保持原值不变，旧条目的 classId / teacherId /
// 周范围 / 单次课跨度一律由 core 从旧条目回填——「只许改时间+教室，其余不变」
// 由本模块结构性强制，调用方拼不坏。
namespace manual {

// 一次课可调的最小变更集合（其余字段由旧条目回填）
struct Change {
    QString entryId;        // 锚点：旧条目 id，必须已存在于 store
    int dayOfWeek = 1;      // 新星期（1..7）
    int startSection = 1;   // 新起始节（结束节 = 起始节 + 旧跨度 − 1）
    QString classroomId;    // 新教室
};

// 校验拒绝原因
enum class Reject {
    None,
    EntryMissing,           // entryId 在 store 不存在 / 其教学班·课程缺失
    RoomMissing,            // 教室号查无
    CapacityTooSmall,       // H4：room.capacity < class.plannedSize
    RoomTypeMismatch,       // H5：requiredRoomType != Any 且 room.type 不符
    BadRange,               // 星期越界 / endSection 超作息最大节
    Conflict,               // 时间·教室·教师冲突（H1~H3，含批内自撞）
    AlreadyPlaced,          // 该班已有排课（"从零排入"前提不满足）
    InvalidHours,           // 课程单次学时非法（须 ≥1 的整数节）
    SlotCountMismatch       // 指定次数 ≠ 课程每周应排次数（防御，正常 UI 不可达）
};

// 一次整批校验的结果；ok=false 时其余字段定位到出问题的那条
struct Report {
    bool ok = false;
    Reject reject = Reject::None;
    int  index = -1;        // 出问题的 Change 下标
    QString entryId;        // 该 Change 的 id
    int day = 0, section = 0;   // 命中槽位（供文案定位）
    QStringList busyTags;   // Conflict 细分：busy 键前缀，含 'R'/'C'/'T'
};

// 整批纯校验（只读，不写库）。批量剔除边界：本批每个 Change.entryId 对应的
// 旧条目被剔除，其余（含同班未改次课、锁定班）全部作背景种入冲突表；
// 新条目逐条 canPlace→place，故批内互相冲突也会被命中。
Report validate(const DataStore &store, const QVector<Change> &changes);

// 先 validate，Ok 才逐条 updateScheduleEntry 落库（原子性依赖 modal 单线程
// 下 validate→apply 之间无第三方写入）；失败返回 false 且不写库。
bool apply(DataStore &store, const QVector<Change> &changes);

} // namespace manual

#endif // CORE_SCHEDULE_MANUALMOVE_H
