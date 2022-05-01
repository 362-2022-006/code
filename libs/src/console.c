#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "console.h"
#include "fat.h"
#include "keyboard.h"
#include "text.h"

void *sbrk(int incr);
static bool _do_code(struct FATFile *file, int *status);

static bool *hasFile = (bool *)0x2000002f;
static struct FATFile *currentFile = (struct FATFile *)0x20000030;
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

static int _backslash_escape(char *c) {
    switch (*c) {
    case 'a':
        c[0] = '\a';
        break;
    case 'b':
        c[0] = '\b';
        break;
    case 't':
        c[0] = '\t';
        break;
    case 'n':
        c[0] = '\n';
        break;
    case 'v':
        c[0] = '\v';
        break;
    case 'f':
        c[0] = '\f';
        break;
    case 'r':
        c[0] = '\r';
        break;
    case '"':
    case '\n':
    case '\\':
    case ' ':
        break;
    case 'h': {
        char n = 0;
        for (int i = 1; i <= 2; i++) {
            if ('0' <= c[i] && c[i] <= '9') {
                n *= 16;
                n += c[i] - '0';
            } else if ('a' <= c[i] && c[i] <= 'f') {
                n *= 16;
                n += c[i] - 'a' + 10;
            } else if ('A' <= c[i] && c[i] <= 'F') {
                n *= 16;
                n += c[i] - 'A' + 10;
            } else {
                if (i == 1)
                    return 0;
                else {
                    c[0] = n;
                    return i;
                }
            }
        }
        c[0] = n;
        return 3;
    }
    default:
        if ('0' <= c[0] && c[0] <= '7') {
            char n = 0;
            for (int i = 0; i < 3; i++) {
                if ('0' <= c[i] && c[i] <= '7') {
                    n *= 8;
                    n += c[i] - '0';
                } else {
                    c[0] = n;
                    return i;
                }
            }
            c[0] = n;
            return 3;
        } else
            return 0;
    }
    return 1;
}

#define READ_IO_BUFFER 32
static char *_read_io_word(unsigned int max_len) {
    char *ptr = NULL;
    unsigned int ptr_len = 0;
    char buffer[READ_IO_BUFFER];
    char backslash_buffer[3];
    u8 len = 0, bpos = 3, bmax = 3;
    bool quoted = false, backslash = false;

    char c = _get_non_whitespace();
    for (;;) {
        if (backslash) {
            bmax = 3;
            backslash_buffer[0] = c;
            backslash_buffer[1] = __io_getchar();
            backslash_buffer[2] = backslash_buffer[1] == '\n' ? bmax = 2, 0 : __io_getchar();
            int length = _backslash_escape(backslash_buffer);
            if (!length) {
                buffer[len++] = '\\';
            } else {
                if (backslash_buffer[0])
                    buffer[len++] = backslash_buffer[0];
            }
            bpos = length;
            backslash = false;
        } else if (c == '"') {
            quoted = !quoted;
        } else {
            buffer[len++] = c;

            if (!quoted && (c == ' ' || c == '\n')) {
                break;
            } else if (c == '\\') {
                backslash = true;
                len--;
            }
        }

        // if backslash the next iteration might write four characters
        if (len >= READ_IO_BUFFER || (backslash && len >= READ_IO_BUFFER - 3)) {
            if (ptr)
                ptr = realloc(ptr, ptr_len + len);
            else
                ptr = malloc(len);
            memcpy(ptr + ptr_len, buffer, len);
            ptr_len += len;
            len = 0;
        }
        if (ptr_len + len >= max_len) {
            break;
        }

        if (bpos < bmax)
            c = backslash_buffer[bpos++];
        else
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
    if (!*hasFile) {
        open_root(currentFile);
        *hasFile = true;
    }
    return false;
}

static int _read_path(struct FATFile *file) {
    if (_update_current_file()) {
        return -4;
    }

    char *path = _read_io_word(300);
    unsigned int startIndex = 0, index = 0;

    *file = *currentFile;
    if (path[0] == '\n') {
        free(path);
        return -1;
    } else if (path[0] == '/') {
        open_root(file);
        startIndex = 1;
        index = 1;
    }

    char c = 0;
    while (path[index]) {
        while (path[index] != '/' && path[index + 1]) {
            index++;
        }

        c = path[index];
        path[index] = '\0';
        if (open(path + startIndex, file, sd_buffer)) {
            while (path[++index])
                ;
            free(path);
            return c == '\n' ? -3 : -2;
        }

        startIndex = ++index;
    }

    free(path);
    return c == '\n' ? 0 : 1;
}

static void _process_command(const char *command, bool no_more_input) {
    if (!strcmp(command, "ls")) {
        if (_update_current_file()) {
            // puts("Could not initialize SD");
        } else {
            if (no_more_input)
                ls(currentFile, sd_buffer);
            else {
                struct FATFile file;
                if (_read_path(&file) < -1) {
                    puts("Invalid path");
                } else {
                    ls(&file, sd_buffer);
                }
            }
        }
    } else if (!strcmp(command, "run")) {
        if (no_more_input)
            puts("not enough arguments");
        else if (_update_current_file()) {
            // puts("Could not initialize SD");
        } else {
            struct FATFile file;
            if (_read_path(&file) < 0) {
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
    } else if (!strcmp(command, "play")) {
        if (no_more_input)
            puts("not enough arguments");
        else if (_update_current_file()) {
            // puts("Could not initialize SD");
        } else {
            struct FATFile file;
            int status = _read_path(&file);
            if (status < 0) {
                puts("Could not open file");
            } else if (file.directory) {
                puts("Is a directory");
            } else {
                int rate = 5;
                if (status) {
                    char *rate_str = _read_io_word(14);
                    int index = 0;

                    if (rate_str[1]) {
                        rate = 0;
                        while (rate_str[index + 1]) {
                            if ('0' <= rate_str[index] && rate_str[index] <= '9') {
                                rate *= 10;
                                rate += rate_str[index] - '0';
                            } else {
                                rate = 0;
                                puts("Rate must be a number");
                                break;
                            }
                            index++;
                        }
                    }

                    free(rate_str);
                    if (rate > 0) {
                        if (rate < 2)
                            puts("Rate must be at least 2");
                        else
                            printf("Playing with rate %d\n", rate);
                    }
                }
                if (rate >= 2) {
                    play_audio(file, rate);
                    for (;;) {
                        get_keyboard_character();
                    }
                }
            }
        }
    } else if (!strcmp(command, "cd")) {
        if (_update_current_file()) {
            puts("Could not initialize SD");
        } else if (no_more_input)
            open_root(currentFile);
        else {
            struct FATFile file;
            if (_read_path(&file) < 0) {
                puts("Could not open file");
            } else if (!file.directory) {
                puts("Not a directory");
            } else {
                *currentFile = file;
            }
        }
    } else if (!strcmp(command, "eject")) {
        *hasFile = false;
        close_fat();
    } else if (!strcmp(command, "clear")) {
        printf("\033[2J\033[1;1H");
    } else if (!strcmp(command, "echo")) {
        if (no_more_input)
            putchar('\n');
        else {
            bool done = false;
            while (!done) {
                char *word = _read_io_word(300);
                printf("%s", word);

                char *mark = word;
                while (*mark)
                    if (*mark++ == '\n')
                        done = true;

                free(word);
            }
        }
    } else if (!strcmp(command, "cat")) {
        struct FATFile file;
        int code;
        if (no_more_input || (code = _read_path(&file)) == -1) {
            char c;
            while ((c = __io_getchar()) != '\004') {
                putchar(c);
            }
        } else {
            if (code > -4) {
                for (;;) {
                    if (code < 0) {
                        puts("Invalid path");
                    } else {
                        int length = 0;
                        while ((length = get_file_next_sector(&file, sd_buffer))) {
                            for (int i = 0; i < length; i++) {
                                putchar(sd_buffer[i]);
                            }
                        }
                    }
                    if (!(code == -2 || code == 1))
                        break;
                    code = _read_path(&file);
                }
            }
        }
    } else if (!strcmp(command, "help")) {
        puts("Command listing:");
        puts("\tcat [file ...]");
        puts("\tcd file");
        puts("\tclear");
        puts("\techo [string ...]");
        puts("\teject");
        puts("\thelp");
        puts("\tls [file]");
        puts("\tplay file [rate]");
        puts("\trun file");
    } else if (!*command) {
        // empty, ignore
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
        else if (command[index] == '\004') {
            print_console_prompt();
            return;
        }
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
        puts("\033[48;5;255;30m%");
    printf("\033[m> ");
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
        // if the position where the code wants to be loaded is before the break, there
        // could be important things in the way, do not continue to load the code and return
        // error flag
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
        printf("Break is at %p\n", sbrk(0));
        printf("Code wants to load at %p\n", code_load_position);
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

    // initialize .text, .rodata, .ARM.extab, .ARM, .preinit_array, .init_array,
    // .fini_array, .data
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
