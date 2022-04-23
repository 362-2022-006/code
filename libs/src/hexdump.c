#include "hexdump.h"

#include <stdbool.h>
#include <stdio.h>

void _hexdump_print_char(uint8_t c) {
    if (' ' <= c && c <= '~') {
        printf("%c", c);
    } else {
        fputc('.', stdout);
    }
}

void hexdump_offset(const void *data, uint32_t offset, uint32_t length) {
    uint8_t *data_int = (uint8_t *)data;
    uint32_t rows = ((length - 1) / 16) + 1;
    bool dup_flag = false;

    for (int r = 0; r < rows; r++) {
        uint32_t chars_in_row = length - r * 16;
        if (chars_in_row > 16)
            chars_in_row = 16;

        if (r && chars_in_row == 16) {
            bool duplicated = true;
            for (int c = 0; c < 16; c++) {
                if (data_int[r * 16 + c] != data_int[r * 16 + c - 16]) {
                    duplicated = false;
                    break;
                }
            }
            if (duplicated) {
                dup_flag = true;
                continue;
            } else if (dup_flag) {
                dup_flag = false;
                puts("*");
            }
        } else if (dup_flag) {
            dup_flag = false;
            puts("*");
        }

        printf("%08lx  ", offset + r * 16);
        for (int c = 0; c < chars_in_row; c++)
            printf(c == 7 ? "%02x  " : "%02x ", data_int[r * 16 + c]);

        if (chars_in_row < 16) {
            if (chars_in_row < 8)
                putc(' ', stdout);
            for (int i = 0; i < 16 - chars_in_row; i++)
                printf("   ");
        }
        printf(" |");

        for (int c = 0; c < chars_in_row; c++)
            _hexdump_print_char(data_int[r * 16 + c]);
        puts("|");
    }
    if (dup_flag) {
        puts("*");
    }
    printf("%08lx\n", offset + length);
}

void hexdump(const void *data, uint32_t length) { hexdump_offset(data, 0, length); }
