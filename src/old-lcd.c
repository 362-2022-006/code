
#include "../libs/lcd.h"
#include "stm32f0xx.h"
#include <stdint.h>

lcd_dev_t lcddev;

extern const Picture sprite;
extern const Picture bars;
extern const Picture rick;
extern const Picture transparent_rick;
extern const RunLengthPicture test_image;
extern const RunLengthPicture run_length;

typedef struct {
    Picture *image;
    u16 x;
    u16 y;
    u8 meta;
    /*
    7:   1 --> rle, 0 --> normal
    6-3: row (if rle)
    2-0: state
        7 --> pending send select x
        6 --> pending send x start
        5 --> pending send x end
        4 --> pending send select y
        3 --> pending send y start
        2 --> pending send y end
        1 --> pending send start
        0 --> pending send data
    */
} DMAFIFO;

#define FIFO_SIZE 5

DMAFIFO dma_fifo[FIFO_SIZE];
int fifo_start = 0;
int fifo_end = 0; // first available spot

#define DMA_FIFO_SIZE (u8)0x80
#define DMA_FIFO_REG (u8)0x40
#define DMA_FIFO_CNDTR (u8)0x3f

#define SPI SPI1
#define DMA DMA1_Channel3

#define CS_NUM 8
#define CS_BIT (1 << CS_NUM)
#define CS_HIGH                                                                                    \
    do {                                                                                           \
        GPIOB->BSRR = GPIO_BSRR_BS_8;                                                              \
    } while (0)
#define CS_LOW                                                                                     \
    do {                                                                                           \
        GPIOB->BSRR = GPIO_BSRR_BR_8;                                                              \
    } while (0)
#define RESET_NUM 11
#define RESET_BIT (1 << RESET_NUM)
#define RESET_HIGH                                                                                 \
    do {                                                                                           \
        GPIOB->BSRR = GPIO_BSRR_BS_11;                                                             \
    } while (0)
#define RESET_LOW                                                                                  \
    do {                                                                                           \
        GPIOB->BSRR = GPIO_BSRR_BR_11;                                                             \
    } while (0)
#define DC_NUM 14
#define DC_BIT (1 << DC_NUM)
#define DC_HIGH                                                                                    \
    do {                                                                                           \
        GPIOB->BSRR = GPIO_BSRR_BS_14;                                                             \
    } while (0)
#define DC_LOW                                                                                     \
    do {                                                                                           \
        GPIOB->BSRR = GPIO_BSRR_BR_14;                                                             \
    } while (0)

void disable_dma(void) { DMA->CCR &= ~DMA_CCR_EN; }

void enable_dma(void) { DMA->CCR |= DMA_CCR_EN; }

void set_dma_addr(const void *val) { DMA->CMAR = (uint32_t)val; }

void set_dma_num(int val) { DMA->CNDTR = val; }

// Set the CS pin low if val is non-zero.
// Note that when CS is being set high again, wait on SPI to not be busy.
void tft_select(int val) {
    if (val == 0) {
        while (SPI->SR & SPI_SR_BSY)
            ;
        CS_HIGH;
    } else {
        while ((GPIOB->ODR & (CS_BIT)) == 0)
            ; // If CS is already low, this is an error.  Loop forever.
        // This has happened because something called a drawing subroutine
        // while one was already in process.  For instance, the main()
        // subroutine could call a long-running LCD_DrawABC function,
        // and an ISR interrupts it and calls another LCD_DrawXYZ function.
        // This is a common mistake made by students.
        // This is what catches the problem early.
        CS_LOW;
    }
}

// If val is non-zero, set nRESET low to reset the display.
static void tft_reset(int val) {
    if (val) {
        RESET_LOW;
    } else {
        RESET_HIGH;
    }
}

// If
static void tft_reg_select(int val) {
    if (val == 1) { // select registers
        DC_LOW;     // clear
    } else {        // select data
        DC_HIGH;    // set
    }
}

// Write to an LCD "register"
void LCD_WR_REG(uint8_t data) {
    while ((SPI->SR & SPI_SR_BSY) != 0)
        ;
    // Don't clear RS until the previous operation is done.
    lcddev.reg_select(1);
    *((uint8_t *)&SPI->DR) = data;
}

// Write 8-bit data to the LCD
void LCD_WR_DATA(uint8_t data) {
    while ((SPI->SR & SPI_SR_BSY) != 0)
        ;
    // Don't set RS until the previous operation is done.
    lcddev.reg_select(0);
    *((uint8_t *)&SPI->DR) = data;
}

void LCD_WR_DATA16(u16 data) {
    while ((SPI->SR & SPI_SR_BSY) != 0)
        ;
    LCD_WriteData16_Prepare();
    SPI->DR = data;
    LCD_WriteData16_End();
}

/*
static inline void nano_wait(int n) {
    while (n > 0) {
        n -= 83;
    }
}
*/

inline void nano_wait(signed int n) {
    asm volatile("           mov r0,%0\n"
                 "repeat%=:  sub r0,#83\n"
                 "           bgt repeat%=\n"
                 :
                 : "r"(n)
                 : "r0", "cc");
}

void LCD_Reset(void) {
    lcddev.reset(1);        // Assert reset
    nano_wait(100000000); // Wait 0.1s
    lcddev.reset(0);        // De-assert reset
    nano_wait(50000000);  // Wait 0.05s
}

// Select an LCD "register" and write 8-bit data to it.
void LCD_WriteReg(uint8_t LCD_Reg, uint16_t LCD_RegValue) {
    LCD_WR_REG(LCD_Reg);
    LCD_WR_DATA(LCD_RegValue);
}

// Configure the lcddev fields for the display orientation.
void LCD_direction(u8 direction) {
    lcddev.setxcmd = 0x2A;
    lcddev.setycmd = 0x2B;
    lcddev.wramcmd = 0x2C;
    switch (direction) {
    case 0:
        lcddev.width = LCD_W;
        lcddev.height = LCD_H;
        LCD_WriteReg(0x36, (1 << 3) | (0 << 6) | (0 << 7)); // BGR==1,MY==0,MX==0,MV==0
        break;
    case 1:
        lcddev.width = LCD_H;
        lcddev.height = LCD_W;
        LCD_WriteReg(0x36, (1 << 3) | (0 << 7) | (1 << 6) | (1 << 5)); // BGR==1,MY==1,MX==0,MV==1
        break;
    case 2:
        lcddev.width = LCD_W;
        lcddev.height = LCD_H;
        LCD_WriteReg(0x36, (1 << 3) | (1 << 6) | (1 << 7)); // BGR==1,MY==0,MX==0,MV==0
        break;
    case 3:
        lcddev.width = LCD_H;
        lcddev.height = LCD_W;
        LCD_WriteReg(0x36, (1 << 3) | (1 << 7) | (1 << 5)); // BGR==1,MY==1,MX==0,MV==1
        break;
    default:
        break;
    }
}

// Do the initialization sequence for the display.
void LCD_Init(void (*reset)(int), void (*select)(int), void (*reg_select)(int)) {
    lcddev.reset = tft_reset;
    lcddev.select = tft_select;
    lcddev.reg_select = tft_reg_select;
    // tft_reset(0);
    // tft_reg_select(0);
    if (reset)
        lcddev.reset = reset;
    if (select)
        lcddev.select = select;
    if (reg_select)
        lcddev.reg_select = reg_select;
    lcddev.select(1);
    LCD_Reset();
    // Initialization sequence for 2.2inch ILI9341
    LCD_WR_REG(0xCF);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0xD9); // C1
    LCD_WR_DATA(0X30);
    LCD_WR_REG(0xED);
    LCD_WR_DATA(0x64);
    LCD_WR_DATA(0x03);
    LCD_WR_DATA(0X12);
    LCD_WR_DATA(0X81);
    LCD_WR_REG(0xE8);
    LCD_WR_DATA(0x85);
    LCD_WR_DATA(0x10);
    LCD_WR_DATA(0x7A);
    LCD_WR_REG(0xCB);
    LCD_WR_DATA(0x39);
    LCD_WR_DATA(0x2C);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x34);
    LCD_WR_DATA(0x02);
    LCD_WR_REG(0xF7);
    LCD_WR_DATA(0x20);
    LCD_WR_REG(0xEA);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x00);
    LCD_WR_REG(0xC0);  // Power control
    LCD_WR_DATA(0x21); // VRH[5:0]  //1B
    LCD_WR_REG(0xC1);  // Power control
    LCD_WR_DATA(0x12); // SAP[2:0];BT[3:0] //01
    LCD_WR_REG(0xC5);  // VCM control
    LCD_WR_DATA(0x39); // 3F
    LCD_WR_DATA(0x37); // 3C
    LCD_WR_REG(0xC7);  // VCM control2
    LCD_WR_DATA(0XAB); // B0
    LCD_WR_REG(0x36);  // Memory Access Control
    LCD_WR_DATA(0x48);
    LCD_WR_REG(0x3A);
    LCD_WR_DATA(0x55);
    LCD_WR_REG(0xB1);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x1B); // 1A
    LCD_WR_REG(0xB6);  // Display Function Control
    LCD_WR_DATA(0x0A);
    LCD_WR_DATA(0xA2);
    LCD_WR_REG(0xF2); // 3Gamma Function Disable
    LCD_WR_DATA(0x00);
    LCD_WR_REG(0x26); // Gamma curve selected
    LCD_WR_DATA(0x01);

    LCD_WR_REG(0xE0); // Set Gamma
    LCD_WR_DATA(0x0F);
    LCD_WR_DATA(0x23);
    LCD_WR_DATA(0x1F);
    LCD_WR_DATA(0x0B);
    LCD_WR_DATA(0x0E);
    LCD_WR_DATA(0x08);
    LCD_WR_DATA(0x4B);
    LCD_WR_DATA(0XA8);
    LCD_WR_DATA(0x3B);
    LCD_WR_DATA(0x0A);
    LCD_WR_DATA(0x14);
    LCD_WR_DATA(0x06);
    LCD_WR_DATA(0x10);
    LCD_WR_DATA(0x09);
    LCD_WR_DATA(0x00);
    LCD_WR_REG(0XE1); // Set Gamma
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x1C);
    LCD_WR_DATA(0x20);
    LCD_WR_DATA(0x04);
    LCD_WR_DATA(0x10);
    LCD_WR_DATA(0x08);
    LCD_WR_DATA(0x34);
    LCD_WR_DATA(0x47);
    LCD_WR_DATA(0x44);
    LCD_WR_DATA(0x05);
    LCD_WR_DATA(0x0B);
    LCD_WR_DATA(0x09);
    LCD_WR_DATA(0x2F);
    LCD_WR_DATA(0x36);
    LCD_WR_DATA(0x0F);
    LCD_WR_REG(0x2B);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x01);
    LCD_WR_DATA(0x3f);
    LCD_WR_REG(0x2A);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0xef);
    LCD_WR_REG(0x11);     // Exit Sleep
    nano_wait(120000000); // Wait 120 ms
    LCD_WR_REG(0x29);     // Display on

    // LCD_direction(USE_HORIZONTAL);
    // LCD_direction(2); // for drawing
    LCD_direction(3); // for text
    lcddev.select(0);
}

/*
void LCD_Setup() {
    init_lcd_spi();
    tft_select(0);
    tft_reset(0);
    tft_reg_select(0);
    LCD_Init(tft_reset, tft_select, tft_reg_select);
}
*/

void LCD_SetWindow(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd) {
    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA16(xStart);
    LCD_WR_DATA16(xEnd);

    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA16(yStart);
    LCD_WR_DATA16(yEnd);

    LCD_WR_REG(lcddev.wramcmd);
}

void LCD_WriteData16_Prepare() {
    lcddev.reg_select(0);
    SPI->CR2 |= SPI_CR2_DS;
}

void LCD_WriteData16(u16 data) {
    while ((SPI->SR & SPI_SR_TXE) == 0)
        ;
    SPI->DR = data;
}

void LCD_WriteData16_End() {
    while ((SPI->SR & SPI_SR_TXE) == 0)
        ;
    SPI->CR2 &= ~SPI_CR2_DS; // bad value forces it back to 8-bit mode
}

void LCD_DrawPicture(int x0, int y0, const Picture *pic) {
    int x1 = x0 + pic->width - 1;
    int y1 = y0 + pic->height - 1;
    if (x0 >= lcddev.width || y0 >= lcddev.height || x1 < 0 || y1 < 0)
        return; // Completely outside of screen.  Nothing to do.
    lcddev.select(1);
    int xs = 0;
    int ys = 0;
    int xe = pic->width;
    int ye = pic->height;
    if (x0 < 0) {
        xs = -x0;
        x0 = 0;
    }
    if (y0 < 0) {
        ys = -y0;
        y0 = 0;
    }
    if (x1 >= lcddev.width) {
        xe -= x1 - (lcddev.width - 1);
        x1 = lcddev.width - 1;
    }
    if (y1 >= lcddev.height) {
        ye -= y1 - (lcddev.height - 1);
        y1 = lcddev.height - 1;
    }

    LCD_SetWindow(x0, y0, x1, y1);
    LCD_WriteData16_Prepare();

    u16 *data = (u16 *)pic->pixel_data;
    for (int y = ys; y < ye; y++) {
        u16 *row = &data[y * pic->width + xs];
        for (int x = xs; x < xe; x++)
            LCD_WriteData16(*row++);
    }

    LCD_WriteData16_End();
    lcddev.select(0);
}

void draw_new(int x0, int y0, const Picture *pic) {
    lcddev.select(1);
    LCD_SetWindow(x0, y0, x0 + pic->width - 1, y0 + pic->height - 1);
    LCD_WriteData16_Prepare();
    disable_dma();
    set_dma_addr(&(pic->pix2));
    set_dma_num(pic->width * pic->height);
    enable_dma();
    // nano_wait(10000000);
    // LCD_WriteData16_End();
    // lcddev.select(0);
}

void draw_run_length(int x0, int y0, const RunLengthPicture *pic) {
    for (int i = 0; i < 16; i++) {
        lcddev.select(1);
        LCD_SetWindow(x0 + pic->rows[i].start, y0 + i,
                      x0 + pic->rows[i].start + pic->rows[i].length - 1, y0 + i);
        LCD_WriteData16_Prepare();
        disable_dma();
        set_dma_addr(&(pic->rows[i].data));
        set_dma_num(pic->rows[i].length);
        enable_dma();
        LCD_WriteData16_End();
        lcddev.select(0);
    }
}

#ifdef UNDEFINED

int xpos = 0;
int ypos = 0;
const static Picture *image = &transparent_rick;

/*
void DMA1_CH2_3_DMA2_CH1_2_IRQHandler(void) {
    DMA1->IFCR |= DMA_IFCR_CTCIF3;
    LCD_WriteData16_End();
    lcddev.select(0);
    xpos += image->width;
    if (xpos > 240 - image->width) {
        xpos = 0;
        ypos += image->height;
        if (ypos > 320 - image->height) {
            disable_dma();
            return;
        }
    }
    draw_new(xpos, ypos, image);
}
*/

/*
void DMA1_CH2_3_DMA2_CH1_2_IRQHandler(void) {
    DMA1->IFCR |= DMA_IFCR_CTCIF3;
    if (fifo_start == fifo_end) {
        DMA->CCR &= ~DMA_CCR_EN;
        return;
    }
    DMA->CCR &= ~DMA_CCR_EN;
    DMA->CMAR = (uint32_t)dma_fifo[fifo_start].data;
    DMA->CNDTR = (dma_fifo[fifo_start].meta & DMA_FIFO_CNDTR) + 1;
    if (dma_fifo[fifo_start].meta & DMA_FIFO_SIZE) {
        SPI->CR2 |= SPI_CR2_DS; // explicit 16 bit
        DMA->CCR |= DMA_CCR_MSIZE_0;
        DMA->CCR |= DMA_CCR_PSIZE_0;
    } else {
        SPI->CR2 &= ~SPI_CR2_DS; // bad value --> 8 bit
        DMA->CCR &= ~DMA_CCR_MSIZE_1;
        DMA->CCR &= ~DMA_CCR_PSIZE_1;
    }
    if (dma_fifo[fifo_start].meta & DMA_FIFO_REG) {
        DC_LOW; // select register
    } else {
        DC_HIGH; // select not register
    }
    DMA->CCR |= DMA_CCR_EN;
    fifo_start++;
}
*/

void DMA1_CH2_3_DMA2_CH1_2_IRQHandler(void) {
    DMA1->IFCR |= DMA_IFCR_CTCIF3;
    /*
    7 --> pending send select x
    6 --> pending send x start
    5 --> pending send x end
    4 --> pending send select y
    3 --> pending send y start
    2 --> pending send y end
    1 --> pending send start
    0 --> pending send data
    */
    DMA->CCR &= DMA_CCR_EN;
    switch (dma_fifo[fifo_start].meta & 0x7) {
    case 7:
        SPI->CR2 &= ~SPI_CR2_DS; // bad value --> 8 bit
        DMA->CMAR = &lcddev.setxcmd;
        break;
    case 6:
        SPI->CR2 |= SPI_CR2_DS; // 16 bit
        DMA->CMAR = &dma_fifo[fifo_start].x;
        break;
    case 5:
        DMA->CMAR = dma_fifo[fifo_start].x + dma_fifo[fifo_start].image->width;
        break;
    case 4:
        SPI->CR2 &= ~SPI_CR2_DS; // bad value --> 8 bit
        SPI->DR = lcddev.setycmd;
        break;
    case 3:
        SPI->CR2 |= SPI_CR2_DS; // 16 bit
        SPI->DR = dma_fifo[fifo_start].y;
        break;
    case 2:
        SPI->DR = dma_fifo[fifo_start].y + dma_fifo[fifo_start].image->height;
        break;
    case 1:
        SPI->CR2 &= ~SPI_CR2_DS; // bad value -- 8 bit
        SPI->DR = lcddev.wramcmd;
        break;
    case 0:
        SPI->CR2 |= SPI_CR2_DS; // 16 bit
        DMA->CMAR = (uint32_t) & (dma_fifo[fifo_start].image->pix2);
        DMA->CNDTR = dma_fifo[fifo_start].image->width * dma_fifo[fifo_start].image->height;
        fifo_start++;
        dma_fifo[fifo_start].meta++; // because it always get decremented after switch
        break;
    }
    DMA->CCR |= DMA_CCR_EN;
    dma_fifo[fifo_start].meta--;
}
#endif
