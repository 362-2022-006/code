#include <stdbool.h>
#include <stdio.h>

#include "keyboard.h"
#include "text.h"

void start_console(bool prompt) {
    init_text(true);

    configure_keyboard();

    if (prompt) {
        if (get_current_column())
            puts("");

        // int status = *(int *)0x20000000;
        // if (status) {
        //     printf("Previous process returned %d\n", status);
        // }

        printf("> ");
        fflush(stdout);
    }
}

void soft_reset(void);

void update_console(void) {
    char key;
    // while ((key = get_keyboard_character())) {
    while ((key = __io_getchar()) != '\n') {
        if (key == '\b') {
            putchar('\b');
            putchar(' ');
        }
        putchar(key);
        fflush(stdout);
    }
    printf("\n> ");
    fflush(stdout);
}
