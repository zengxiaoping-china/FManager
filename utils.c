// utils.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
    #include <conio.h>
#else
    #include <termios.h>
    #include <unistd.h>
#endif

// 清屏函数
void clear_screen(void) {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

// 模拟 _getch（仅非 Windows）
#ifndef _WIN32
static int _getch(void) {
    struct termios oldt, newt;
    int ch;
    if (tcgetattr(STDIN_FILENO, &oldt) != 0) return EOF;
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);  // ← 注意：你原文是 “～”，应为 “～”
    if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) != 0) return EOF;
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}
#endif

// 按任意键继续
void press_any_key_to_continue(void) {
    printf("\n✅ 按任意键返回主菜单...");
#ifdef _WIN32
    _getch();
#else
    _getch(); // 使用上面 static 定义
#endif
    printf("\n");
}

// CSV 转义函数：处理逗号、双引号、换行
void csv_escape(const char* input, char* output, size_t out_size) {
    if (!input || input[0] == '\0') {
        output[0] = '\0';
        return;
    }

    size_t len = strlen(input);
    size_t j = 0;
    int needs_quotes = 0;

    for (size_t i = 0; i < len && j + 2 < out_size - 1; i++) {
        if (input[i] == '"' || input[i] == ',' || input[i] == '\n' || input[i] == '\r') {
            needs_quotes = 1;
            break;
        }
    }

    if (needs_quotes) {
        output[j++] = '"';
        for (size_t i = 0; i < len && j < out_size - 2; i++) {
            if (input[i] == '"') {
                if (j + 2 >= out_size) break;
                output[j++] = '"';
                output[j++] = '"';
            } else {
                output[j++] = input[i];
            }
        }
        if (j < out_size - 1) output[j++] = '"';
    } else {
        strncpy(output, input, out_size - 1);
        output[out_size - 1] = '\0';
    }
}

//任意步骤取消函数
bool input_with_cancel(
    const char* prompt,
    const char* cancel_value,
    char* out_buffer,
    size_t buffer_size
) {
    if (!prompt || !cancel_value || !out_buffer || buffer_size == 0) {
        return false;
    }

    printf("%s (输入 \"%s\" 取消): ", prompt, cancel_value);

    if (fgets(out_buffer, (int)buffer_size, stdin) == NULL) {
        // 读取失败（如 EOF）
        printf("\n❌ 输入中断。\n");
        return false;
    }

    // 移除换行符
    out_buffer[strcspn(out_buffer, "\n")] = '\0';

    // 检查是否为取消指令（精确匹配）
    if (strcmp(out_buffer, cancel_value) == 0) {
        printf("⚠️ 操作已取消。\n");
        return false;
    }

    // 可选：禁止空输入（根据需求决定是否保留）
    if (strlen(out_buffer) == 0) {
        printf("❌ 输入不能为空。\n");
        return false;
    }

    return true; // 成功
}