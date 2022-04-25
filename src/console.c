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
        printf("> ");
        fflush(stdout);
    }
}

void soft_reset(void);

void update_console(void) {
    char key;
    // while ((key = get_keyboard_character())) {
    while ((key = getchar()) != '\n') {
        if (key == '\003') {
            soft_reset();
        }
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
