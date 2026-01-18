// main.c
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "auth.h"
#include "utils.h"
#include "finance.h"
#include "settings.h"

int main() {

//确定UTF-8编码环境
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setlocale(LC_ALL, ".UTF8");
#endif

//开发模式
#ifndef DEBUG_MODE
    if (!login_at_startup()) return 1; // 内部已处理初始化
#endif

init_finance_database();// 首次运行时初始化数据库（包括成员、账户、分类等）

    int choice;
    do {
        clear_screen();//清屏
        printf("\n=== 家庭财务管理系统 ===\n");
        printf("1.  添加记录\n"); 
        printf("2.  修改记录\n");
        printf("3.  删除记录\n");
        printf("4.  查看所有记录\n");
        printf("5.  导出所有记录\n");
        printf("6.  按日期查询\n");
        printf("7.  按分类查询\n");
        printf("8.  月度统计\n");
        printf("9.  年度统计\n");
        printf("10. 分类统计\n");
        printf("11. 系统设置\n"); 
        printf("0.  退出\n");
        printf("请选择: ");

        if (scanf("%d", &choice) != 1) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            choice = -1; // 无效输入
        } else {
            getchar(); // 清除换行（scanf %d 不吃 \n）
        }

        switch (choice) {
            case 1: add_record(); press_any_key_to_continue(); break;
            case 2: edit_record(); press_any_key_to_continue(); break; 
            case 3: delete_record(); press_any_key_to_continue(); break; 
            case 4: list_records(); press_any_key_to_continue(); break;
            case 5: export_to_csv(); press_any_key_to_continue(); break;
            case 6: query_by_date(); press_any_key_to_continue(); break;
            case 7: query_by_category(); press_any_key_to_continue(); break;
            case 8: show_monthly_report(); press_any_key_to_continue(); break;
            case 9: show_yearly_report(); press_any_key_to_continue(); break;
            case 10: show_category_report(); press_any_key_to_continue(); break;
            case 11: show_settings_menu(); break;  // ← 新增：进入系统设置
            case 0: printf("再见！\n"); break;
            default: printf("无效选项！\n"); press_any_key_to_continue();
        }
    } while (choice != 0);

    return 0;
}