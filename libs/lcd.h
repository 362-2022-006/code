#ifndef __LCD_H__
#define __LCD_H__
#include "types.h"

#define SPI SPI1
#define DMA DMA2_Channel4

#define X_CMD 0x2a
#define Y_CMD 0x2b
#define S_CMD 0x2c

#define DC_HIGH() GPIOB->BSRR = GPIO_BSRR_BS_14;
#define DC_LOW() GPIOB->BSRR = GPIO_BSRR_BR_14;
#define RESET_HIGH() GPIOB->BSRR = GPIO_BSRR_BS_11;
#define RESET_LOW() GPIOB->BSRR = GPIO_BSRR_BR_11;
#define CS_HIGH() GPIOB->BSRR = GPIO_BSRR_BS_8;
#define CS_LOW() GPIOB->BSRR = GPIO_BSRR_BR_8;

void init_screen(u8 direction);
void init_lcd_spi(void);
void setup_dma(void);

#endif
