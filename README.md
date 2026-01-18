# 财务管理系统（C + SQLite）

一个基于 C 语言和 SQLite 的命令行家庭财务管理工具。

## 功能
- 管理元登录（SHA256 加密）
- 收入/支出记录管理
- 收入/支出记录导出
- 分类（支持父子分类）
- 账户、成员管理
- 分页显示记录

## 编译
```bash
mkdir build
cd build
cmake ..
cmake --build .