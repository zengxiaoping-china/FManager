// auth.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "auth.h"
#include "sha256.h"
#include "sqlite3.h"
#define DATABASE_NAME "finance.db"

// 辅助：隐藏密码输入（Windows）
#ifdef _WIN32
#include <conio.h>
static char input_buf[128]; // 静态缓冲区（仅 Windows 使用）

char* getpass(const char* prompt) {
    printf("%s", prompt);
    int i = 0;
    char ch;
    while ((ch = getch()) != '\r' && ch != '\n' && i < 127) {
        if (ch == '\b' && i > 0) {
            printf("\b \b");
            i--;
        } else if (ch != '\b') {
            input_buf[i++] = ch;
            putchar('*');
        }
    }
    input_buf[i] = '\0';
    printf("\n");
    return input_buf;
}
#else
#include <termios.h>
// Linux/macOS 使用系统 getpass
#endif

// 生成随机盐（简单版）
void generate_salt(char salt[17]) {
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < 16; i++) {
        salt[i] = chars[rand() % (sizeof(chars) - 1)];
    }
    salt[16] = '\0';
}

// 哈希密码 = SHA256(密码 + 盐)
void hash_password(const char* password, const char* salt, char hash_hex[65]) {
    if (!password || !salt) {
        memset(hash_hex, 0, 65);
        return;
    }
    char combined[256];
    snprintf(combined, sizeof(combined), "%s%s", password, salt);

    uint8_t hash[32];
    sha256((uint8_t*)combined, strlen(combined), hash);

    for (int i = 0; i < 32; i++) {
        sprintf(hash_hex + (i * 2), "%02x", hash[i]);
    }
    hash_hex[64] = '\0';
}

// 初始化认证所需表（仅 admin）
void init_auth_database(sqlite3* db) {
    if (!db) return;

    const char* create_admin_sql = 
        "CREATE TABLE IF NOT EXISTS admin ("
        "  id INTEGER PRIMARY KEY CHECK (id = 1), "
        "  password_hash TEXT NOT NULL, "
        "  salt TEXT NOT NULL"
        ");";
    sqlite3_exec(db, create_admin_sql, NULL, NULL, NULL);
}

// 检查是否首次运行（admin 表为空）
static int is_first_run(sqlite3* db) {
    int count = 0;
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM admin;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 1; // 视为首次（保守）

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return (count == 0);
}

// 设置初始密码（安全版：参数化查询）
static void setup_initial_password(sqlite3* db) {
    printf("【首次运行】请设置管理员密码：\n");
    char* pwd = getpass("密码: ");
    if (!pwd || strlen(pwd) == 0) {
        printf("❌ 密码不能为空！\n");
        return;
    }

    char salt[17];
    generate_salt(salt);
    char hash_hex[65];
    hash_password(pwd, salt, hash_hex);

    const char* sql = "INSERT INTO admin (id, password_hash, salt) VALUES (1, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "❌ 准备插入语句失败\n");
        return;
    }
    sqlite3_bind_text(stmt, 1, hash_hex, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, salt, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        printf("✅ 管理员密码设置成功！\n");
    } else {
        fprintf(stderr, "❌ 插入密码失败: %s\n", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);

    // 安全清零（仅 Windows，因使用静态缓冲区）
#ifdef _WIN32
    memset(input_buf, 0, sizeof(input_buf));
#endif
}

// 验证密码（公开接口）
int authenticate_user(sqlite3* db, const char* input_pwd) {
    if (!db || !input_pwd) return 0;

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, "SELECT password_hash, salt FROM admin WHERE id = 1;", -1, &stmt, NULL);
    if (rc != SQLITE_OK || sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return 0;
    }
    const char* stored_hash = (const char*)sqlite3_column_text(stmt, 0);
    const char* salt = (const char*)sqlite3_column_text(stmt, 1);

    char input_hash[65];
    hash_password(input_pwd, salt, input_hash);

    int result = (stored_hash && strcmp(stored_hash, input_hash) == 0);
    sqlite3_finalize(stmt);

    // 安全清零（仅 Windows）
#ifdef _WIN32
    // 注意：这里不能直接 memset input_pwd，因为可能是外部传入的指针
    // 只有当 input_pwd 指向 input_buf 时才清零（但无法安全判断）
    // 所以通常不在这里清零外部密码，由调用方负责
#endif

    return result;
}

// 主入口：登录或初始化
int login_at_startup(void) {
    sqlite3* db;
    if (sqlite3_open(DATABASE_NAME, &db) != SQLITE_OK) {
        fprintf(stderr, "❌ 无法打开数据库: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    init_auth_database(db); // ✅ 正确调用（传入 db）

    if (is_first_run(db)) {
        setup_initial_password(db);
        sqlite3_close(db);
        return 1;
    } else {
        int attempts = 3;
        while (attempts-- > 0) {
            char* pwd = getpass("请输入管理员密码: ");
            if (authenticate_user(db, pwd)) {
                sqlite3_close(db);
                return 1;
            }
            printf("❌ 密码错误！剩余 %d 次机会。\n", attempts);
        }
        sqlite3_close(db);
        return 0;
    }
}