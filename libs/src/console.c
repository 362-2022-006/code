#include <stdbool.h>
#include <stdio.h>

#include "console.h"
#include "keyboard.h"
#include "text.h"

void start_console(bool prompt) {
    init_text(true);

    configure_keyboard();

    if (prompt) {
        if (get_current_column())
            puts("");

        int status = *(int *)0x20000004;
        if (status) {
            printf("Previous process returned %d\n", status);
        }
        *(int *)0x20000004 = 0;

        print_console_prompt();
    }
}

void update_console(void) {
    char key;
    while ((key = __io_getchar()) != '\n') {
        if (key == '\b') {
            putchar('\b');
            putchar(' ');
        }
        putchar(key);
        fflush(stdout);
    }
    print_console_prompt();
}

void print_console_prompt(void) {
    if (get_current_column())
        puts("");
    printf("> ");
    fflush(stdout);
}
