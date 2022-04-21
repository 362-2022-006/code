#include "stm32f0xx.h"
#include "text.h"
#include "font.h"
#include "lcd.h"
#include "types.h"

void init_text() {
    init_lcd_spi();
    SPI->CR2 &= ~SPI_CR2_TXDMAEN;

    init_screen(3);
}

u16 print_color = 0xffff;
const extern u8 font_0507[][5];

#define COL_MAX 37
#define LINE_MAX 31

void fake_putchar(unsigned char c) {
    static u8 line = 0;
    static u8 col = 0;
    if (c >= 0x20) {
        u16 xstart = line * 10 + 5;
        u16 ystart = 240 - (col + 1) * 6 - 3;
        CS_LOW();

        // set x window
        while (SPI->SR & SPI_SR_BSY)
            ;
        // bad value to force to 8 bit
        SPI->CR2 &= ~SPI_CR2_DS;
        DC_LOW();
        *(u8 *)&SPI->DR = X_CMD;
        while (SPI->SR & SPI_SR_BSY)
            ;
        DC_HIGH();
        // back to 16 bit
        SPI->CR2 |= SPI_CR2_DS;
        SPI->DR = xstart;
        while (SPI->SR & SPI_SR_BSY)
            ;
        SPI->DR = xstart + 7;

        // set y window
        while (SPI->SR & SPI_SR_BSY)
            ;
        // bad value to force to 8 bit
        SPI->CR2 &= ~SPI_CR2_DS;
        DC_LOW();
        *(u8 *)&SPI->DR = Y_CMD;
        while (SPI->SR & SPI_SR_BSY)
            ;
        DC_HIGH();
        // back to 16 bit
        SPI->CR2 |= SPI_CR2_DS;
        SPI->DR = ystart;
        while (SPI->SR & SPI_SR_BSY)
            ;
        SPI->DR = ystart + 5;
        while (SPI->SR & SPI_SR_BSY)
            ;

        // send start command
        while (SPI->SR & SPI_SR_BSY)
            ;
        // bad value to force to 8 bit
        SPI->CR2 &= ~SPI_CR2_DS;
        DC_LOW();
        *(u8 *)&SPI->DR = S_CMD;
        while (SPI->SR & SPI_SR_BSY)
            ;
        DC_HIGH();
        // back to 16 bit
        SPI->CR2 |= SPI_CR2_DS;

        c -= 0x20;
        for (int i = 4; i >= 0; i--) {
            for (int j = 7; j >= 0; j--) {
                u16 printchar = ((font_0507[c][i] >> j) & 1) * print_color;
                while (SPI->SR & SPI_SR_BSY)
                    ;
                SPI->DR = printchar;
            }
            while (SPI->SR & SPI_SR_BSY)
                ;
        }
        CS_HIGH();
        col++;
        if (col > COL_MAX) {
            line++;
            col = 0;
        }
    } else {
        // control character
        switch (c) {
        case '\n':
            line++;
            col = 0;
            break;
        case '\r':
            col = 0;
            break;
        case '\b':
            if (col == 0) {
                col = COL_MAX;
                line--;
            } else
                col--;
            break;
        case '\t':
            col += 4;
            col &= ~3;
            if (col > COL_MAX)
                col = 0;
            break;
        case '\v':
        case '\f':
            line++;
            break;
        }
    }
}

void fake_putstring(const char *str) {
    while (*str) {
        fake_putchar(*str++);
    }
}
