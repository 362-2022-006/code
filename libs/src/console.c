#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "fat.h"
#include "keyboard.h"
#include "text.h"

void *sbrk(int incr);
static bool _do_code(struct FATFile *file, int *status);

static bool hasFile = false;
static struct FATFile currentFile;
static uint8_t *sd_buffer;

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

    sd_buffer = malloc(512);

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

static char _get_non_whitespace(void) {
    char c;
    while ((c = __io_getchar()) == ' ')
        ;
    return c;
}

static char *_read_io_word(unsigned int max_len) {
    char *ptr = NULL;
    unsigned int ptr_len = 0;
    char buffer[22];
    u8 len = 0;

    char c = _get_non_whitespace();
    for (;;) {
        buffer[len++] = c;
        if (c == ' ' || c == '\n')
            break;
        if (len >= 22) {
            if (ptr)
                ptr = realloc(ptr, ptr_len + 22);
            else
                ptr = malloc(22);
            memcpy(ptr + ptr_len, buffer, 22);
            ptr_len += 22;
            len = 0;
        }
        if (ptr_len + len >= max_len) {
            break;
        }
        c = __io_getchar();
    }

    if (ptr)
        ptr = realloc(ptr, ptr_len + len + 1);
    else
        ptr = malloc(len + 1);
    memcpy(ptr + ptr_len, buffer, len);
    ptr[ptr_len + len] = '\0';

    return ptr;
}

static bool _update_current_file(void) {
    if (init_fat(sd_buffer))
        return true;
    if (!hasFile) {
        open_root(&currentFile);
        hasFile = true;
    }
    return false;
}

static bool _read_path(struct FATFile *file) {
    if (_update_current_file()) {
        return true;
    }

    char *path = _read_io_word(300);
    unsigned int startIndex = 0, index = 0;

    *file = currentFile;
    if (path[0] == '\n')
        return false;
    else if (path[0] == '/') {
        open_root(file);
        startIndex = 1;
        index = 1;
    }

    char c = path[index];
    while (c != ' ' && c != '\n') {
        while (path[index] != '/' && path[index] != ' ' && path[index] != '\n') {
            index++;
        }

        c = path[index];
        path[index] = '\0';
        if (open(path + startIndex, file, sd_buffer)) {
            free(path);
            return true;
        }

        startIndex = ++index;
    }

    free(path);
    return false;
}

static void _process_command(const char *command, bool no_more_input) {
    if (!strcmp(command, "ls")) {
        if (_update_current_file()) {
            puts("Could not initialize SD");
        } else {
            if (no_more_input)
                ls(&currentFile, sd_buffer);
            else {
                struct FATFile file;
                if (_read_path(&file)) {
                    puts("Invalid path");
                } else {
                    ls(&file, sd_buffer);
                }
            }
        }
    } else if (!strcmp(command, "run")) {
        if (_update_current_file()) {
            puts("Could not initialize SD");
        } else {
            struct FATFile file;
            if (_read_path(&file)) {
                puts("Could not open file");
            } else if (file.directory) {
                puts("Is a directory");
            } else {
                int length = get_file_next_sector(&file, sd_buffer);
                if (length < 12) {
                    puts("File length too short");
                } else {
                    // actually run the code
                    *(struct FATFile *)0x20000010 = file;
                    exit(0xf0f0f0f0);
                }
            }
        }
    } else if (!strcmp(command, "cd")) {
        if (_update_current_file()) {
            puts("Could not initialize SD");
        } else {
            struct FATFile file;
            if (_read_path(&file)) {
                puts("Could not open file");
            } else if (!file.directory) {
                puts("Not a directory");
            } else {
                currentFile = file;
            }
        }
    } else if (!strcmp(command, "eject")) {
        hasFile = false;
        close_fat();
    } else {
        printf("command not found: %s\n", command);
    }

    discard_input_line();
}

void update_console(void) {
    char command[15];
    u8 index = 0;

    while (index < 14) {
        command[index] = index ? __io_getchar() : _get_non_whitespace();
        if (command[index] == ' ' || command[index] == '\n')
            break;
        index++;
    }

    bool singleton = command[index] == '\n';
    command[index] = '\0';

    if (index >= 14) {
        printf("command not found: %s", command);

        char c;
        while ((c = __io_getchar()) != '\n')
            putchar(c);
        putchar('\n');
    } else {
        _process_command(command, singleton);
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

    if (init_fat(sd_buffer)) {
        puts("Could not initialize SD");
        return true;
    }

    reset_file(file);

    int length = get_file_next_sector(file, sd_buffer);
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
        // printf("Break is at %p\n", sbrk(0));
        // printf("Code wants to load at %p\n", code_load_position);
        return true;
    }
    if (_check_position(entry_point)) {
        puts("Invalid entry point");
        return true;
    }
    if (bss_length > 0x7000) {
        puts("BSS too long");
        return true;
    }

    // move remainder of first sector to the correct position
    memmove(code_load_position, sd_buffer + 12, length - 12);
    code_load_position += length - 12;

    // initialize .text, .rodata, .ARM.extab, .ARM, .preinit_array, .init_array, .fini_array,
    // .data
    do {
        length = get_file_next_sector(file, code_load_position);
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
