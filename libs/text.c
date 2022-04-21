#include <stdbool.h>
#include <stm32f0xx.h>
#include <string.h>

#include "font.h"
#include "lcd.h"
#include "types.h"

#define SPI SPI1
#define DMA DMA2_Channel4

void init_text() {
    init_lcd_spi();
    SPI->CR2 &= ~SPI_CR2_TXDMAEN;

    init_screen(3);
}

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

#define COL_MAX 38
#define LINE_MAX 34

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
    if (lines > LINE_MAX || lines < -LINE_MAX) {
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
        blank_line_to_line(LINE_MAX - (int)lines + 1, LINE_MAX + 1);
    } else {
        blank_line_to_line(0, -lines);
        blank_line_to_line(LINE_MAX + 1, LINE_MAX + 1);
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

void __io_putchar(unsigned char c) {
    static int16_t line = 0;
    static int16_t col = 0;
    static u8 vline_breaks = 0;

    static u16 f_color = DEFAULT_FOREGROUND;
    static u16 b_color = DEFAULT_BACKGROUND;

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
                    line = (c == 'A' || c == 'F') ? line - escape_num[0] : line + escape_num[0];
                    if (c == 'E' || c == 'F')
                        col = 0;
                }
                goto fake_putchar_end_escape;
            } else if (c == 'C' || c == 'D') {
                if (escape_index == 0) {
                    if (escape_state < 3) {
                        escape_num[0] = 1;
                    }
                    col = c == 'D' ? col - escape_num[0] : col + escape_num[0];
                }
                goto fake_putchar_end_escape;
            } else if (c == 'G') {
                if (escape_state < 3) {
                    escape_num[0] = 1;
                }
                col = escape_num[0] - 1;
                goto fake_putchar_end_escape;
            } else if (c == 'H' || c == 'f') {
                if (escape_state < 3) {
                    escape_num[escape_index] = 1;
                }
                if (escape_index == 0) {
                    escape_num[1] = 1;
                }
                line = escape_num[0] - 1;
                col = escape_num[1] - 1;
                goto fake_putchar_end_escape;
            } else if (c == 'J' || c == 'K') {
                if (escape_num[0] == 2) {
                    if (c == 'J')
                        blank_screen();
                    else {
                        u16 color = _lookup_color(0, false);
                        for (int i = 0; i <= COL_MAX; i++)
                            _write_char(' ', line, i, 0, color);
                    }
                } else if (escape_num[0] == 0) {
                    u16 color = _lookup_color(0, false);
                    for (int i = col; i <= COL_MAX; i++) {
                        _write_char(' ', line, i, 0, color);
                    }
                    if (c == 'J')
                        blank_line_to_line(line + 1, LINE_MAX);
                } else if (escape_num[0] == 1) {
                    u16 color = _lookup_color(0, false);
                    for (int i = 0; i < col; i++) {
                        _write_char(' ', line, i, 0, color);
                    }
                    if (c == 'J')
                        blank_line_to_line(0, line - 1);
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
    fake_putchar_end_escape:
        escape_state = 0;
        escape_index = 0;
        memset(escape_num, 0, sizeof escape_num);
    } else {
        if (c >= 0x20) {
            _write_char(c, line, col, f_color, b_color);
            col++;
        } else {
            // control character
            switch (c) {
            case '\n':
                col = 0;
                line++;
                vline_breaks = 0;
                break;
            case '\r':
                col = 0;
                break;
            case '\b':
                col--;
                break;
            case '\t':
                col += 4;
                col &= ~3;
                break;
            case '\v':
            case '\f':
                line++;
                vline_breaks = 0;
                break;
            case '\033':
                escape_state = 1;
                break;
            }
        }
    }

fake_putchar_end:
    if (col < 0) {
        if (vline_breaks) {
            col += COL_MAX + 1;
            line--;
            vline_breaks--;
        } else {
            col = 0;
        }
    } else if (col > COL_MAX) {
        col = 0;
        line++;
        vline_breaks++;
    }

    if (line < 0) {
        line = 0;
    } else if (line > LINE_MAX) {
        scroll_screen(line - LINE_MAX);
        line = LINE_MAX;
    }
}
