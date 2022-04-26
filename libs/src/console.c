#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "fat.h"
#include "keyboard.h"
#include "text.h"

void *sbrk(int incr);

static bool _do_code(struct FATFile *file, int *status);

void start_console(bool prompt) {
    int status = *(int *)0x20000004;

    init_text(true);
    configure_keyboard();

    if (status == 0xf0f0f0f0) {
        if (_do_code((struct FATFile *)0x20000010, &status)) {
            puts("Error encountered running file");
            status = 0;
        } else {
            exit(status);
        }
    }

    if (prompt) {
        if (get_current_column())
            puts("");

        if (status) {
            printf("Exit code %d\n", status);
        }
        *(int *)0x20000004 = 0;

        print_console_prompt();
    }
}

void update_console(void) {
    char command[15];
    u8 index = 0;

    while (index < 14) {
        command[index] = __io_getchar();
        if (command[index] == ' ' || command[index] == '\n')
            break;
        index++;
    }

    if (index >= 14) {
        while (__io_getchar() != '\n')
            ;
        puts("Invalid command");
    } else {
        command[index] = '\0';

        if (!strcmp(command, "ls")) {
            uint8_t *sd_buffer = malloc(512);

            struct FATParameters params;
            if (init_fat(&params, sd_buffer)) {
                puts("Could not initialize SD");
            } else {
                struct FATFile root;
                open_root(&params, &root);

                ls(&params, &root, sd_buffer);
            }

            free(sd_buffer);
        } else if (!strcmp(command, "run")) {
            char *filename = (char *)0x20000010;
            u8 index = 0;
            while (index < 250) {
                filename[index] = __io_getchar();
                if (filename[index] == '\n')
                    break;
                index++;
            }

            filename[index] = '\0';

            if (index >= 250) {
                while (__io_getchar() != '\n')
                    ;
                puts("Filename too long");
            } else {
                uint8_t *sd_buffer = malloc(512);

                struct FATParameters params;
                if (init_fat(&params, sd_buffer)) {
                    puts("Could not initialize SD");
                } else {
                    struct FATFile file;
                    open_root(&params, &file);

                    if (open(filename, &params, &file, sd_buffer)) {
                        printf("Could not open %s\n", filename);
                    } else if (file.directory) {
                        printf("%s is a directory\n", filename);
                    } else {
                        int length = get_file_next_sector(&params, &file, sd_buffer);
                        if (length < 12) {
                            puts("File length too short");
                        } else {
                            // actually run the code
                            *(struct FATFile *)0x20000010 = file;
                            exit(0xf0f0f0f0);
                        }
                    }
                }

                free(sd_buffer);
            }
        } else {
            puts("Invalid command");
        }
    }

    print_console_prompt();
}

void print_console_prompt(void) {
    fflush(stdout); // to make sure column is accurate
    if (get_current_column())
        puts("");
    printf("> ");
    fflush(stdout);
}

// ===== CODE LOADING STUFF ===== //

// static void _wait_for_key(void) {
//     // flush buffer
//     while (get_keyboard_event())
//         ;

//     puts("Waiting for key...");

//     for (;;) {
//         if (get_keyboard_event()) {
//             puts("Key pressed");
//             return;
//         }
//     }
// }

static bool _check_position(void *position) {
    if (position < sbrk(0)) {
        // if the position where the code wants to be loaded is before the break, there could be
        // important things in the way, do not continue to load the code and return error flag
        puts("Failed position check");
        return true;
    }
    if ((u32)position > 0x20007000) {
        puts("Position is above top of RAM"); // technically top of RAM - 2 KiB
        return true;
    }
    return false;
}

static bool _do_code(struct FATFile *file, int *status) {
    uint8_t *sd_buffer = (uint8_t *)0x20004000;

    if (_check_position((void *)0x20004000)) {
        puts("Not enough space to load code");
        return true;
    }

    struct FATParameters params;
    if (init_fat(&params, sd_buffer)) {
        puts("Could not initialize SD");
        return true;
    }

    reset_file(file);

    int length = get_file_next_sector(&params, file, sd_buffer);
    if (length < 12) {
        puts("File length too short");
        return true;
    }

    // retrieve metadata
    void *code_load_position = (void *)*(uint32_t *)sd_buffer;
    int (*entry_point)() = (int (*)())((uint32_t *)sd_buffer)[1];
    u32 bss_length = ((uint32_t *)sd_buffer)[2];

    if (_check_position(code_load_position)) {
        puts("Not enough space to load code");
        printf("Break is at %p\n", sbrk(0));
        printf("Code wants to load at %p\n", code_load_position);
        return true;
    }

    // move remainder of first sector to the correct position
    memmove(code_load_position, sd_buffer + 12, length - 12);
    code_load_position += length - 12;

    // initialize .text, .rodata, .ARM.extab, .ARM, .preinit_array, .init_array, .fini_array,
    // .data
    do {
        length = get_file_next_sector(&params, file, code_load_position);
        code_load_position += length;
    } while (length > 0);

    // initialize .bss directly after .data
    memset(code_load_position, 0, bss_length);

    // set code_loc to the first byte after .bss, where the heap starts
    code_load_position += bss_length;
    // set break (location where new memory on the heap will be allocated)
    sbrk(code_load_position - sbrk(0));

    // puts("Call");
    // _wait_for_key();

    set_screen_text_buffer(false);
    *status = entry_point();

    return false;
}
