# error/empty_rooms_import_fails —— 教室文件为空

**类别**：错误数据 · classrooms.csv 是 0 字节空文件

## 预期
loadCsv 对三个文件任一“打不开或为空”即整体导入失败（返回 false），
App 导入对话框会报错、不进入排课 —— 属导入层失败样例（不可用做正常导入）。
