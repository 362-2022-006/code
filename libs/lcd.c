#include "lcd.h"
#include <stm32f0xx.h>

void init_lcd_spi(void) {
    RCC->AHBENR |= RCC_AHBENR_GPIOBEN;
    GPIOB->MODER &=
        ~(0x3 << (3 * 2) | 0x3 << (5 * 2) | 0x3 << (8 * 2) | 0x3 << (11 * 2) | 0x3 << (14 * 2));
    GPIOB->MODER |=
        0x2 << (3 * 2) | 0x2 << (5 * 2) | 0x1 << (8 * 2) | 0x1 << (11 * 2) | 0x1 << (14 * 2);
    GPIOB->ODR |= 0x1 << 8 | 0x1 << 11 | 0x1 << 14;
    GPIOB->AFR[0] &= ~(0xf << (3 * 4) | 0xf << (5 * 4));

    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    SPI->CR1 &= ~SPI_CR1_SPE;
    // SPI->CR1 &= ~SPI_CR1_BR;
    // SPI->CR1 |= SPI_CR1_BR_0 | SPI_CR1_BR_1;
    SPI->CR1 |= SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI;
    SPI->CR2 |= SPI_CR2_DS; // 16 bit
    // SPI->CR2 |= SPI_CR2_TXDMAEN;
    SPI->CR1 |= SPI_CR1_SPE;
}

void setup_dma(void) {
    // setup:
    // 16 bit memory and periph
    // no peripheral increment
    // mem -> periph
    // transfer compete interrupt on
    RCC->AHBENR |= RCC_AHBENR_DMA2EN;
    DMA->CCR &= ~(DMA_CCR_EN);
    DMA->CPAR = (int)&(SPI->DR);
    DMA->CCR |= DMA_CCR_DIR | DMA_CCR_TCIE | DMA_CCR_MINC | DMA_CCR_MSIZE_0 | DMA_CCR_PSIZE_0;
    DMA2->RMPCR |= 0x3 << ((4 - 1) * 4);

    NVIC_EnableIRQ(DMA1_Ch4_7_DMA2_Ch3_5_IRQn);
}

// Write to an LCD "register"
static void LCD_WR_REG(uint8_t data) {
    while (SPI->SR & SPI_SR_BSY)
        ;
    // Don't clear RS until the previous operation is done.
    DC_LOW();
    *((uint8_t *)&SPI->DR) = data;
}

// Write 8-bit data to the LCD
static void LCD_WR_DATA(uint8_t data) {
    while (SPI->SR & SPI_SR_BSY)
        ;
    // Don't set RS until the previous operation is done.
    DC_HIGH();
    *((uint8_t *)&SPI->DR) = data;
}

static inline void nano_wait(signed int n) {
    asm volatile("           mov r0,%0\n"
                 "repeat%=:  sub r0,#83\n"
                 "           bgt repeat%=\n"
                 :
                 : "r"(n)
                 : "r0", "cc");
}

// Select an LCD "register" and write 8-bit data to it.
static void LCD_WriteReg(uint8_t LCD_Reg, uint16_t LCD_RegValue) {
    LCD_WR_REG(LCD_Reg);
    LCD_WR_DATA(LCD_RegValue);
}

// Configure the lcddev fields for the display orientation.
static void LCD_direction(u8 direction) {
    switch (direction) {
    case 0:
        LCD_WriteReg(0x36, (1 << 3) | (0 << 6) | (0 << 7)); // BGR==1,MY==0,MX==0,MV==0
        // LCD_WR_REG(0x36);
        // LCD_WR_DATA((1 << 3) | (0 << 6) | (0 << 7));
        break;
    case 1:
        LCD_WriteReg(0x36, (1 << 3) | (0 << 7) | (1 << 6) | (1 << 5)); // BGR==1,MY==1,MX==0,MV==1
        // LCD_WR_REG(0x36);
        // LCD_WR_DATA((1 << 3) | (0 << 7) | (1 << 6) | (1 << 5));
        break;
    case 2:
        LCD_WriteReg(0x36, (1 << 3) | (1 << 6) | (1 << 7)); // BGR==1,MY==0,MX==0,MV==0
        // LCD_WR_REG(0x36);
        // LCD_WR_DATA((1 << 3) | (1 << 6) | (1 << 7));
        break;
    case 3:
        LCD_WriteReg(0x36, (1 << 3) | (1 << 7) | (1 << 5)); // BGR==1,MY==1,MX==0,MV==1
        // LCD_WR_REG(0x36);
        // LCD_WR_DATA((1 << 3) | (1 << 7) | (1 << 5));
        break;
    default:
        break;
    }
}

void init_screen(u8 direction) {
    while (SPI->SR & SPI_SR_BSY)
        ;
    SPI->CR2 &= ~SPI_CR2_DS; // bad value forces it back to 8-bit mode

    CS_LOW();
    RESET_LOW();          // Assert reset
    nano_wait(100000000); // Wait 0.1s
    RESET_HIGH();         // De-assert reset
    nano_wait(50000000);  // Wait 0.05s

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

    LCD_direction(direction);

    while (SPI->SR & SPI_SR_BSY)
        ;
    CS_HIGH();
    SPI->CR2 |= SPI_CR2_DS; // back to 16 bit
}
