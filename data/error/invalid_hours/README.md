# error/invalid_hours —— 学时非整数或小于 1

**类别**：错误数据 · C101 hoursPerSession=1.5、C102 hoursPerSession=0.5，均非法（须为 ≥1 整数节）

## 预期
导入成功；排课时 C101/C102 各自失败（「单次学时 X 非法：必须是 ≥1 的整数（节）」），
C201 对照可成功排入。
