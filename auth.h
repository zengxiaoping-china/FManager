// auth.h
#ifndef AUTH_H
#define AUTH_H

#include "sqlite3.h"

// 跨平台 getpass 声明
#ifdef _WIN32
char* getpass(const char* prompt);  // ← 声明 Windows 自实现版本
#else
#include <unistd.h>  // Linux/macOS 使用系统 getpass
#endif

void init_auth_database(sqlite3* db);
int login_at_startup(void);
void generate_salt(char salt[17]);
void hash_password(const char* password, const char* salt, char hash_hex[65]);
int authenticate_user(sqlite3* db, const char* input_password);

#endif