#include "sd_spi.h"

void init_spi(void) {
    // NSS PB12 (AF0)
    // SCK PB13 (AF0)
    // MISO PC2 (AF1)
    // MOSI PC3 (AF1)

    // GPIO
    RCC->AHBENR |= RCC_AHBENR_GPIOBEN | RCC_AHBENR_GPIOCEN;

    GPIOB->MODER &= ~(GPIO_MODER_MODER12 | GPIO_MODER_MODER13);
    GPIOB->MODER |= (GPIO_MODER_MODER12_1 | GPIO_MODER_MODER13_1);
    GPIOB->AFR[1] &= ~(GPIO_AFRH_AFR12 | GPIO_AFRH_AFR13);

    GPIOC->MODER &= ~(GPIO_MODER_MODER2 | GPIO_MODER_MODER3);
    GPIOC->MODER |= (GPIO_MODER_MODER2_1 | GPIO_MODER_MODER3_1);
    GPIOC->AFR[0] &= ~(GPIO_AFRL_AFR2 | GPIO_AFRL_AFR3);
    GPIOC->AFR[0] |= 0x00000100 | 0x00001000;

    // SPI (with NSS)
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
    SD_SPI->CR1 &= ~SPI_CR1_SPE;                               // turn off SPI
    SD_SPI->CR1 &= ~(SPI_CR1_BR | SPI_CR1_SSM);                // clear bitrate, NSS management
    SD_SPI->CR1 |= SPI_CR1_MSTR | SPI_CR1_BR_1 | SPI_CR1_BR_2; // 375 kHz bitrate, master mode
    SD_SPI->CR2 =
        (SD_SPI->CR2 & ~(SPI_CR2_DS)) | 0x0700 | SPI_CR2_FRXTH; // 8 bit data size, RXNE at 1/4 full
    SD_SPI->CR2 |= SPI_CR2_NSSP | SPI_CR2_SSOE;                 // SS enable
    SD_SPI->CR1 |= SPI_CR1_SPE;                                 // turn on SPI

    // DMA
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    DMA1_Channel2->CCR &= ~DMA_CCR_EN;
    DMA1_Channel2->CCR = DMA_CCR_TCIE | DMA_CCR_MINC;
    DMA1_Channel2->CPAR = (uint32_t)&SD_SPI->DR;
    DMA1_Channel2->CNDTR = 0;
    NVIC_EnableIRQ(DMA1_Ch2_3_DMA2_Ch1_2_IRQn);
    SD_SPI->CR2 |= SPI_CR2_RXDMAEN;
}

void send_spi(uint8_t byte) {
    wait_for_spi();
    set_spi(byte);
}

void _set_ss(bool on) {
    wait_for_spi();
    SD_SPI->CR1 &= ~SPI_CR1_SPE;
    discard_spi_DR();
    if (on) {
        SD_SPI->CR2 |= SPI_CR2_NSSP | SPI_CR2_SSOE; // SS enable
    } else {
        SD_SPI->CR2 &= ~(SPI_CR2_NSSP | SPI_CR2_SSOE); // SS disable
    }
    wait_for_spi();
    SD_SPI->CR1 |= SPI_CR1_SPE;
}

void receive_spi_no_wait(void) {
    wait_for_spi();
    discard_spi_DR();
    set_spi(0xFF);
}

uint8_t receive_spi(void) {
    receive_spi_no_wait();
    wait_for_spi();
    return get_spi();
}

void flush_spi() {
    while (receive_spi() != 0xFF)
        ;
}

void send_clocks(int n) {
    _set_ss(false);

    for (int i = 0; i < n; i++) {
        send_spi(0xFF);
    }

    _set_ss(true);
}

void receive_string(char *loc, int length) {
    int i;
    for (i = 0; i < length; i++) {
        loc[i] = receive_spi();
    }
    loc[i] = '\0';
}
