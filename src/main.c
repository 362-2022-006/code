#include <stdio.h>
#include <stdlib.h>
#include <stm32f0xx.h>
#include <string.h>

#include "audio.h"
#include "console.h"
#include "fat.h"
#include "keyboard.h"
#include "sd.h"
#include "text.h"
#include "types.h"

#include "snake.h"

void *sbrk(int incr);
void soft_reset(void);

static void _wait_for_key(void) {
    // flush buffer
    while (get_keyboard_event())
        ;

    puts("Waiting for key...");

    for (;;) {
        if (get_keyboard_event()) {
            puts("Key pressed");
            return;
        }
    }
}

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

static bool _do_code(int *status) {
    uint8_t *sd_buffer = (uint8_t *)0x20004000;

    struct FATParameters params;
    if (init_fat(&params, sd_buffer)) {
        puts("Could not initialize SD");
        return 1;
    }

    struct FATFile root, file;
    open_root(&params, &root);
    file = root;

    if (open("TETRIS.BIN", &params, &file, sd_buffer)) {
        puts("Could not open TETRIS.BIN");
        return true;
    }

    if (file.directory) {
        puts("tetris.bin is a directory");
        return true;
    }

    int length = get_file_next_sector(&params, &file, sd_buffer);
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

    // initialize .text, .rodata, .ARM.extab, .ARM, .preinit_array, .init_array, .fini_array, .data
    do {
        length = get_file_next_sector(&params, &file, code_load_position);
        code_load_position += length;
    } while (length > 0);

    // initialize .bss directly after .data
    memset(code_load_position, 0, bss_length);

    // set code_loc to the first byte after .bss, where the heap starts
    code_load_position += bss_length;
    // set break (location where new memory on the heap will be allocated)
    sbrk(code_load_position - sbrk(0));

    puts("Call");

    _wait_for_key();

    set_screen_text_buffer(false);
    *status = entry_point();

    return false;
}

int main() {
    /*
    start_console(true);

    for (;;) {
        update_console();
    }

    start_audio();

    int status;
    if (_do_code(&status)) {
        puts("Error encountered");
        return 1;
    }

    start_console(false);
    printf("Function returned (%d)\n", status);

    _wait_for_key();

    exit(status);
    */

    run_snake();

    return 0;
}
