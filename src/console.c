#include <stdbool.h>
#include <stdio.h>

#include "keyboard.h"
#include "text.h"

void blank_screen(void); // FIXME: h file

void start_console(void) {
    init_text();
    blank_screen();

    configure_keyboard();

    printf("> ");
    fflush(stdout);
}

const bool do_print = false;

void update_console(void) {
    static bool shift_down = false;
    static bool control_down = false;

    const KeyEvent *event;
    while ((event = get_keyboard_event())) {
        char c = event->value;
        if (control_down) {
            c = get_control_key(c);
        } else if (shift_down) {
            c = get_shifted_key(c);
        }

        if (event->type == KEY_HELD) {
            if (event->class == ASCII_KEY) {
                if (c == '\b') {
                    putchar('\b');
                    putchar(' ');
                }
                putchar(c);
                fflush(stdout);
            }
            continue;
        }

        if (event->class == ASCII_KEY) {
            if (do_print)
                printf("Key '%c'", c);
            else if (event->type == KEY_DOWN) {
                if (c == '\b') {
                    putchar('\b');
                    putchar(' ');
                }
                putchar(c);
                fflush(stdout);
            }
        } else if (event->class == CONTROL_KEY) {
            if (do_print)
                printf("Control");
            control_down = event->type != KEY_UP;
        } else if (event->class == ALT_KEY) {
            if (do_print)
                printf("Alt");
        } else if (event->class == ESCAPE_KEY) {
            if (do_print)
                printf("Escape");
            else if (event->type == KEY_DOWN)
                putchar('\033');
        } else if (event->class == LSHIFT_KEY || event->class == RSHIFT_KEY) {
            if (do_print)
                printf("Shift");
            shift_down = event->type != KEY_UP;
        } else if (event->class == LEFT_ARROW_KEY) {
            if (do_print)
                printf("Left");
        } else if (event->class == RIGHT_ARROW_KEY) {
            if (do_print)
                printf("Right");
        } else if (event->class == UP_ARROW_KEY) {
            if (do_print)
                printf("Up");
        } else if (event->class == DOWN_ARROW_KEY) {
            if (do_print)
                printf("Down");
        } else {
            if (do_print)
                printf("Data: 0x%02x", event->class);
        }

        if (do_print)
            puts(event->type == KEY_UP ? " up" : " down");
    }
}
