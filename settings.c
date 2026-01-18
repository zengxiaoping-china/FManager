// settings.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite3.h"
#include "auth.h"
#include "sha256.h"
#include "utils.h"
#include "finance.h"
#include "settings.h"

// 显示所有分类（一级 + 二级）
static void list_all_categories(sqlite3* db) {
    printf("\n--- 所有分类 ---\n");
    printf("一级分类:\n");

    sqlite3_stmt* stmt;
    // 先查一级分类 (parent_id IS NULL or 0)
    const char* sql1 = 
        "SELECT id, name FROM categories WHERE parent_id IS NULL OR parent_id = 0 ORDER BY id;";
    if (sqlite3_prepare_v2(db, sql1, -1, &stmt, NULL) != SQLITE_OK) {
        printf("❌ 查询一级分类失败\n");
        return;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* name = (const char*)sqlite3_column_text(stmt, 1);
        printf("  [%d] %s\n", id, name);

        // 查对应的二级分类
        sqlite3_stmt* sub_stmt;
        const char* sql2 = "SELECT id, name FROM categories WHERE parent_id = ? ORDER BY id;";
        if (sqlite3_prepare_v2(db, sql2, -1, &sub_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(sub_stmt, 1, id);
            while (sqlite3_step(sub_stmt) == SQLITE_ROW) {
                int sub_id = sqlite3_column_int(sub_stmt, 0);
                const char* sub_name = (const char*)sqlite3_column_text(sub_stmt, 1);
                printf("    └─ [%d] %s\n", sub_id, sub_name);
            }
            sqlite3_finalize(sub_stmt);
        }
    }
    sqlite3_finalize(stmt);
}

// 显示所有成员（供编辑/删除前参考）
static void list_members(void) {
    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库\n");
        return;
    }

    printf("\n--- 当前成员列表 ---\n");

    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, name FROM members ORDER BY id;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("❌ 查询成员失败: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* name = (const char*)sqlite3_column_text(stmt, 1);
        printf("  [%d] %s\n", id, name ? name : "(无名)");
        count++;
    }

    if (count == 0) {
        printf("  （暂无成员）\n");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

// 显示所有账户（供编辑/删除前参考）
static void list_all_accounts(sqlite3* db) {
    printf("\n--- 当前账户列表 ---\n");
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, name, balance FROM accounts ORDER BY id;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("❌ 查询账户失败\n");
        return;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* name = (const char*)sqlite3_column_text(stmt, 1);
        double balance = sqlite3_column_double(stmt, 2);
        printf("  [%d] %s (余额: %.2f)\n", id, name, balance);
        count++;
    }
    sqlite3_finalize(stmt);

    if (count == 0) {
        printf("  （暂无账户）\n");
    }
}

// === 辅助：检查成员是否被记录引用 ===
static int is_member_referenced(sqlite3* db, int member_id) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT 1 FROM records WHERE member_id = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 1;
    sqlite3_bind_int(stmt, 1, member_id);
    int used = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return used;
}

// === 辅助：检查账户是否被引用或有余额 ≠ 0 ===
static int is_account_referenced_or_nonzero(sqlite3* db, int account_id) {
    sqlite3_stmt* stmt;
    // 检查是否有记录引用 或 余额非零
    const char* sql = 
        "SELECT 1 FROM records WHERE account_id = ? "
        "UNION "
        "SELECT 1 FROM accounts WHERE id = ? AND balance != 0 "
        "LIMIT 1;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 1;
    sqlite3_bind_int(stmt, 1, account_id);
    sqlite3_bind_int(stmt, 2, account_id);
    int used = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return used;
}

// === 辅助：检查分类是否被记录引用 ===
static int is_category_referenced(sqlite3* db, int category_id) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT 1 FROM records WHERE category_id = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 1;
    sqlite3_bind_int(stmt, 1, category_id);
    int used = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return used;
}

// === 成员管理 ===
void add_member(void) {
    // ✅ 新增：先显示现有成员列表
    list_members();

    char name[50];
    printf("\n输入新成员姓名: ");  // 注意加了 \n 让提示更清晰
    if (fgets(name, sizeof(name), stdin) == NULL) return;
    name[strcspn(name, "\n")] = 0;
    if (strlen(name) == 0) { 
        printf("❌ 姓名不能为空。\n"); 
        return; 
    }

    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) return;
    
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO members (name) VALUES (?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            printf("✅ 成员 \"%s\" 添加成功！\n", name);
        } else {
            printf("❌ 添加失败: %s\n", sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

void edit_member(void) {
    list_members();
    int id;
    printf("输入要编辑的成员 ID（0 取消）: ");
    if (scanf("%d", &id) != 1) {
        // 输入非数字，清空整行
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
        printf("❌ 请输入有效数字。\n");
        return;
    }

    if (id == 0) {
        return; // 取消
    }

    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) return;

    // 检查是否存在
    sqlite3_stmt* check;
    if (sqlite3_prepare_v2(db, "SELECT name FROM members WHERE id = ?;", -1, &check, NULL) != SQLITE_OK) {
        sqlite3_close(db); return;
    }
    sqlite3_bind_int(check, 1, id);
    if (sqlite3_step(check) != SQLITE_ROW) {
        printf("❌ 成员 ID %d 不存在。\n", id);
        sqlite3_finalize(check);
        sqlite3_close(db);
        return;
    }
    const char* old_name = (const char*)sqlite3_column_text(check, 0);
    sqlite3_finalize(check);

    char new_name[50];
    printf("新姓名 [%s]: ", old_name);
    if (fgets(new_name, sizeof(new_name), stdin) == NULL) { sqlite3_close(db); return; }
    new_name[strcspn(new_name, "\n")] = 0;

    if (strlen(new_name) == 0) {
        printf("❌ 姓名不能为空。\n");
        sqlite3_close(db);
        return;
    }

    sqlite3_stmt* stmt;
    const char* sql = "UPDATE members SET name = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, new_name, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, id);
        if (sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db) > 0) {
            printf("✅ 成员更新成功！\n");
        } else {
            printf("❌ 更新失败。\n");
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

void delete_member(void) {
    list_members();
    int id;
    printf("输入要删除的成员 ID（0 取消）: ");
    if (scanf("%d", &id) != 1) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
        printf("❌ 请输入有效数字。\n");
        return;
    }

    if (id == 0) {
        return;
    }

    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) return;

    if (is_member_referenced(db, id)) {
        printf("❌ 无法删除：该成员已被财务记录引用。\n");
        sqlite3_close(db);
        return;
    }

    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM members WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db) > 0) {
            printf("✅ 成员删除成功！\n");
        } else {
            printf("❌ 删除失败或成员不存在。\n");
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

void manage_members(void) {
    int choice;
    while (1) {
        clear_screen();
        printf("=== 成员管理 ===\n");
        printf("1. 添加成员\n");
        printf("2. 编辑成员\n");
        printf("3. 删除成员\n");
        printf("0. 返回\n");
        printf("请选择: ");
        if (scanf("%d", &choice) != 1) { while(getchar()!='\n'); continue; }
        getchar();

        switch (choice) {
            case 1: add_member(); break;
            case 2: edit_member(); break;
            case 3: delete_member(); break;
            case 0: return;
            default: printf("无效选项。\n");
        }
        press_any_key_to_continue();
    }
}

// === 账户管理（类似，略作简化）===
void add_account(void) {
    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库\n");
        return;
    }

    // 使用你已有的 list_all_accounts
    list_all_accounts(db);

    char name[50];
    printf("\n输入新账户名称: ");
    if (fgets(name, sizeof(name), stdin) == NULL) {
        sqlite3_close(db);
        return;
    }
    name[strcspn(name, "\n")] = 0;
    if (strlen(name) == 0) {
        printf("❌ 账户名称不能为空。\n");
        sqlite3_close(db);
        return;
    }

    double balance = 0.0;
    char buf[30];
    printf("输入初始余额（默认 0，可为负数）: ");
    if (fgets(buf, sizeof(buf), stdin) != NULL) {
        buf[strcspn(buf, "\n")] = 0;
        if (strlen(buf) > 0) {
            balance = atof(buf);
        }
    }

    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO accounts (name, balance) VALUES (?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 2, balance);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            printf("✅ 账户 \"%s\" 添加成功！初始余额: %.2f\n", name, balance);
        } else {
            printf("❌ 添加失败: %s\n", sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

void edit_account(void) {
    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库\n");
        return;
    }

    list_all_accounts(db);

    int id;
    printf("\n输入要编辑的账户 ID（0 取消）: ");
    if (scanf("%d", &id) != 1) {
        int c; while ((c = getchar()) != '\n' && c != EOF);
        printf("❌ 请输入有效数字。\n");
        sqlite3_close(db);
        return;
    }
    if (id == 0) {
        sqlite3_close(db);
        return;
    }
    int c;
    while ((c = getchar()) != '\n' && c != EOF);

    // 检查是否存在
    sqlite3_stmt* check;
    const char* check_sql = "SELECT name, balance FROM accounts WHERE id = ?";
    if (sqlite3_prepare_v2(db, check_sql, -1, &check, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return;
    }
    sqlite3_bind_int(check, 1, id);
    if (sqlite3_step(check) != SQLITE_ROW) {
        printf("❌ 账户 ID %d 不存在。\n", id);
        sqlite3_finalize(check);
        sqlite3_close(db);
        return;
    }
    const char* old_name = (const char*)sqlite3_column_text(check, 0);
    double old_balance = sqlite3_column_double(check, 1);
    sqlite3_finalize(check);

    char new_name[50];
    printf("新名称 [%s]: ", old_name);
    if (fgets(new_name, sizeof(new_name), stdin) == NULL) {
        sqlite3_close(db);
        return;
    }
    new_name[strcspn(new_name, "\n")] = 0;
    if (strlen(new_name) == 0) {
        strcpy(new_name, old_name);
    }

    char buf[30];
    printf("新余额 [%.2f]: ", old_balance);
    double new_balance = old_balance;
    if (fgets(buf, sizeof(buf), stdin) != NULL) {
        buf[strcspn(buf, "\n")] = 0;
        if (strlen(buf) > 0) {
            new_balance = atof(buf);
        }
    }

    // 检查重名（排除自己）
    sqlite3_stmt* unique;
    const char* unique_sql = "SELECT COUNT(*) FROM accounts WHERE name = ? AND id != ?";
    if (sqlite3_prepare_v2(db, unique_sql, -1, &unique, NULL) == SQLITE_OK) {
        sqlite3_bind_text(unique, 1, new_name, -1, SQLITE_STATIC);
        sqlite3_bind_int(unique, 2, id);
        if (sqlite3_step(unique) == SQLITE_ROW) {
            if (sqlite3_column_int(unique, 0) > 0) {
                printf("❌ 账户名称 \"%s\" 已存在。\n", new_name);
                sqlite3_finalize(unique);
                sqlite3_close(db);
                return;
            }
        }
        sqlite3_finalize(unique);
    }

    // 更新
    sqlite3_stmt* upd;
    const char* update_sql = "UPDATE accounts SET name = ?, balance = ? WHERE id = ?";
    if (sqlite3_prepare_v2(db, update_sql, -1, &upd, NULL) == SQLITE_OK) {
        sqlite3_bind_text(upd, 1, new_name, -1, SQLITE_STATIC);
        sqlite3_bind_double(upd, 2, new_balance);
        sqlite3_bind_int(upd, 3, id);

        if (sqlite3_step(upd) == SQLITE_DONE) {
            printf("✅ 账户更新成功！\n");
        } else {
            printf("❌ 更新失败: %s\n", sqlite3_errmsg(db));
        }
        sqlite3_finalize(upd);
    }
    sqlite3_close(db);
}

void delete_account(void) {
    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库\n");
        return;
    }

    list_all_accounts(db);

    int id;
    printf("\n输入要删除的账户 ID（0 取消）: ");
    if (scanf("%d", &id) != 1) {
        int c; while ((c = getchar()) != '\n' && c != EOF);
        printf("❌ 请输入有效数字。\n");
        sqlite3_close(db);
        return;
    }
    if (id == 0) {
        sqlite3_close(db);
        return;
    }
    int c;
    while ((c = getchar()) != '\n' && c != EOF);

    // 使用你已有的严格检查函数
    if (is_account_referenced_or_nonzero(db, id)) {
        printf("❌ 无法删除：该账户已被使用或余额非零。\n");
        sqlite3_close(db);
        return;
    }

    // 获取名称用于确认
    sqlite3_stmt* name_stmt;
    const char* name_sql = "SELECT name FROM accounts WHERE id = ?";
    if (sqlite3_prepare_v2(db, name_sql, -1, &name_stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return;
    }
    sqlite3_bind_int(name_stmt, 1, id);
    if (sqlite3_step(name_stmt) != SQLITE_ROW) {
        printf("❌ 账户不存在。\n");
        sqlite3_finalize(name_stmt);
        sqlite3_close(db);
        return;
    }
    const char* name = (const char*)sqlite3_column_text(name_stmt, 0);
    sqlite3_finalize(name_stmt);

    printf("⚠️  确认删除账户 [%d] \"%s\"?(y/N): ", id, name);
    char confirm[10];
    if (fgets(confirm, sizeof(confirm), stdin) == NULL) {
        sqlite3_close(db);
        return;
    }

    if (confirm[0] != 'y' && confirm[0] != 'Y') {
        printf("取消删除。\n");
        sqlite3_close(db);
        return;
    }

    sqlite3_stmt* del;
    const char* del_sql = "DELETE FROM accounts WHERE id = ?";
    if (sqlite3_prepare_v2(db, del_sql, -1, &del, NULL) == SQLITE_OK) {
        sqlite3_bind_int(del, 1, id);
        if (sqlite3_step(del) == SQLITE_DONE) {
            printf("✅ 账户删除成功！\n");
        } else {
            printf("❌ 删除失败: %s\n", sqlite3_errmsg(db));
        }
        sqlite3_finalize(del);
    }
    sqlite3_close(db);
}

void manage_accounts(void) {
    int choice;
    while (1) {
        clear_screen();
        printf("=== 账户管理 ===\n");
        printf("1. 添加账户\n");
        printf("2. 编辑账户\n");
        printf("3. 删除账户\n");
        printf("0. 返回上一级\n");
        printf("----------------\n");
        printf("请选择操作: ");

        if (scanf("%d", &choice) != 1) {
            // 清空无效输入
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            continue;
        }
        // 清除 scanf 后残留的换行符
        int c;
        while ((c = getchar()) != '\n' && c != EOF);

        switch (choice) {
            case 1:
                add_account();
                break;
            case 2:
                edit_account();
                break;
            case 3:
                delete_account();
                break;
            case 0:
                return; // 退出菜单，返回上级
            default:
                printf("❌ 无效选项，请重新选择。\n");
        }

        press_any_key_to_continue();
    }
}

// === 分类管理（较复杂，支持父子）===
void add_category(void) {
    
    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库\n");
        return;
    }

    list_all_categories(db);//添加分类时先实现已有的分类

    char type_str[20] = {0};

    while (1) {
        printf("请选择类型:\n");
        printf("1. 收入\n");
        printf("2. 支出\n");
        printf("请输入选项 (1/2) 输入0取消: ");

        char input[10];
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("❌ 输入错误，请重试。\n");
            continue;
        }

        // 去除换行符
        input[strcspn(input, "\n")] = 0;

        if (strcmp(input, "0") == 0) {
            printf("⚠️ 操作已取消。\n");
            return; // 或根据上下文使用 goto / 设置标志后 break
        } else if (strcmp(input, "1") == 0) {
            strcpy(type_str, "income");
            break;
        } else if (strcmp(input, "2") == 0) {
            strcpy(type_str, "expense");
            break;
        } else {
            printf("❌ 无效选项，请输入 0、1 或 2。\n");
        }
    }

    char name[50];
    printf("分类名称: ");
    if (fgets(name, sizeof(name), stdin) == NULL) return;
    name[strcspn(name, "\n")] = 0;
    if (strlen(name) == 0) { printf("❌ 名称不能为空。\n"); return; }

    printf("是否为子分类？(y/n): ");
    char yn[10];
    if (fgets(yn, sizeof(yn), stdin) == NULL) return;
    yn[strcspn(yn, "\n")] = 0;

    int parent_id = 0;
    if (yn[0] == 'y' || yn[0] == 'Y') {
        // 显示一级分类供选择
        sqlite3_stmt* p_stmt;
        const char* p_sql = "SELECT id, name FROM categories WHERE type = ? AND parent_id IS NULL;";
        if (sqlite3_prepare_v2(db, p_sql, -1, &p_stmt, NULL) != SQLITE_OK) {
            sqlite3_close(db); return;
        }
        sqlite3_bind_text(p_stmt, 1, type_str, -1, SQLITE_STATIC);
        printf("【父分类列表】\n");
        int count = 0;
        int ids[100];
        while (sqlite3_step(p_stmt) == SQLITE_ROW) {
            ids[count] = sqlite3_column_int(p_stmt, 0);
            printf("%d. %s\n", ++count, sqlite3_column_text(p_stmt, 1));
        }
        sqlite3_finalize(p_stmt);

        if (count == 0) {
            printf("❌ 无可用父分类，请先添加一级分类。\n");
            sqlite3_close(db);
            return;
        }

        printf("选择父分类编号 (1-%d): ", count);
        int choice;
        if (scanf("%d", &choice) != 1 || choice < 1 || choice > count) {
            while(getchar()!='\n');
            sqlite3_close(db);
            return;
        }
        getchar();
        parent_id = ids[choice - 1];
    }

    sqlite3_stmt* stmt;
    const char* sql = parent_id ? 
        "INSERT INTO categories (name, parent_id, type) VALUES (?, ?, ?);" :
        "INSERT INTO categories (name, type) VALUES (?, ?);";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        if (parent_id) {
            sqlite3_bind_int(stmt, 2, parent_id);
            sqlite3_bind_text(stmt, 3, type_str, -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_text(stmt, 2, type_str, -1, SQLITE_STATIC);
        }
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            printf("✅ 分类 \"%s\" 添加成功！\n", name);
        } else {
            printf("❌ 添加失败: %s\n", sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

//编辑分类
void edit_category(void) {
    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库\n");
        return;
    }

    list_all_categories(db);

    printf("\n请输入要编辑的分类 ID: ");
    int id;
    if (scanf("%d", &id) != 1 || id <= 0) {
        printf("❌ 无效 ID\n");
        sqlite3_close(db);
        return;
    }
    getchar(); // 清除换行

    // 检查分类是否存在
    sqlite3_stmt* check;
    const char* check_sql = "SELECT name, parent_id FROM categories WHERE id = ?";
    if (sqlite3_prepare_v2(db, check_sql, -1, &check, NULL) != SQLITE_OK) {
        printf("❌ 查询分类失败\n");
        sqlite3_close(db);
        return;
    }
    sqlite3_bind_int(check, 1, id);
    if (sqlite3_step(check) != SQLITE_ROW) {
        printf("❌ 分类不存在\n");
        sqlite3_finalize(check);
        sqlite3_close(db);
        return;
    }

    const char* old_name = (const char*)sqlite3_column_text(check, 0);
    int parent_id = sqlite3_column_int(check, 1);
    sqlite3_finalize(check);

    printf("当前名称: %s\n", old_name);
    printf("请输入新名称（直接回车保持不变）: ");
    char new_name[50];
    if (fgets(new_name, sizeof(new_name), stdin) == NULL) {
        sqlite3_close(db);
        return;
    }
    new_name[strcspn(new_name, "\n")] = 0;

    if (strlen(new_name) == 0) {
        strcpy(new_name, old_name); // 保持原名
    }

    if (strlen(new_name) > 30) {
        printf("❌ 名称过长（最多30字符）\n");
        sqlite3_close(db);
        return;
    }

    // 检查重名（同级内唯一）
    sqlite3_stmt* unique_check;
    const char* unique_sql = 
        "SELECT COUNT(*) FROM categories WHERE name = ? AND parent_id = ? AND id != ?";
    if (sqlite3_prepare_v2(db, unique_sql, -1, &unique_check, NULL) != SQLITE_OK) {
        printf("❌ 检查重名失败\n");
        sqlite3_close(db);
        return;
    }
    sqlite3_bind_text(unique_check, 1, new_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(unique_check, 2, parent_id);
    sqlite3_bind_int(unique_check, 3, id);

    if (sqlite3_step(unique_check) == SQLITE_ROW) {
        if (sqlite3_column_int(unique_check, 0) > 0) {
            printf("❌ 同级分类中已存在同名项\n");
            sqlite3_finalize(unique_check);
            sqlite3_close(db);
            return;
        }
    }
    sqlite3_finalize(unique_check);

    // 执行更新
    sqlite3_stmt* upd;
    const char* update_sql = "UPDATE categories SET name = ? WHERE id = ?";
    if (sqlite3_prepare_v2(db, update_sql, -1, &upd, NULL) != SQLITE_OK) {
        printf("❌ 准备更新失败\n");
        sqlite3_close(db);
        return;
    }
    sqlite3_bind_text(upd, 1, new_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(upd, 2, id);

    if (sqlite3_step(upd) == SQLITE_DONE) {
        printf("✅ 分类修改成功！\n");
    } else {
        printf("❌ 修改失败: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(upd);
    sqlite3_close(db);
}

//删除分类
void delete_category(void) {
    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库\n");
        return;
    }

    list_all_categories(db);

    printf("\n请输入要删除的分类 ID: ");
    int id;
    if (scanf("%d", &id) != 1 || id <= 0) {
        printf("❌ 无效 ID\n");
        sqlite3_close(db);
        return;
    }
    getchar();

    // 检查是否存在
    sqlite3_stmt* check;
    const char* check_sql = "SELECT name, parent_id FROM categories WHERE id = ?";
    if (sqlite3_prepare_v2(db, check_sql, -1, &check, NULL) != SQLITE_OK) {
        printf("❌ 查询失败\n");
        sqlite3_close(db);
        return;
    }
    sqlite3_bind_int(check, 1, id);
    if (sqlite3_step(check) != SQLITE_ROW) {
        printf("❌ 分类不存在\n");
        sqlite3_finalize(check);
        sqlite3_close(db);
        return;
    }

    const char* name = (const char*)sqlite3_column_text(check, 0);
    int parent_id = sqlite3_column_int(check, 1);
    int is_top_level = (parent_id == 0 || parent_id == -1 || sqlite3_column_type(check, 1) == SQLITE_NULL);
    sqlite3_finalize(check);

    // 检查是否被财务记录引用
    sqlite3_stmt* ref_check;
    const char* ref_sql = "SELECT COUNT(*) FROM records WHERE category_id = ?";
    if (sqlite3_prepare_v2(db, ref_sql, -1, &ref_check, NULL) != SQLITE_OK) {
        printf("❌ 检查引用失败\n");
        sqlite3_close(db);
        return;
    }
    sqlite3_bind_int(ref_check, 1, id);
    int has_ref = 0;
    if (sqlite3_step(ref_check) == SQLITE_ROW) {
        has_ref = sqlite3_column_int(ref_check, 0) > 0;
    }
    sqlite3_finalize(ref_check);

    if (has_ref) {
        printf("❌ 无法删除：该分类已被财务记录使用！\n");
        sqlite3_close(db);
        return;
    }

    // 如果是一级分类，检查是否有子分类
    if (is_top_level) {
        sqlite3_stmt* child_check;
        const char* child_sql = "SELECT COUNT(*) FROM categories WHERE parent_id = ?";
        if (sqlite3_prepare_v2(db, child_sql, -1, &child_check, NULL) == SQLITE_OK) {
            sqlite3_bind_int(child_check, 1, id);
            if (sqlite3_step(child_check) == SQLITE_ROW) {
                if (sqlite3_column_int(child_check, 0) > 0) {
                    printf("❌ 无法删除：该一级分类下还有子分类！请先删除子分类。\n");
                    sqlite3_finalize(child_check);
                    sqlite3_close(db);
                    return;
                }
            }
            sqlite3_finalize(child_check);
        }
    }

    printf("确认删除分类 [%d] \"%s\"？(y/N): ", id, name);
    char confirm[10];
    if (fgets(confirm, sizeof(confirm), stdin) == NULL) {
        sqlite3_close(db);
        return;
    }

    if (confirm[0] != 'y' && confirm[0] != 'Y') {
        printf("取消删除。\n");
        sqlite3_close(db);
        return;
    }

    // 执行删除
    sqlite3_stmt* del;
    const char* del_sql = "DELETE FROM categories WHERE id = ?";
    if (sqlite3_prepare_v2(db, del_sql, -1, &del, NULL) != SQLITE_OK) {
        printf("❌ 删除失败\n");
        sqlite3_close(db);
        return;
    }
    sqlite3_bind_int(del, 1, id);

    if (sqlite3_step(del) == SQLITE_DONE) {
        printf("✅ 分类删除成功！\n");
    } else {
        printf("❌ 删除失败: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(del);
    sqlite3_close(db);
}

void manage_categories(void) {
    int choice;
    while (1) {
        clear_screen();
        printf("=== 分类管理 ===\n");
        printf("1. 添加分类\n");
        printf("2. 编辑分类\n");      // ← 新增
        printf("3. 删除分类\n");      // ← 新增
        printf("0. 返回\n");
        printf("请选择: ");
        if (scanf("%d", &choice) != 1) {
            int c; while ((c = getchar()) != '\n' && c != EOF);
            choice = -1;
        } else {
            getchar();
        }

        switch (choice) {
            case 1: add_category(); break;
            case 2: edit_category(); break;   // ← 调用
            case 3: delete_category(); break; // ← 调用
            case 0: return;
            default: printf("无效选项。\n");
        }
        press_any_key_to_continue();
    }
}

void change_password(void) {
    sqlite3* db;
    if (sqlite3_open("finance.db", &db) != SQLITE_OK) {
        printf("❌ 无法打开数据库。\n");
        return;
    }

    // === 验证旧密码 ===
    int verified = 0;
    for (int attempts = 3; attempts > 0 && !verified; attempts--) {
        char* old_pwd = getpass("请输入旧密码: ");
        if (!old_pwd || strlen(old_pwd) == 0) {
            printf("❌ 密码不能为空。\n");
            continue;
        }
        verified = authenticate_user(db, old_pwd); // ← 直接复用你的验证函数！
        if (!verified) {
            printf("❌ 旧密码错误！剩余 %d 次机会。\n", attempts - 1);
        }
    }

    if (!verified) {
        sqlite3_close(db);
        printf("❌ 验证失败，退出密码修改。\n");
        return;
    }

    // === 输入新密码 ===
    char* new_pwd1 = getpass("请输入新密码（至少6位）: ");
    if (!new_pwd1 || strlen(new_pwd1) < 6) {
        sqlite3_close(db);
        printf("❌ 新密码至少需要6位。\n");
        return;
    }

    char* new_pwd2 = getpass("请再次输入新密码: ");
    if (!new_pwd2 || strcmp(new_pwd1, new_pwd2) != 0) {
        sqlite3_close(db);
        printf("❌ 两次输入的新密码不一致！\n");
        return;
    }

    // === 生成新盐和新哈希 ===
    char new_salt[17];
    generate_salt(new_salt);  // ← 你的函数

    char new_hash[65];
    hash_password(new_pwd1, new_salt, new_hash); // ← 你的函数

    // === 更新数据库 ===
    sqlite3_stmt* stmt;
    const char* sql = "UPDATE admin SET password_hash = ?, salt = ? WHERE id = 1;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        printf("❌ 准备更新语句失败。\n");
        return;
    }

    sqlite3_bind_text(stmt, 1, new_hash, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, new_salt, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_DONE) {
        printf("✅ 密码修改成功！下次登录请使用新密码。\n");
    } else {
        printf("❌ 更新密码失败: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

}

// === 主设置菜单 ===
void show_settings_menu(void) {
    int choice;
    while (1) {
        clear_screen();
        printf("=== 系统设置 ===\n");
        printf("1. 成员管理\n");
        printf("2. 账户管理\n");
        printf("3. 分类管理\n");
        printf("4. 修改密码\n");
        printf("0. 返回主菜单\n");
        printf("请选择: ");
        if (scanf("%d", &choice) != 1) { while(getchar()!='\n'); continue; }
        getchar();

        switch (choice) {
            case 1: manage_members(); break;
            case 2: manage_accounts(); break;
            case 3: manage_categories(); break;
            case 4: change_password(); break;
            case 0: return;
            default: printf("无效选项。\n");
        }
        if (choice != 0) press_any_key_to_continue();
    }
}