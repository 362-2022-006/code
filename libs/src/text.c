#include <stdlib.h>
#include <stm32f0xx.h>
#include <string.h>

#include "font.h"
#include "gpu.h"
#include "keyboard.h"
#include "lcd.h"
#include "types.h"

#define SPI SPI1
#define DMA DMA2_Channel4

#define COLUMNS 39
#define LINES 35

u16 *const start_line = (u16 *)0x200001FA;
int16_t *const current_line_buffer = (int16_t *)0x200001FC;
int16_t *const current_column_buffer = (int16_t *)0x200001FE;
char *const screen_buffer = (char *)0x20000200;

static bool use_buffer = true;
static int16_t current_line_temp = 0;
static int16_t current_column_temp = 0;

static int16_t *current_line;
static int16_t *current_column;

void start_send(u16 start_x, u16 end_x, u16 start_y, u16 end_y) {
    while (SPI->SR & SPI_SR_BSY)
        ;
    CS_LOW();

    // set x window
    while (SPI->SR & SPI_SR_BSY)
        ;
    // bad value to force to 8 bit
    SPI->CR2 &= ~SPI_CR2_DS;
    DC_LOW();
    *(u8 *)&SPI->DR = 0x2a; // x_cmd
    while (SPI->SR & SPI_SR_BSY)
        ;
    DC_HIGH();
    // back to 16 bit
    SPI->CR2 |= SPI_CR2_DS;
    SPI->DR = start_x;
    while (SPI->SR & SPI_SR_BSY)
        ;
    SPI->DR = end_x;

    // set y window
    while (SPI->SR & SPI_SR_BSY)
        ;
    // bad value to force to 8 bit
    SPI->CR2 &= ~SPI_CR2_DS;
    DC_LOW();
    *(u8 *)&SPI->DR = 0x2b; // y_cmd
    while (SPI->SR & SPI_SR_BSY)
        ;
    DC_HIGH();
    // back to 16 bit
    SPI->CR2 |= SPI_CR2_DS;
    SPI->DR = start_y;
    while (SPI->SR & SPI_SR_BSY)
        ;
    SPI->DR = end_y;

    // send start command
    while (SPI->SR & SPI_SR_BSY)
        ;
    // bad value to force to 8 bit
    SPI->CR2 &= ~SPI_CR2_DS;
    DC_LOW();
    *(u8 *)&SPI->DR = 0x2c; // s_cmd
    while (SPI->SR & SPI_SR_BSY)
        ;
    DC_HIGH();
    // back to 16 bit
    SPI->CR2 |= SPI_CR2_DS;
}

#define DEFAULT_BACKGROUND 0x1082
#define DEFAULT_FOREGROUND 0xef3d

static u16 _lookup_color(u8 code, bool foreground) {
    const static u16 ansi_colors[15] = {
        0xd186, 0x15ec, 0xef22, 0x2399, 0xc218, 0x155a, DEFAULT_FOREGROUND, 0x6b4d, 0xf26a,
        0x268e, 0xffa8, 0x3c9d, 0xdb9b, 0x2ddb, 0xffff};

    if (code == 0) {
        return foreground ? 0x0000 : DEFAULT_BACKGROUND;
    } else if (code < 16) {
        return ansi_colors[code - 1];
    } else if (code < 232) {
        code -= 16;
        u8 b = (code % 6) * 6.2 + 0.5;
        code /= 6;
        u8 g = (code % 6) * 12.6 + 0.5;
        u8 r = (code / 6) * 6.2 + 0.5;
        return ((r & 0x1f) << 11) | ((g & 0x3f) << 5) | (b & 0x1f);
    } else {
        u8 rb = (code - 232) * (31.0 / 23.0) + 0.5;
        u8 g = (code - 232) * (63.0 / 23.0) + 0.5;
        return ((rb & 0x1f) << 11) | ((g & 0x3f) << 5) | (rb & 0x1f);
    }
}

static int current_scroll = 0;

static u16 _line_to_pixel(int line) {
    u16 val = ((line + current_scroll) * 9) % 320;
    return val;
}

void blank_screen(void) {
    start_send(0, 319, 0, 239);

    u16 color = _lookup_color(0, false);

    for (int i = 0; i < 240 * 320; i++) {
        while (SPI->SR & SPI_SR_BSY)
            ;
        SPI->DR = color;
    }

    while (SPI->SR & SPI_SR_BSY)
        ;
}

void blank_line_to_line(int start_line, int end_line) {
    if (end_line < start_line) {
        return;
    }

    u16 start, mid, end;

    if (end_line - start_line >= 320 / 9) {
        start = 0;
        mid = 319;
        end = 319;
    } else {
        start = _line_to_pixel(start_line);

        if (end_line >= 320 / 9) {
            end = _line_to_pixel(0) - 1;
            if ((u16)(end + 320) < 320) {
                end += 320;
            }
        } else {
            end = (start + (end_line - start_line) * 9 + 8) % 320;
        }

        mid = start > end ? 319 : end;
    }

    u16 color = _lookup_color(0, false);

    start_send(start, mid, 0, 239);
    for (int i = 0; i < (mid - start + 1) * 240; i++) {
        while (!(SPI->SR & SPI_SR_TXE))
            ;
        SPI->DR = color;
    }

    if (mid == end) {
        while (!(SPI->SR & SPI_SR_TXE))
            ;
        CS_HIGH();
        return;
    }

    start_send(0, end, 0, 239);
    for (int i = 0; i < (end + 1) * 240; i++) {
        while (!(SPI->SR & SPI_SR_TXE))
            ;
        SPI->DR = color;
    }

    while (SPI->SR & SPI_SR_BSY)
        ;
    CS_HIGH();
}

static u16 _get_color(u16 *color, const u8 *params, bool foreground) {
    if (*params == 2) {
        *color = ((params[1] & 0xf8) << 8) | ((params[2] & 0xfc) << 3) | (params[3] >> 3);
        return 4;
    } else if (*params == 5) {
        *color = _lookup_color(params[1], foreground);
        return 2;
    }
    return 0; // error?
}

void scroll_screen(int lines) {
    if (lines == 0)
        return;

    CS_LOW();

    // send start command
    while (SPI->SR & SPI_SR_BSY)
        ;
    // bad value to force to 8 bit
    SPI->CR2 &= ~SPI_CR2_DS;
    DC_LOW();
    *(u8 *)&SPI->DR = 0x37;

    current_scroll += lines;
    if (current_scroll < 0) {
        current_scroll += 320;
    }
    if (lines >= LINES || lines <= -LINES) {
        current_scroll = 0;
    }
    u16 write_val = 320 - ((current_scroll * 9) % 320);

    while (SPI->SR & SPI_SR_BSY)
        ;
    DC_HIGH();

    // back to 16 bit
    SPI->CR2 |= SPI_CR2_DS;

    // send scroll amount
    SPI->DR = write_val;

    while (SPI->SR & SPI_SR_BSY)
        ;
    CS_HIGH();

    if (lines > 0) {
        blank_line_to_line(LINES - (int)lines, LINES);
    } else {
        blank_line_to_line(0, -lines);
        blank_line_to_line(LINES, LINES);
    }
}

#define MAX_VAL(a, b) (a > b ? a : b)
#define MIN_VAL(a, b) (a < b ? a : b)

static inline void _write_char(char c, u16 line, u16 col, u16 f_color, u16 b_color) {
    u16 xstart = _line_to_pixel(line);
    u16 ystart = 240 - (col + 1) * 6 - 3;

    c -= 0x20;
    start_send(xstart, MIN_VAL(xstart + 8, 319), ystart - 1, ystart + 6);

    for (int i = 5; i >= -1; i--) {
        for (int j = 7; j >= MAX_VAL(xstart - 312, -1); j--) {
            u16 printchar;
            if (j == -1 || i == 5 || i == -1)
                printchar = b_color;
            else
                printchar = ((font_0507[c][i] >> j) & 1) ? f_color : b_color;
            while (SPI->SR & SPI_SR_BSY)
                ;
            SPI->DR = printchar;
        }
    }

    if (use_buffer) {
        int location = (*start_line + line) % LINES * COLUMNS + col;
        screen_buffer[location] = c + 0x20;
    }

    while (SPI->SR & SPI_SR_BSY)
        ;
    CS_HIGH();

    if (xstart + 8 < 320)
        return;

    start_send(0, xstart + 8 - 320, ystart - 1, ystart + 6);

    for (int i = 5; i >= -1; i--) {
        for (int j = xstart - 313; j >= -1; j--) {
            u16 printchar;
            if (j == -1 || i == 5 || i == -1)
                printchar = b_color;
            else
                printchar = ((font_0507[c][i] >> j) & 1) ? f_color : b_color;
            while (SPI->SR & SPI_SR_BSY)
                ;
            SPI->DR = printchar;
        }
    }

    while (SPI->SR & SPI_SR_BSY)
        ;
    CS_HIGH();
}

static u8 vline_breaks = 0;

static u16 f_color = DEFAULT_FOREGROUND;
static u16 b_color = DEFAULT_BACKGROUND;

void __io_putchar(unsigned char c) {
    static u8 escape_state = 0;
    static u8 escape_index = 0;
    static u8 escape_num[8] = {0};

    if (escape_state) {
        if (escape_state == 1) {
            if (c == '[') {
                escape_state++;
            } else {
                // error
                goto fake_putchar_end_escape;
            }
        } else {
            if ('0' <= c && c <= '9') {
                escape_state = 3;
                escape_num[escape_index] *= 10;
                escape_num[escape_index] += c - '0';
            } else if (c == ';') {
                escape_state = 2;
                escape_index++;
            } else if (c == 'm') {
                // colors
                if (escape_index || escape_num[0]) {
                    for (int i = 0; i <= escape_index; i++) {
                        if (escape_num[i] == 0) {
                            f_color = _lookup_color(7, true);
                            b_color = _lookup_color(0, false);
                        } else if (escape_num[i] < 30) {
                            // not implemented
                        } else if (escape_num[i] <= 37) {
                            f_color = _lookup_color(escape_num[i] - 30, true);
                        } else if (escape_num[i] == 38) {
                            i += _get_color(&f_color, escape_num + i + 1, true);
                        } else if (escape_num[i] == 39) {
                            f_color = _lookup_color(7, true);
                        } else if (escape_num[i] <= 47) {
                            b_color = _lookup_color(escape_num[i] - 40, false);
                        } else if (escape_num[i] == 48) {
                            i += _get_color(&b_color, escape_num + i + 1, false);
                        } else if (escape_num[i] == 49) {
                            b_color = _lookup_color(0, false);
                        } else if (escape_num[i] < 90) {
                            // not implemented / invalid codes
                        } else if (escape_num[i] <= 97) {
                            f_color = _lookup_color(escape_num[i] - 82, true);
                        } else if (escape_num[i] < 100) {
                            // invalid codes
                        } else if (100 <= escape_num[i] && escape_num[i] <= 107) {
                            b_color = _lookup_color(escape_num[i] - 92, false);
                        }
                    }
                } else {
                    f_color = _lookup_color(7, true);
                    b_color = _lookup_color(0, false);
                }
                goto fake_putchar_end_escape;
            } else if (c == 'A' || c == 'B' || c == 'E' || c == 'F') {
                if (escape_index == 0) {
                    if (escape_state < 3) {
                        escape_num[0] = 1;
                    }
                    *current_line = (c == 'A' || c == 'F') ? *current_line - escape_num[0]
                                                           : *current_line + escape_num[0];
                    if (c == 'E' || c == 'F')
                        *current_column = 0;
                }
                goto fake_putchar_end_escape_movement;
            } else if (c == 'C' || c == 'D') {
                if (escape_index == 0) {
                    if (escape_state < 3) {
                        escape_num[0] = 1;
                    }
                    *current_column = c == 'D' ? *current_column - escape_num[0]
                                               : *current_column + escape_num[0];
                }
                goto fake_putchar_end_escape_movement;
            } else if (c == 'G') {
                if (escape_state < 3) {
                    escape_num[0] = 1;
                }
                *current_column = escape_num[0] - 1;
                goto fake_putchar_end_escape_movement;
            } else if (c == 'H' || c == 'f') {
                if (escape_state < 3) {
                    escape_num[escape_index] = 1;
                }
                if (escape_index == 0) {
                    escape_num[1] = 1;
                }
                *current_line = escape_num[0] - 1;
                *current_column = escape_num[1] - 1;
                goto fake_putchar_end_escape_movement;
            } else if (c == 'J' || c == 'K') {
                // TODO: clear buffer for other codes
                if (escape_num[0] == 2) {
                    if (c == 'J') {
                        blank_screen();
                        if (use_buffer)
                            memset(screen_buffer, 0, LINES * COLUMNS);
                    } else {
                        u16 color = _lookup_color(0, false);
                        for (int i = 0; i < COLUMNS; i++)
                            _write_char(' ', *current_line, i, 0, color);
                        if (use_buffer)
                            memset(screen_buffer + *current_line * COLUMNS, 0, COLUMNS);
                    }
                } else if (escape_num[0] == 0) {
                    u16 color = _lookup_color(0, false);
                    for (int i = *current_column; i < COLUMNS; i++) {
                        _write_char(' ', *current_line, i, 0, color);
                    }
                    if (c == 'J')
                        blank_line_to_line(*current_line + 1, LINES - 1);
                } else if (escape_num[0] == 1) {
                    u16 color = _lookup_color(0, false);
                    for (int i = 0; i < *current_column; i++) {
                        _write_char(' ', *current_line, i, 0, color);
                    }
                    if (c == 'J')
                        blank_line_to_line(0, *current_line - 1);
                }
                goto fake_putchar_end_escape;
            } else if (c == 'S' || c == 'T') {
                if (escape_state < 3) {
                    escape_num[0] = 1;
                }
                scroll_screen(escape_num[0] * (c == 'S' ? 1 : -1));
                goto fake_putchar_end_escape;
            } else {
                // not implemented / invalid
                goto fake_putchar_end_escape;
            }
        }
        goto fake_putchar_end;

    fake_putchar_end_escape_movement:
        vline_breaks = 0;
    fake_putchar_end_escape:
        escape_state = 0;
        escape_index = 0;
        memset(escape_num, 0, sizeof escape_num);
    } else {
        if (c >= 0x20) {
            _write_char(c, *current_line, *current_column, f_color, b_color);
            ++*current_column;
        } else {
            // control character
            switch (c) {
            case '\n':
                *current_column = 0;
                ++*current_line;
                vline_breaks = 0;
                break;
            case '\r':
                *current_column = 0;
                break;
            case '\b':
                --*current_column;
                break;
            case '\t':
                *current_column += 4;
                *current_column &= ~3;
                break;
            case '\v':
            case '\f':
                ++*current_line;
                vline_breaks = 0;
                break;
            case '\033':
                escape_state = 1;
                break;
            }
        }
    }

fake_putchar_end:
    if (*current_column < 0) {
        if (vline_breaks) {
            *current_column += COLUMNS;
            --*current_line;
            vline_breaks--;
        } else {
            *current_column = 0;
        }
    } else if (*current_column >= COLUMNS) {
        *current_column = 0;
        ++*current_line;
        vline_breaks++;
    }

    if (*current_line < 0) {
        *current_line = 0;
    } else if (*current_line >= LINES) {
        scroll_screen(1 + *current_line - LINES);
        *current_line = LINES - 1;

        if (use_buffer)
            memset(screen_buffer + *start_line * COLUMNS, 0, COLUMNS);
        *start_line = (*start_line + 1) % LINES;
    }
}

void set_screen_text_buffer(bool on) {
    use_buffer = on;
    current_line = use_buffer ? current_line_buffer : &current_line_temp;
    current_column = use_buffer ? current_column_buffer : &current_column_temp;
}

void init_text(bool use_buffer) {
    init_lcd_spi();
    SPI->CR2 &= ~SPI_CR2_TXDMAEN;

    set_screen_text_buffer(use_buffer);

    init_screen(3);
    blank_screen();

    if (!use_buffer)
        return;

    bool on_first_line = true;
    for (int line = *start_line; line != *start_line || on_first_line; line = (line + 1) % LINES) {
        for (int col = 0; col < COLUMNS; col++) {
            char c = screen_buffer[(line + *start_line) % LINES * COLUMNS + col];
            if (c)
                _write_char(c, line, col, DEFAULT_FOREGROUND, DEFAULT_BACKGROUND);
        }
        on_first_line = false;
    }
}

u16 get_current_line(void) { return *current_line; }

u16 get_current_column(void) { return *current_column; }

static char input_buffer[122];
static int8_t buffer_write_pos = -1, buffer_max_write_pos = -1;
static int8_t buffer_read_pos;
static bool found_newline = false;

static void _echo_input(char c) {
    if (!c)
        return;

    if (c == '\b') {
        if (buffer_write_pos < 0)
            return;

        if (input_buffer[buffer_write_pos] <= '\037') {
            __io_putchar('\b');
            __io_putchar(' ');
            __io_putchar('\b');
        }
        __io_putchar('\b');
        __io_putchar(' ');
        __io_putchar('\b');
    } else if (c <= '\037' && c != '\n') {
        __io_putchar('^');
        __io_putchar(c + 'A' - 1);
    } else {
        __io_putchar(c);
    }
}

static void _process_input_char(char c) {
    if (c == '\003')
        exit(143); // SIGTERM
    else if (c == '\n') {
        __io_putchar('\n');
        found_newline = true;
        buffer_read_pos = 0;
        input_buffer[++buffer_max_write_pos] = '\n';
        return;
    } else if (c == '\t') {
        // completely ignore tabs
        return;
    }

    if (buffer_write_pos == buffer_max_write_pos) {
        _echo_input(c);
        if (c == '\b') {
            if (buffer_write_pos > -1) {
                buffer_write_pos--;
                buffer_max_write_pos--;
            }
            return;
        }
        input_buffer[++buffer_write_pos] = c;
        buffer_max_write_pos++;
    } else {
        if (c == '\b') {
            if (buffer_write_pos > -1) {
                char *pos = input_buffer + buffer_write_pos;
                int len = buffer_max_write_pos - buffer_write_pos;
                memmove(pos, pos + 1, len);
                buffer_max_write_pos--;
                buffer_write_pos--;

                __io_putchar('\b');
                int16_t initial_col = *current_column;
                int16_t initial_line = *current_line;
                for (int i = 1; i <= len; i++) {
                    _echo_input(input_buffer[buffer_write_pos + i]);
                }
                __io_putchar(' ');
                *current_column = initial_col;
                *current_line = initial_line;
            }
        } else if (is_in_insert_mode()) {
            char *pos = input_buffer + buffer_write_pos;
            int len = buffer_max_write_pos - buffer_write_pos;
            memmove(pos + 2, pos + 1, len);
            buffer_max_write_pos++;
            buffer_write_pos++;

            _echo_input(c);
            input_buffer[buffer_write_pos] = c;

            int16_t initial_col = *current_column;
            int16_t initial_line = *current_line;
            for (int i = 1; i <= len; i++) {
                _echo_input(input_buffer[buffer_write_pos + i]);
            }
            *current_column = initial_col;
            *current_line = initial_line;
        } else {
            _echo_input(c);
            input_buffer[++buffer_write_pos] = c;
        }
    }
}

unsigned char __io_getchar(void) {
    u8 escape_state = 0;

    while (!found_newline) {
        char c = get_keyboard_character();
        if (!c)
            continue;

        if (escape_state == 1) {
            if (c == '[') {
                // valid next character
                escape_state++;
            } else {
                // invalid next character
                escape_state = 0;
                _process_input_char('\033');
                _process_input_char(c);
            }
            continue;
        } else if (escape_state == 2) {
            if (c == 'A') { // up
                // ignore for now
            } else if (c == 'B') { // down
                // ignore for now
            } else if (c == 'C') { // right
                if (buffer_write_pos < buffer_max_write_pos) {
                    __io_putchar('\033');
                    __io_putchar('[');
                    __io_putchar('C');
                    buffer_write_pos++;
                }
            } else if (c == 'D') { // left
                if (buffer_write_pos >= 0) {
                    __io_putchar('\b');
                    buffer_write_pos--;
                }
            } else {
                _process_input_char('\033');
                _process_input_char('[');
                _process_input_char(c);
            }
            escape_state = 0;
            continue;
        }

        if (c == '\033') {
            escape_state = 1;
            continue;
        }

        _process_input_char(c);
    }

    if (buffer_read_pos == buffer_max_write_pos) {
        found_newline = false;
        buffer_write_pos = -1;
        buffer_max_write_pos = -1;
    }

    return input_buffer[buffer_read_pos++];
}

void discard_input_line(void) {
    found_newline = false;
    buffer_write_pos = -1;
    buffer_max_write_pos = -1;
}

void set_text_cursor_display(bool on) {
    //
}
