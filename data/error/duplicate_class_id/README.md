# error/duplicate_class_id —— 同一 classId 出现两次

**类别**：错误数据 · 两行 classId 均为 C101（归属不同课程/教师）

## 预期
导入成功但同一班级 id 出现两个实体，占位/H1 判定共用 C|classId|… 键，
排课与展示会出现互相占用等异常。探测引擎对重复主键的处理。
