#ifndef __LCD_H__
#define __LCD_H__
#include "types.h"

#define SPI SPI1
#define DMA DMA2_Channel4

#define X_CMD 0x2a
#define Y_CMD 0x2b
#define S_CMD 0x2c

#define DC_HIGH() GPIOB->BSRR = GPIO_BSRR_BS_6;
#define DC_LOW() GPIOB->BSRR = GPIO_BSRR_BR_6;
#define RESET_HIGH() GPIOB->BSRR = GPIO_BSRR_BS_7;
#define RESET_LOW() GPIOB->BSRR = GPIO_BSRR_BR_7;
#define CS_HIGH() GPIOB->BSRR = GPIO_BSRR_BS_8;
#define CS_LOW() GPIOB->BSRR = GPIO_BSRR_BR_8;

void init_screen(u8 direction);
void init_lcd_spi(void);
void init_lcd_dma(void);

typedef enum { GPU_ENABLE = 0, GPU_DISABLE = 1, TEXT_SENDING = 2 } LCD_FLAG;
typedef u8 LCD_STATE;

void set_lcd_flag(LCD_FLAG toset);
void clear_lcd_flag(LCD_FLAG toclear);
int check_lcd_flag(LCD_FLAG tocheck);

#endif
