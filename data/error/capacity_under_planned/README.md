# error/capacity_under_planned —— 计划人数 > 最大容纳

**类别**：错误数据 · C101 plannedSize=80 而 maxCapacity=40（自相矛盾）

## 预期
可正常导入与排课（排课只看 plannedSize，教室容量足则成功）；
但编辑口径里“计划 > 上限”属数据不自洽，用于检验 UI/编辑对该字段组合的容忍。
