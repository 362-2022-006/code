#include <stdbool.h>
#include <stdio.h>
#include <stm32f0xx.h>
#include <string.h>

#include "console.h"
#include "delay.h"
#include "fat.h"
#include "hexdump.h"
#include "keyboard.h"
#include "sd.h"
#include "types.h"

void *sbrk(int incr);

static void _wait_for_key(void) {
    puts("Waiting for key...");

    const KeyEvent *event;
    for (;;) {
        while ((event = get_keyboard_event())) {
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
    return false;
}

static bool _do_code(void) {
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
    void (*entry_point)() = (void (*)())((uint32_t *)sd_buffer)[1];
    u32 bss_length = ((uint32_t *)sd_buffer)[2];

    if (_check_position(code_load_position)) {
        puts("Not enough space to load code");
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

    entry_point();

    return false;
}

int main() {
    start_console(false);

    if (_do_code()) {
        puts("Error encountered");
    }

    start_console(false);
    puts("Function returned");

    return 0;
}
