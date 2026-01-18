// finance.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "finance.h"
#include "utils.h"
#include "sqlite3.h"

//初始化数据库函数
void init_finance_database(void) {
    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        fprintf(stderr, "❌ 无法打开数据库（财务模块）\n");
        return;
    }

    // 启用外键约束
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);

    // 分类表（支持父子结构）
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS categories ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL UNIQUE,"
        "  parent_id INTEGER,"
        "  type TEXT NOT NULL CHECK(type IN ('income', 'expense')), "
        "  FOREIGN KEY(parent_id) REFERENCES categories(id)"
        ");",
        NULL, NULL, NULL);

    // 创建 accounts 表（含 balance）
    const char* create_accounts_sql =
    "CREATE TABLE IF NOT EXISTS accounts ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "name TEXT NOT NULL UNIQUE, "
    "balance REAL DEFAULT 0.0"
    ");";

    if (sqlite3_exec(db, create_accounts_sql, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "创建 accounts 表失败: %s\n", sqlite3_errmsg(db));
    }

    // 成员表（家庭成员）
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS members ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL UNIQUE"
        ");",
        NULL, NULL, NULL);

    // 记录表（核心）
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS records ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  amount REAL NOT NULL CHECK(amount > 0),"
        "  type TEXT NOT NULL CHECK(type IN ('income', 'expense')), "
        "  category_id INTEGER NOT NULL,"
        "  account_id INTEGER NOT NULL,"
        "  member_id INTEGER,"
        "  remark TEXT,"
        "  date TEXT NOT NULL CHECK(date LIKE '____-__-__'),"
        "  created_at TEXT DEFAULT (datetime('now', 'localtime')), "
        "  updated_at TEXT DEFAULT (datetime('now', 'localtime')), "
        "  FOREIGN KEY(category_id) REFERENCES categories(id),"
        "  FOREIGN KEY(account_id) REFERENCES accounts(id),"
        "  FOREIGN KEY(member_id) REFERENCES members(id)"
        ");",
        NULL, NULL, NULL);

    sqlite3_close(db);
}

//辅助：打印表头的通用函数
static void print_separator(int len) {
    for (int i = 0; i < len; i++) printf("-");
    printf("\n");
}

// 辅助函数：检查记录 ID 是否存在
static int record_id_exists(sqlite3* db, int id) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT 1 FROM records WHERE id = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_int(stmt, 1, id);
    int exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

// 辅助函数：判断是否为有效日期（YYYY-MM-DD）
int is_valid_date(const char* date_str) {
    if (strlen(date_str) != 10 || date_str[4] != '-' || date_str[7] != '-') {
        return 0;
    }

    int year, month, day;
    if (sscanf(date_str, "%d-%d-%d", &year, &month, &day) != 3) {
        return 0;
    }

    if (year < 1900 || year > 2100) return 0;
    if (month < 1 || month > 12) return 0;
    if (day < 1 || day > 31) return 0;

    // 简单闰年处理（可选增强）
    int days_in_month[] = {31, 28 + ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)), 31, 30, 31, 30,
                           31, 31, 30, 31, 30, 31};
    return day <= days_in_month[month - 1];
}

//打印收支记录列表表头
static void print_record_header(void) {
    printf("ID   日期        类型   分类                 账户               成员     金额    备注         修改时间\n");
    printf("--------------------------------------------------------------------------------------------------------\n");
}

//打印列表通用函数
static void print_record_row(sqlite3_stmt* stmt) {
    // 字段索引说明（对应 SELECT 顺序）：
    // 0: r.id
    // 1: r.date                → 业务日期
    // 2: r.type                → 'income' 或 'expense'
    // 3: c_parent.name         → 一级分类名（可能 NULL）
    // 4: c_child.name          → 当前分类名
    // 5: a.name                → 账户名
    // 6: m.name                → 成员名（可能 NULL）
    // 7: r.amount
    // 8: r.remark
    // 9: r.updated_at

    int id = sqlite3_column_int(stmt, 0);
    const char* date = (const char*)sqlite3_column_text(stmt, 1);
    const char* type_en = (const char*)sqlite3_column_text(stmt, 2); // 类型字段
    const char* parent_name = (const char*)sqlite3_column_text(stmt, 3); // 一级分类名
    const char* child_name = (const char*)sqlite3_column_text(stmt, 4);  // 当前分类名
    const char* account = (const char*)sqlite3_column_text(stmt, 5);
    const char* member = (const char*)sqlite3_column_text(stmt, 6);
    double amount = sqlite3_column_double(stmt, 7);
    const char* remark = (const char*)sqlite3_column_text(stmt, 8);
    const char* updated_at = (const char*)sqlite3_column_text(stmt, 9);

    // --- 类型转中文 ---
    const char* type_cn = "未知";
    if (type_en) {
        if (strcmp(type_en, "income") == 0) {
            type_cn = "收入";
        } else if (strcmp(type_en, "expense") == 0) {
            type_cn = "支出";
        }
    }

    // --- 构建分类路径 ---
    char category_path[60] = "未分类";
    if (parent_name && strlen(parent_name) > 0) {
        // 有父分类（即二级分类）
        snprintf(category_path, sizeof(category_path), "%s > %s", parent_name, child_name);
    } else {
        // 仅有一级分类
        strncpy(category_path, child_name, sizeof(category_path) - 1);
    }

    // --- 处理空值显示 ---
    const char* disp_account = (account != NULL && account[0] != '\0') ? account : "-";
    const char* disp_member = (member != NULL && member[0] != '\0') ? member : "-";
    const char* disp_remark = (remark != NULL && remark[0] != '\0') ? remark : "";
    const char* disp_date = date ? date : "";
    const char* disp_updated = updated_at ? updated_at : "";

    // --- 打印行（严格对齐表头）---
    printf("%-3d %-12s %-8s %-20s %-20s %-8s %-8.2f %-20s %-12s\n",
           id,
           disp_date,
           type_cn,
           category_path,
           disp_account,
           disp_member,
           amount,
           disp_remark,
           disp_updated);
}

// 在事务内安全更新账户余额（delta 可正可负）
static int apply_balance_delta(sqlite3* db, int account_id, double delta) {
    if (account_id <= 0) return 0;

    sqlite3_stmt* stmt;
    const char* sql = "UPDATE accounts SET balance = balance + ? WHERE id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_double(stmt, 1, delta);
    sqlite3_bind_int(stmt, 2, account_id);

    int ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

// 添加收支记录函数
void add_record(void) {
    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库: %s\n", sqlite3_errmsg(db));
        return;
    }

    char input[256] = {0};
    char date[11] = {0};
    char type_str[20] = {0};
    double amount = 0.0;
    char remark[100] = {0};

    // === 1. 输入日期（带完整校验）===
    while (1) {
        printf("请输入日期 (YYYY-MM-DD) [按 Enter 使用今天]: ");
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = 0;

        if (input[0] == '\0') {
            // 使用当前日期
            time_t t = time(NULL);
            struct tm* tm_info = localtime(&t);
            strftime(date, sizeof(date), "%Y-%m-%d", tm_info);
            break;
        }

        if (is_valid_date(input)) {
            strcpy(date, input);
            break;
        } else {
            printf("❌ 日期无效！请重新输入。\n");
        }
    }

    // === 2. 选择类型（中文菜单） ===
    while (1) {
        printf("请选择类型:\n");
        printf("1. 收入\n");
        printf("2. 支出\n");
        printf("请输入选项 (1/2): ");

        char choice_input[10];
        if (fgets(choice_input, sizeof(choice_input), stdin) == NULL) {
            printf("❌ 输入错误，请重试。\n");
            continue;
        }

        // 去掉换行符
        choice_input[strcspn(choice_input, "\n")] = 0;

        if (strcmp(choice_input, "1") == 0) {
            strcpy(type_str, "income");
            break;
        } else if (strcmp(choice_input, "2") == 0) {
            strcpy(type_str, "expense");
            break;
        } else {
            printf("❌ 无效选项，请输入 1 或 2。\n");
        }
    }

    // === 3. 选择分类 ===
    int category_id = select_category(type_str);
    if (category_id == -1) {
        printf("❌ 分类选择失败。\n");
        sqlite3_close(db);
        return;
    }

    // === 4. 选择账户 ===
    int account_id = select_account();
    if (account_id == -1) {
        printf("❌ 账户选择失败。\n");
        sqlite3_close(db);
        return;
    }

    // === 5. 选择成员（可选）===
    int member_id = select_member(); // 返回成员 ID，默认可选“本人”
    if (member_id == -1) {
        member_id = 1; // 默认“本人”
    }

    // === 6. 输入金额 ===
    while (1) {
        printf("请输入金额: ");
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = 0;

        char* endptr;
        amount = strtod(input, &endptr);

        if (*endptr == '\0' && amount > 0) {
            break;
        } else {
            printf("❌ 金额必须是大于 0 的数字！\n");
        }
    }

    // === 7. 输入备注（可选）===
    printf("备注 (可选): ");
    fgets(remark, sizeof(remark), stdin);
    remark[strcspn(remark, "\n")] = 0;

    // === 8. 插入数据库（使用事务保证完整性）===
    sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);

    sqlite3_stmt* stmt;
    const char* sql = 
        "INSERT INTO records (date, type, category_id, amount, account_id, member_id, remark, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, datetime('now', 'localtime'));";

    int success = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, date, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, type_str, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, category_id);
        sqlite3_bind_double(stmt, 4, amount);
        sqlite3_bind_int(stmt, 5, account_id);
        sqlite3_bind_int(stmt, 6, member_id);
        sqlite3_bind_text(stmt, 7, remark[0] ? remark : NULL, -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            success = 1;
        } else {
            printf("❌ 插入失败: %s\n", sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
    } else {
        printf("❌ SQL 准备失败: %s\n", sqlite3_errmsg(db));
    }

    if (success) {
        // ✅ 计算 delta 并更新余额（仍在事务中）
        double delta = (strcmp(type_str, "income") == 0) ? amount : -amount;
        if (!apply_balance_delta(db, account_id, delta)) {
            printf("⚠️  警告：账户余额更新失败，但记录已保存。\n");
            // 可选择回滚，但通常记录更重要，这里仅警告
        }
        sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
        printf("✅ 记录添加成功！\n"); // 现在才提示成功
    } else {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    }

    sqlite3_close(db);
}

// 编辑收支记录函数
void edit_record(void) {
    // 显示记录列表（第一页）
    list_records();

    int id;
    printf("\n请输入要编辑的记录 ID（输入 0 取消）: ");
    if (scanf("%d", &id) != 1) {
        while (getchar() != '\n'); // 清空输入缓冲区
        printf("❌ 输入无效，请输入数字。\n");
        return;
    }
    getchar(); // 清除可能的换行（虽 %d 不会留，但保险）

    if (id == 0) {
        printf("❌ 已取消编辑。\n");
        return;
    }

    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库: %s\n", sqlite3_errmsg(db));
        return;
    }

    // 检查记录是否存在，并加载原始数据
    sqlite3_stmt* load_stmt;
    const char* load_sql = 
        "SELECT date, type, category_id, account_id, member_id, amount, remark "
        "FROM records WHERE id = ?;";
    if (sqlite3_prepare_v2(db, load_sql, -1, &load_stmt, NULL) != SQLITE_OK) {
        printf("❌ 查询记录失败: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }
    sqlite3_bind_int(load_stmt, 1, id);

    if (sqlite3_step(load_stmt) != SQLITE_ROW) {
        printf("❌ 记录 ID %d 不存在！\n", id);
        sqlite3_finalize(load_stmt);
        sqlite3_close(db);
        return;
    }

    // 保存原始值
    char orig_date[11] = {0};
    char orig_type[20] = {0};
    int orig_category_id = sqlite3_column_int(load_stmt, 2);
    int orig_account_id = sqlite3_column_int(load_stmt, 3);
    int orig_member_id = sqlite3_column_int(load_stmt, 4);
    double orig_amount = sqlite3_column_double(load_stmt, 5);
    const char* orig_remark = (const char*)sqlite3_column_text(load_stmt, 6);

    strncpy(orig_date, (const char*)sqlite3_column_text(load_stmt, 0), sizeof(orig_date) - 1);
    strncpy(orig_type, (const char*)sqlite3_column_text(load_stmt, 1), sizeof(orig_type) - 1);

    sqlite3_finalize(load_stmt);

    // --- 开始编辑 ---
    char input[256] = {0};
    char new_date[11] = {0};
    char new_type[20] = {0};
    int new_category_id = orig_category_id;
    int new_account_id = orig_account_id;
    int new_member_id = orig_member_id;
    double new_amount = orig_amount;
    char new_remark[100] = {0};

    strcpy(new_date, orig_date);
    strcpy(new_type, orig_type);
    if (orig_remark) strncpy(new_remark, orig_remark, sizeof(new_remark) - 1);

    printf("\n--- 编辑记录 (ID=%d) ---\n", id);
    printf("提示：直接按 Enter 保留原值，输入新值则覆盖。\n\n");

    // 1. 日期
    printf("日期 [%s]: ", orig_date);
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;
    if (input[0] != '\0') {
        if (is_valid_date(input)) {
            strcpy(new_date, input);
        } else {
            printf("⚠️ 日期格式无效，保留原值 \"%s\"\n", orig_date);
        }
    }

    // 2. 类型（注意：切换类型会改变可用分类！）
    printf("类型 [%s] (income/expense): ", orig_type);
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;
    if (input[0] != '\0') {
        if (strcmp(input, "income") == 0 || strcmp(input, "expense") == 0) {
            strcpy(new_type, input);
        } else {
            printf("⚠️ 类型无效，保留原值 \"%s\"\n", orig_type);
        }
    }

    // 3. 分类（根据当前类型过滤）
    printf("当前分类需匹配类型 \"%s\"\n", new_type);
    int temp_cat_id = select_category(new_type); // 允许用户重新选择
    if (temp_cat_id != -1) {
        new_category_id = temp_cat_id;
    } else {
        printf("⚠️ 分类未更改，保留原分类。\n");
    }

    // 4. 账户
    printf("账户: 输入任意键重新选择，否则保留原账户。\n");
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;
    if (input[0] != '\0') {
        int temp_acc_id = select_account();
        if (temp_acc_id != -1) {
            new_account_id = temp_acc_id;
        } else {
            printf("⚠️ 账户未更改。\n");
        }
    }

    // 5. 成员（可选）
    printf("成员: 输入任意键重新选择，否则保留原成员。\n");
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;
    if (input[0] != '\0') {
        int temp_mem_id = select_member(); // 假设你已实现此函数
        if (temp_mem_id != -1) {
            new_member_id = temp_mem_id;
        } else {
            printf("⚠️ 成员未更改，使用默认。\n");
            new_member_id = 1; // 默认“本人”
        }
    }

    // 6. 金额
    printf("金额 [%.2f]: ", orig_amount);
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;
    if (input[0] != '\0') {
        char* endptr;
        double val = strtod(input, &endptr);
        if (*endptr == '\0' && val > 0) {
            new_amount = val;
        } else {
            printf("⚠️ 金额无效，保留原值 %.2f\n", orig_amount);
        }
    }

    // 7. 备注
    printf("备注 [%s]: ", orig_remark ? orig_remark : "无");
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;
    if (input[0] != '\0') {
        strncpy(new_remark, input, sizeof(new_remark) - 1);
    } else if (input[0] == '\0' && orig_remark) {
        // 用户按 Enter → 保留原备注
        strncpy(new_remark, orig_remark, sizeof(new_remark) - 1);
    } // 否则 new_remark 保持为空

    // === 执行更新 ===
    sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);

    sqlite3_stmt* update_stmt;
    const char* update_sql =
        "UPDATE records SET "
        "date = ?, type = ?, category_id = ?, account_id = ?, member_id = ?, "
        "amount = ?, remark = ?, updated_at = datetime('now', 'localtime') "
        "WHERE id = ?;";

    int success = 0;
    if (sqlite3_prepare_v2(db, update_sql, -1, &update_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(update_stmt, 1, new_date, -1, SQLITE_STATIC);
        sqlite3_bind_text(update_stmt, 2, new_type, -1, SQLITE_STATIC);
        sqlite3_bind_int(update_stmt, 3, new_category_id);
        sqlite3_bind_int(update_stmt, 4, new_account_id);
        sqlite3_bind_int(update_stmt, 5, new_member_id);
        sqlite3_bind_double(update_stmt, 6, new_amount);
        sqlite3_bind_text(update_stmt, 7, new_remark[0] ? new_remark : NULL, -1, SQLITE_STATIC);
        sqlite3_bind_int(update_stmt, 8, id);

        if (sqlite3_step(update_stmt) == SQLITE_DONE) {
            if (sqlite3_changes(db) > 0) {
                success = 1;
            } else {
                printf("\n⚠️ 无更改或记录已被删除。\n");
            }
        } else {
            printf("\n❌ 更新失败: %s\n", sqlite3_errmsg(db));
        }
        sqlite3_finalize(update_stmt);
    } else {
        printf("\n❌ 准备更新语句失败: %s\n", sqlite3_errmsg(db));
    }

    if (success) {
        // === 关键：同步更新账户余额 ===
        int balance_ok = 1;

        // 1. 撤销原记录对原账户的影响
        double old_delta = (strcmp(orig_type, "income") == 0) ? -orig_amount : orig_amount;
        if (!apply_balance_delta(db, orig_account_id, old_delta)) {
            printf("⚠️  警告：无法撤销原账户余额变更。\n");
            balance_ok = 0;
        }

        // 2. 应用新记录对新账户的影响
        double new_delta = (strcmp(new_type, "income") == 0) ? new_amount : -new_amount;
        if (!apply_balance_delta(db, new_account_id, new_delta)) {
            printf("⚠️  警告：无法应用新账户余额变更。\n");
            balance_ok = 0;
        }

        // 可选：如果余额更新失败，是否回滚？这里选择提交（记录优先）
        sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

        if (balance_ok) {
            printf("✅ 记录及账户余额已同步更新！\n");
        } else {
            printf("⚠️  记录已更新，但账户余额可能不一致，请检查。\n");
        }
    } else {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    }

    sqlite3_close(db);
}

// 删除收支记录函数（完善版）
void delete_record(void) {
    // 先显示所有记录（第一页）
    list_records();

    int id;
    printf("\n请输入要删除的记录 ID（输入 0 取消）: ");
    if (scanf("%d", &id) != 1) {
        // 清除无效输入
        while (getchar() != '\n');
        printf("❌ 输入无效，请输入数字。\n");
        return;
    }
    getchar(); // 清除换行符（虽然 scanf %d 不会留下 \n，但保险）

    if (id == 0) {
        printf("❌ 已取消删除。\n");
        return;
    }

    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库: %s\n", sqlite3_errmsg(db));
        return;
    }

    if (!record_id_exists(db, id)) {
        printf("❌ 记录 ID %d 不存在！\n", id);
        sqlite3_close(db);
        return;
    }

    // 获取记录简要信息用于确认（提升体验）
    sqlite3_stmt* info_stmt;
    const char* info_sql = 
        "SELECT date, type, amount, remark FROM records WHERE id = ?;";
    if (sqlite3_prepare_v2(db, info_sql, -1, &info_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(info_stmt, 1, id);
        if (sqlite3_step(info_stmt) == SQLITE_ROW) {
            const char* date = (const char*)sqlite3_column_text(info_stmt, 0);
            const char* type = (const char*)sqlite3_column_text(info_stmt, 1);
            double amount = sqlite3_column_double(info_stmt, 2);
            const char* remark = (const char*)sqlite3_column_text(info_stmt, 3);
            const char* type_cn = (strcmp(type, "income") == 0) ? "收入" : "支出";
            printf("\n即将删除:\n");
            printf("  ID: %d\n", id);
            printf("  日期: %s\n", date ? date : "未知");
            printf("  类型: %s\n", type_cn);
            printf("  金额: %.2f\n", amount);
            printf("  备注: %s\n", remark && strlen(remark) > 0 ? remark : "无");
        }
        sqlite3_finalize(info_stmt);
    }

    // 二次确认（修复 scanf("%c") 问题）
    char input[10];
    printf("\n⚠️ 确定要永久删除此记录吗？(输入 y/Y 确认，其他取消): ");
    if (fgets(input, sizeof(input), stdin) == NULL) {
        printf("\n❌ 输入错误，已取消。\n");
        sqlite3_close(db);
        return;
    }
    input[strcspn(input, "\n")] = 0; // 去掉换行

    if (input[0] != 'y' && input[0] != 'Y') {
        printf("❌ 已取消删除。\n");
        sqlite3_close(db);
        return;
    }

    // 执行删除（使用事务）
    sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);

    // === 新增：先获取记录详情用于余额调整 ===
    int account_id = -1;
    char type_str[20] = {0};
    double amount = 0.0;

    sqlite3_stmt* fetch_stmt;
    const char* fetch_sql = "SELECT account_id, type, amount FROM records WHERE id = ?;";
    if (sqlite3_prepare_v2(db, fetch_sql, -1, &fetch_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(fetch_stmt, 1, id);
        if (sqlite3_step(fetch_stmt) == SQLITE_ROW) {
            account_id = sqlite3_column_int(fetch_stmt, 0);
            const char* type = (const char*)sqlite3_column_text(fetch_stmt, 1);
            amount = sqlite3_column_double(fetch_stmt, 2);
            if (type) strncpy(type_str, type, sizeof(type_str) - 1);
        }
        sqlite3_finalize(fetch_stmt);
    }

    int success = 0;
    if (account_id <= 0) {
        printf("❌ 无法获取记录详情，删除中止。\n");
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        sqlite3_close(db);
        return;
    }

    // === 1. 先更新账户余额（撤销影响）===
    double delta = (strcmp(type_str, "income") == 0) ? -amount : amount;
    if (!apply_balance_delta(db, account_id, delta)) {
        printf("⚠️  警告：账户余额回滚失败，但将继续删除记录。\n");
        // 可选择回滚，但通常记录删除更重要
    }

    // === 2. 再删除记录 ===
    sqlite3_stmt* del_stmt;
    const char* del_sql = "DELETE FROM records WHERE id = ?;";
    if (sqlite3_prepare_v2(db, del_sql, -1, &del_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(del_stmt, 1, id);
        if (sqlite3_step(del_stmt) == SQLITE_DONE) {
            int changes = sqlite3_changes(db);
            if (changes > 0) {
                success = 1;
                // 不在这里打印成功！移到 COMMIT 后
            } else {
                printf("❌ 删除失败：记录可能已被其他操作移除。\n");
            }
        } else {
            printf("❌ SQL 执行失败: %s\n", sqlite3_errmsg(db));
        }
        sqlite3_finalize(del_stmt);
    } else {
        printf("❌ 准备删除语句失败: %s\n", sqlite3_errmsg(db));
    }

    // === 提交或回滚 ===
    if (success) {
        sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
        printf("✅ 记录 ID=%d 已成功删除，账户余额已同步更新！\n", id);
    } else {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    }

    sqlite3_close(db);
}

//显示所有收支记录函数（分页显示）
void list_records(void) {
    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库: %s\n", sqlite3_errmsg(db));
        return;
    }

    // 获取总记录数
    sqlite3_stmt* count_stmt;
    int total_records = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM records;", -1, &count_stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            total_records = sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }

    if (total_records == 0) {
        printf("📭 暂无财务记录。\n");
        sqlite3_close(db);
        return;
    }

    const int PAGE_SIZE = 8; // 略微减少，因列变宽
    int current_page = 0;
    char input[20];

    while (1) {
        clear_screen(); // 清屏函数

        printf("=== 所有财务记录 (共 %d 条) ===\n", total_records);
        print_record_header(); // 使用你更新后的表头

        // ⭐ 核心 SQL：JOIN 分类（父子）、账户、成员
        const char* sql = 
        "SELECT "
        "    r.id, "
        "    r.date, "
        "    r.type, "
        "    c_parent.name, "        //-- 父分类名（通过 parent_id = id 关联）
        "    c_child.name, "         //-- 当前分类名
        "    a.name, "
        "    m.name, "
        "    r.amount, "
        "    r.remark, "
        "    r.updated_at "
        "FROM records r "
        "JOIN categories c_child ON r.category_id = c_child.id "       //-- 按 ID 找子分类
        "LEFT JOIN categories c_parent ON c_child.parent_id = c_parent.id "  //-- ⭐ 按 ID 找父分类！
        "JOIN accounts a ON r.account_id = a.id "
        "LEFT JOIN members m ON r.member_id = m.id "
        "ORDER BY r.date DESC, r.id DESC "
        "LIMIT ? OFFSET ?;";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
            printf("❌ 查询失败: %s\n", sqlite3_errmsg(db));
            sqlite3_close(db);
            return;
        }

        sqlite3_bind_int(stmt, 1, PAGE_SIZE);
        sqlite3_bind_int(stmt, 2, current_page * PAGE_SIZE);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            print_record_row(stmt); // 行打印
        }
        sqlite3_finalize(stmt);

        // 分页控制
        int total_pages = (total_records + PAGE_SIZE - 1) / PAGE_SIZE;
        printf("\n【第 %d/%d 页】", current_page + 1, total_pages);
        if (current_page > 0) {
            printf(" [P]上一页");
        }
        if ((current_page + 1) * PAGE_SIZE < total_records) {
            printf(" [N]下一页");
        }
        printf(" [Q]返回: ");

        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        input[strcspn(input, "\n")] = 0;

        if (strcasecmp(input, "Q") == 0) {
            break;
        } else if (strcasecmp(input, "N") == 0) {
            if ((current_page + 1) * PAGE_SIZE < total_records) {
                current_page++;
            }
        } else if (strcasecmp(input, "P") == 0) {
            if (current_page > 0) {
                current_page--;
            }
        }
    }

    sqlite3_close(db);
}

//导出收支记录到CSV的函数
void export_to_csv(void) {
    char filename[100];
    printf("请输入导出文件名（默认: records.csv）: ");
    if (fgets(filename, sizeof(filename), stdin) == NULL) {
        strcpy(filename, "records.csv\n");
    }
    filename[strcspn(filename, "\n")] = 0;
    if (strlen(filename) == 0) {
        strcpy(filename, "records.csv");
    }
    if (strstr(filename, ".csv") == NULL) {
        strcat(filename, ".csv");
    }

    FILE* fp = fopen(filename, "w");
    if (!fp) {
        printf("❌ 无法创建文件 \"%s\"（权限不足或路径无效）\n", filename);
        return;
    }

    // 写入 UTF-8 BOM（确保 Excel 正确识别中文）
    fprintf(fp, "\xEF\xBB\xBF");

    // 写入表头
    fprintf(fp, "ID,日期,类型,父分类,子分类,账户,成员,金额,备注,更新时间\n");

    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库: %s\n", sqlite3_errmsg(db));
        fclose(fp);
        return;
    }

    // 使用与 list_records 相同的 SQL
    const char* sql = 
        "SELECT "
        "    r.id, "
        "    r.date, "
        "    r.type, "
        "    c_parent.name, "
        "    c_child.name, "
        "    a.name, "
        "    m.name, "
        "    r.amount, "
        "    r.remark, "
        "    r.updated_at "
        "FROM records r "
        "JOIN categories c_child ON r.category_id = c_child.id "
        "LEFT JOIN categories c_parent ON c_child.parent_id = c_parent.id "
        "JOIN accounts a ON r.account_id = a.id "
        "LEFT JOIN members m ON r.member_id = m.id "
        "ORDER BY r.date, r.id;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("❌ 查询失败: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        fclose(fp);
        return;
    }

    #define CSV_ESCAPE_BUF_SIZE 1024
    char escaped[CSV_ESCAPE_BUF_SIZE];
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* date = (const char*)sqlite3_column_text(stmt, 1);
        const char* type_raw = (const char*)sqlite3_column_text(stmt, 2);
        const char* parent_cat = (const char*)sqlite3_column_text(stmt, 3);
        const char* child_cat = (const char*)sqlite3_column_text(stmt, 4);
        const char* account = (const char*)sqlite3_column_text(stmt, 5);
        const char* member = (const char*)sqlite3_column_text(stmt, 6);
        double amount = sqlite3_column_double(stmt, 7);
        const char* remark = (const char*)sqlite3_column_text(stmt, 8);
        const char* updated_at = (const char*)sqlite3_column_text(stmt, 9);

        const char* type_cn = (strcmp(type_raw, "income") == 0) ? "收入" : "支出";

        // 转义各字段
        csv_escape(parent_cat, escaped, sizeof(escaped));
        char parent_escaped[256]; strcpy(parent_escaped, escaped);

        csv_escape(child_cat, escaped, sizeof(escaped));
        char child_escaped[256]; strcpy(child_escaped, escaped);

        csv_escape(account, escaped, sizeof(escaped));
        char account_escaped[256]; strcpy(account_escaped, escaped);

        csv_escape(member, escaped, sizeof(escaped));
        char member_escaped[256]; strcpy(member_escaped, escaped);

        csv_escape(remark, escaped, sizeof(escaped));
        char remark_escaped[256]; strcpy(remark_escaped, escaped);

        // 写入一行
        fprintf(fp, "%d,%s,%s,%s,%s,%s,%s,%.2f,%s,%s\n",
                id,
                date ? date : "",
                type_cn,
                parent_escaped,
                child_escaped,
                account_escaped,
                member_escaped,
                amount,
                remark_escaped,
                updated_at ? updated_at : ""
        );
        count++;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    fclose(fp);

    printf("✅ 成功导出 %d 条记录到 \"%s\"\n", count, filename);
}

//按日期查询收支记录的函数
void query_by_date(void) {
    char input[20];
    printf("请输入日期 (格式: YYYY-MM-DD，如 2026-01-17): ");
    if (fgets(input, sizeof(input), stdin) == NULL) {
        printf("❌ 输入失败。\n");
        return;
    }
    input[strcspn(input, "\n")] = 0;

    if (strlen(input) != 10 || input[4] != '-' || input[7] != '-') {
        printf("❌ 日期格式错误！应为 YYYY-MM-DD\n");
        return;
    }

    // 可选：增强校验
    if (!is_valid_date(input)) {
        printf("❌ 日期无效（如 2026-99-99）！\n");
        return;
    }

    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库: %s\n", sqlite3_errmsg(db));
        return;
    }

    // 使用与 list_records 相同的 SQL，仅添加 WHERE date = ?
    const char* sql = 
        "SELECT "
        "    r.id, "
        "    r.date, "
        "    r.type, "
        "    c_parent.name, "         //-- 父分类
        "    c_child.name, "          //-- 子分类
        "    a.name, "                //-- 账户
        "    m.name, "                //-- 成员
        "    r.amount, "
        "    r.remark, "
        "    r.updated_at "
        "FROM records r "
        "JOIN categories c_child ON r.category_id = c_child.id "
        "LEFT JOIN categories c_parent ON c_child.parent_id = c_parent.id "
        "JOIN accounts a ON r.account_id = a.id "
        "LEFT JOIN members m ON r.member_id = m.id "
        "WHERE r.date = ? "
        "ORDER BY r.date DESC, r.id DESC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("❌ 查询准备失败: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    sqlite3_bind_text(stmt, 1, input, -1, SQLITE_STATIC);

    print_record_header();
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        print_record_row(stmt);
        found = 1;
    }

    if (!found) {
        printf("📝 未找到 %s 的记录。\n", input);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

//按分类查询收支记录的函数
void query_by_category(void) {
    char input[50];
    printf("请输入分类关键词（如“餐饮”、“工资”）: ");
    if (fgets(input, sizeof(input), stdin) == NULL) {
        printf("❌ 输入失败。\n");
        return;
    }
    input[strcspn(input, "\n")] = 0;

    if (strlen(input) == 0) {
        printf("❌ 分类关键词不能为空！\n");
        return;
    }

    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库: %s\n", sqlite3_errmsg(db));
        return;
    }

    // 在子分类或父分类中模糊匹配
    const char* sql = 
        "SELECT "
        "    r.id, "
        "    r.date, "
        "    r.type, "
        "    c_parent.name, "
        "    c_child.name, "
        "    a.name, "
        "    m.name, "
        "    r.amount, "
        "    r.remark, "
        "    r.updated_at "
        "FROM records r "
        "JOIN categories c_child ON r.category_id = c_child.id "
        "LEFT JOIN categories c_parent ON c_child.parent_id = c_parent.id "
        "JOIN accounts a ON r.account_id = a.id "
        "LEFT JOIN members m ON r.member_id = m.id "
        "WHERE c_child.name LIKE ? OR (c_parent.name IS NOT NULL AND c_parent.name LIKE ?) "
        "ORDER BY r.date DESC, r.id DESC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("❌ 查询准备失败: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    char pattern[60];
    snprintf(pattern, sizeof(pattern), "%%%s%%", input);
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC); // 绑定两次

    print_record_header();
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        print_record_row(stmt);
        found = 1;
    }

    if (!found) {
        printf("📝 未找到包含“%s”的分类记录。\n", input);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

//月度统计报表
void show_monthly_report(void) {
    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库: %s\n", sqlite3_errmsg(db));
        return;
    }

    const char* sql = 
        "SELECT "
        "  strftime('%Y-%m', date) AS month, "
        "  SUM(CASE WHEN type = 'income' THEN amount ELSE 0 END) AS total_income, "
        "  SUM(CASE WHEN type = 'expense' THEN amount ELSE 0 END) AS total_expense "
        "FROM records "
        "GROUP BY month "
        "ORDER BY month DESC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("❌ 查询失败: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    printf("\n📊 月度报表（基于业务日期）\n");
    printf("%-8s %-12s %-12s %-12s\n", "年月", "收入", "支出", "结余");
    print_separator(50);

    double grand_income = 0.0, grand_expense = 0.0;
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* month = (const char*)sqlite3_column_text(stmt, 0);
        double income = sqlite3_column_double(stmt, 1);
        double expense = sqlite3_column_double(stmt, 2);
        double balance = income - expense;

        printf("%-8s %-12.2f %-12.2f %-12.2f\n", month, income, expense, balance);
        grand_income += income;
        grand_expense += expense;
        found = 1;
    }

    if (!found) {
        printf("📝 暂无记录。\n");
    } else {
        print_separator(50);
        printf("%-8s %-12.2f %-12.2f %-12.2f\n", 
               "总计", grand_income, grand_expense, grand_income - grand_expense);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

//年度统计报表
void show_yearly_report(void) {
    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库: %s\n", sqlite3_errmsg(db));
        return;
    }

    const char* sql = 
        "SELECT "
        "  strftime('%Y', date) AS year, "
        "  SUM(CASE WHEN type = 'income' THEN amount ELSE 0 END) AS total_income, "
        "  SUM(CASE WHEN type = 'expense' THEN amount ELSE 0 END) AS total_expense "
        "FROM records "
        "GROUP BY year "
        "ORDER BY year DESC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("❌ 查询失败: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    printf("\n📊 年度报表（基于业务日期）\n");
    printf("%-6s %-12s %-12s %-12s\n", "年份", "收入", "支出", "结余");
    print_separator(48);

    double grand_income = 0.0, grand_expense = 0.0;
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* year = (const char*)sqlite3_column_text(stmt, 0);
        double income = sqlite3_column_double(stmt, 1);
        double expense = sqlite3_column_double(stmt, 2);
        double balance = income - expense;

        printf("%-6s %-12.2f %-12.2f %-12.2f\n", year, income, expense, balance);
        grand_income += income;
        grand_expense += expense;
        found = 1;
    }

    if (!found) {
        printf("📝 暂无记录。\n");
    } else {
        print_separator(48);
        printf("%-6s %-12.2f %-12.2f %-12.2f\n", 
               "总计", grand_income, grand_expense, grand_income - grand_expense);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

//分类统计报表
void show_category_report(void) {
    char input[10];
    printf("\n📊 分类统计\n");
    printf("请选择类型:\n");
    printf("1. 支出分类\n");
    printf("2. 收入分类\n");
    printf("请选择 (1/2): ");
    if (fgets(input, sizeof(input), stdin) == NULL) {
        printf("❌ 输入失败。\n");
        return;
    }
    input[strcspn(input, "\n")] = 0;

    const char* type_filter;
    const char* report_title;
    if (input[0] == '2') {
        type_filter = "income";
        report_title = "📈 收入分类统计";
    } else {
        type_filter = "expense";
        report_title = "📉 支出分类统计";
    }

    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库: %s\n", sqlite3_errmsg(db));
        return;
    }

    // ✅ 正确 JOIN categories，构建分类路径
    const char* sql = 
        "SELECT "
        "  CASE "
        "    WHEN c_parent.name IS NOT NULL THEN c_parent.name || ' > ' || c_child.name "
        "    ELSE c_child.name "
        "  END AS category_path, "
        "  SUM(r.amount) AS total "
        "FROM records r "
        "JOIN categories c_child ON r.category_id = c_child.id "
        "LEFT JOIN categories c_parent ON c_child.parent_id = c_parent.id "
        "WHERE r.type = ? "
        "GROUP BY category_path "
        "ORDER BY total DESC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("❌ 查询失败: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }
    sqlite3_bind_text(stmt, 1, type_filter, -1, SQLITE_STATIC);

    printf("\n%s\n", report_title);
    printf("%-20s %s\n", "分类", "金额");
    print_separator(30);

    double grand_total = 0.0;
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* category = (const char*)sqlite3_column_text(stmt, 0);
        double total = sqlite3_column_double(stmt, 1);
        printf("%-20s %.2f\n", category, total);
        grand_total += total;
        found = 1;
    }

    if (!found) {
        printf("📝 暂无 %s 记录。\n", 
               strcmp(type_filter, "income") == 0 ? "收入" : "支出");
    } else {
        print_separator(30);
        printf("%-20s %.2f\n", "总计", grand_total);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

//账户选择（扁平列表）
int select_account(void) {
    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, name FROM accounts ORDER BY id;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("❌ 查询账户失败: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    printf("\n--- 选择账户 ---\n");
    int count = 0;
    int capacity = 50;
    int* account_ids = malloc(capacity * sizeof(int));
    if (!account_ids) {
        printf("❌ 内存不足\n");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity *= 2;
            int* tmp = realloc(account_ids, capacity * sizeof(int));
            if (!tmp) {
                free(account_ids);
                sqlite3_finalize(stmt);
                sqlite3_close(db);
                return -1;
            }
            account_ids = tmp;
        }
        account_ids[count] = sqlite3_column_int(stmt, 0);
        const char* name = (const char*)sqlite3_column_text(stmt, 1);
        printf("%d. %s\n", count + 1, name);
        count++;
    }
    sqlite3_finalize(stmt);

    if (count == 0) {
        printf("⚠️ 无可用账户，请先在系统设置中添加。\n");
        free(account_ids);
        sqlite3_close(db);
        return -1;
    }

    int choice;
    printf("请选择账户编号 (1-%d): ", count);
    if (scanf("%d", &choice) != 1) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
    choice = -1;
    }
    getchar(); // 清除换行

    if (choice < 1 || choice > count) {
        printf("❌ 无效选项！\n");
        free(account_ids);
        sqlite3_close(db);
        return -1;
    }

    int selected_id = account_ids[choice - 1];
    free(account_ids);
    sqlite3_close(db);
    return selected_id;
}

// 分类选择（带层级）
int select_category(const char* type) {
    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    // 查询一级分类
    sqlite3_stmt* stmt;
    const char* sql_top = 
        "SELECT id, name FROM categories WHERE type = ? AND parent_id IS NULL ORDER BY id;";
    if (sqlite3_prepare_v2(db, sql_top, -1, &stmt, NULL) != SQLITE_OK) {
        printf("❌ 查询分类失败: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, type, -1, SQLITE_STATIC);

    printf("\n--- 选择%s分类 ---\n", 
           strcmp(type, "income") == 0 ? "收入" : "支出");

    #define MAX_CATEGORIES 200
    int all_ids[MAX_CATEGORIES];
    char all_labels[MAX_CATEGORIES][100];
    int total = 0;

    // 添加一级分类
    while (sqlite3_step(stmt) == SQLITE_ROW && total < MAX_CATEGORIES) {
        int id = sqlite3_column_int(stmt, 0);
        const char* name = (const char*)sqlite3_column_text(stmt, 1);
        all_ids[total] = id;
        snprintf(all_labels[total], sizeof(all_labels[total]), "%s", name);
        total++;
    }
    sqlite3_finalize(stmt);

    if (total == 0) {
        printf("⚠️ 暂无%s分类，请先添加。\n", 
               strcmp(type, "income") == 0 ? "收入" : "支出");
        sqlite3_close(db);
        return -1;
    }

    // 添加子分类
    for (int i = 0; i < total && total < MAX_CATEGORIES; i++) {
        sqlite3_stmt* sub_stmt;
        const char* sql_sub = "SELECT id, name FROM categories WHERE parent_id = ? ORDER BY id;";
        if (sqlite3_prepare_v2(db, sql_sub, -1, &sub_stmt, NULL) != SQLITE_OK) continue;
        sqlite3_bind_int(sub_stmt, 1, all_ids[i]);

        while (sqlite3_step(sub_stmt) == SQLITE_ROW && total < MAX_CATEGORIES) {
            int sub_id = sqlite3_column_int(sub_stmt, 0);
            const char* sub_name = (const char*)sqlite3_column_text(sub_stmt, 1);
            all_ids[total] = sub_id;
            snprintf(all_labels[total], sizeof(all_labels[total]), "  └─ %s", sub_name);
            total++;
        }
        sqlite3_finalize(sub_stmt);
    }

    // 显示完整列表
    printf("\n可用分类:\n");
    for (int i = 0; i < total; i++) {
        printf("%2d. %s\n", i + 1, all_labels[i]);
    }

    int choice;
    printf("请选择编号 (1-%d): ", total);
    if (scanf("%d", &choice) != 1) choice = -1;
    getchar();

    if (choice < 1 || choice > total) {
        printf("❌ 无效选项！\n");
        sqlite3_close(db);
        return -1;
    }

    int selected_id = all_ids[choice - 1];
    sqlite3_close(db);
    return selected_id;
}

//成员选择器函数
int select_member(void) {
    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库\n");
        return -1;
    }

    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, name FROM members ORDER BY id;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }

    printf("\n【成员列表】\n");
    printf("0) 跳过（默认本人）\n");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* name = (const char*)sqlite3_column_text(stmt, 1);
        printf("%d) %s\n", id, name);
    }

    int choice;
    printf("请选择成员 ID: ");
    if (scanf("%d", &choice) != 1) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
    choice = -1;
    }
    getchar(); // 清除换行

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return (choice > 0) ? choice : -1; // -1 表示使用默认
}